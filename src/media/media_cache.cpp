#include "media/media_cache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

namespace ytp {
namespace {

void setError(QString* destination, const QString& message) {
    if (destination != nullptr) {
        *destination = message;
    }
}

QString ffmpegExecutable() {
    auto executable = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    return executable.isEmpty() ? QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe") : executable;
}

bool runFfmpeg(const QStringList& arguments, QString* errorMessage, int timeoutMs = 60'000,
               const std::atomic_bool* cancellationRequested = nullptr) {
    QProcess process;
    process.start(ffmpegExecutable(), arguments);
    if (!process.waitForStarted(5'000)) {
        process.kill();
        setError(errorMessage, process.errorString());
        return false;
    }
    QElapsedTimer timer;timer.start();
    while(process.state()!=QProcess::NotRunning&&(timeoutMs<0||timer.elapsed()<timeoutMs)){
        process.waitForFinished(50);
        if(cancellationRequested&&cancellationRequested->load()){
            process.kill();process.waitForFinished(5'000);
            setError(errorMessage,QStringLiteral("FFmpeg cache generation cancelled."));
            return false;
        }
    }
    if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5'000);setError(errorMessage,QStringLiteral("FFmpeg cache generation timed out."));return false;}
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(errorMessage, QString::fromUtf8(process.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

QString rgbRemap(const QString& x,const QString& y){return QString("format=gbrp,geq=r='r(%1,%2)':g='g(%1,%2)':b='b(%1,%2)'").arg(x,y);}

void appendAdvancedVideoEffect(QStringList& filters,const EffectInstance& effect){
    const auto v=[&](const char*n,double d){const auto*p=findParameter(effect,n);return p?p->value:d;};
    if(effect.type=="rgb_split")filters<<QString("rgbashift=rh=%1:rv=%2:bh=%3:bv=%4:edge=wrap").arg(static_cast<int>(v("offset",12))).arg(static_cast<int>(v("vertical",0))).arg(-static_cast<int>(v("offset",12))).arg(-static_cast<int>(v("vertical",0)));
    else if(effect.type=="chromatic_aberration")filters<<QString("chromashift=cbh=%1:crh=%2:edge=wrap").arg(static_cast<int>(v("offset",8))).arg(-static_cast<int>(v("offset",8)));
    else if(effect.type=="wave_warp")filters<<QString("scroll=h=%1:v=%2").arg(v("amount",.08)).arg(v("speed",.15));
    else if(effect.type=="lens_warp")filters<<QString("lenscorrection=k1=%1:k2=%2:i=bilinear").arg(v("amount",.35)).arg(v("secondary",0));
    else if(effect.type=="kaleidoscope")filters<<QString("frei0r=filter_name=kaleid0sc0pe:filter_params='%1|.5|.5'").arg(std::clamp(v("strength",.7),0.0,1.0));
    else if(effect.type=="edge_echo")filters<<QString("edgedetect=mode=colormix:high=%1").arg(std::max(.01,v("strength",.4)));
    else if(effect.type=="recursive_trails"){const auto frames=std::clamp(static_cast<int>(v("frames",6)),2,30);const auto decay=std::clamp(v("decay",.8),0.0,1.0);QStringList weights;for(int index=0;index<frames;++index)weights<<QString::number(std::pow(decay,index),'f',6);filters<<QString("tmix=frames=%1:weights='%2'").arg(frames).arg(weights.join(' '));}
    else if(effect.type=="time_smear")filters<<QString("tmix=frames=%1").arg(std::clamp(static_cast<int>(v("frames",8)),2,30));
    else if(effect.type=="frame_blend")filters<<"tblend=all_mode=average";
    else if(effect.type=="screen_shake"){const int amount=std::max(0,static_cast<int>(v("amount",12)));filters<<QString("crop=iw-%1:ih-%1*ih/iw:x='%2+%2*sin(n*%3)':y='%2*ih/iw+%2*ih/iw*cos(n*%3)'").arg(amount*2).arg(amount).arg(v("speed",18)/10);}
    else if(effect.type=="chroma_key")filters<<QString("chromakey=0x00FF00:%1:%2").arg(v("similarity",.15)).arg(v("blend",.05));
    else if(effect.type=="datamosh")filters<<QString("tblend=all_mode=difference,lagfun=decay=%1").arg(v("decay",.9));
    else if(effect.type=="scanlines")filters<<QString("drawgrid=w=iw:h=%1:t=1:c=black@%2").arg(std::max(2,static_cast<int>(v("spacing",4)))).arg(v("opacity",.28));
    else if(effect.type=="vhs_noise")filters<<QString("noise=alls=%1:allf=t+u,chromashift=cbh=%2:crh=%3:edge=wrap").arg(v("strength",18)).arg(static_cast<int>(v("chroma",5))).arg(-static_cast<int>(v("chroma",5)));
    else if(effect.type=="solarize"){const auto threshold=std::clamp(v("threshold",.5),0.0,1.0)*255.0;filters<<QString("lutrgb=r='if(gt(val,%1),255-val,val)':g='if(gt(val,%1),255-val,val)':b='if(gt(val,%1),255-val,val)'").arg(threshold);}
    else if(effect.type=="emboss")filters<<QString("format=gbrp,convolution='-2 -1 0 -1 1 1 0 1 2:-2 -1 0 -1 1 1 0 1 2:-2 -1 0 -1 1 1 0 1 2:0 0 0 0 1 0 0 0 0',eq=contrast=%1").arg(1.0+std::max(0.0,v("amount",1))*.35);
    else if(effect.type=="neon_edges")filters<<QString("edgedetect=mode=colormix:high=%1,eq=saturation=%2:contrast=1.25").arg(std::max(.01,v("strength",.35))).arg(v("saturation",2.4));
    else if(effect.type=="vignette")filters<<QString("vignette=angle=%1").arg(std::clamp(v("strength",.65),.05,1.0));
    else if(effect.type=="color_cycle")filters<<QString("hue=H='2*PI*t*%1'").arg(v("speed",.6));
    else if(effect.type=="strobe"){const auto rate=std::max(1.0,v("rate",8));const auto period=1.0/rate;filters<<QString("drawbox=x=0:y=0:w=iw:h=ih:c=white@0.88:t=fill:enable='lt(mod(t,%1),%2)'").arg(period).arg(period*std::clamp(v("duty",.45),.05,.95));}
    else if(effect.type=="channel_swap"){const auto mix=std::clamp(v("mix",1),0.0,1.0);filters<<QString("colorchannelmixer=rr=%1:rb=%2:gg=1:br=%2:bb=%1").arg(1.0-mix).arg(mix);}
    else if(effect.type=="vertical_roll")filters<<QString("scroll=v=%1").arg(v("speed",.18));
    else if(effect.type=="bad_tv")filters<<QString("noise=alls=%1:allf=t+u,drawgrid=w=iw:h=4:t=1:c=black@%2,scroll=v=%3").arg(v("noise",16)).arg(v("scanlines",.25)).arg(v("roll",.03));
    else if(effect.type=="cartoon_edges")filters<<QString("edgedetect=mode=colormix:high=%1,eq=saturation=%2:contrast=1.2").arg(std::max(.01,v("edge",.28))).arg(v("saturation",2));
    else if(effect.type=="impact_zoom"){const auto zoom=std::clamp(v("zoom",1.4),1.0,3.0),ox=std::clamp(v("offsetX",0),-1.0,1.0),oy=std::clamp(v("offsetY",0),-1.0,1.0);filters<<rgbRemap(QString("W/2+(X-W/2)/%1+%2*W*(1-1/%1)/2").arg(zoom).arg(ox),QString("H/2+(Y-H/2)/%1+%2*H*(1-1/%1)/2").arg(zoom).arg(oy));}
    else if(effect.type=="spin")filters<<QString("rotate='%1*t':ow=iw:oh=ih:c=black").arg(v("speed",1.2));
    else if(effect.type=="pendulum")filters<<QString("rotate='%1*PI/180*sin(2*PI*t*%2)':ow=iw:oh=ih:c=black").arg(v("angle",14)).arg(v("speed",1.6));
    else if(effect.type=="perspective_tilt"){const auto amount=std::clamp(v("amount",.18),-.45,.45),vertical=std::clamp(v("vertical",.10),-.4,.4);filters<<QString("perspective=x0='W*%1':y0='H*%2':x1='W*(1-%1)':y1='H*%3':x2=0:y2=H:x3=W:y3=H:sense=destination:eval=init").arg(std::max(0.0,amount)).arg(std::max(0.0,vertical)).arg(std::max(0.0,-vertical));}
    else if(effect.type=="elastic_wave")filters<<QString("format=gbrp,geq=r='r(mod(X+%1*sin(Y/%2+T*%3)+W,W),Y)':g='g(mod(X+%4*sin(Y/%2+T*%3*.87)+W,W),Y)':b='b(mod(X+%5*sin(Y/%2+T*%3*1.13)+W,W),Y)'").arg(v("amount",24)).arg(std::max(4.0,v("spacing",18))).arg(v("speed",8)).arg(v("amount",24)*.72).arg(v("amount",24)*1.18);
    else if(effect.type=="glitch_bands")filters<<QString("format=gbrp,geq=r='r(mod(X+%1*gt(mod(Y+floor(T*%3),%2),%2*.66)+W,W),Y)':g='g(X,Y)':b='b(mod(X-%4*gt(mod(Y+floor(T*%3*.79),%2*1.16),%2*.78)+W,W),Y)'").arg(v("amount",42)).arg(std::max(8.0,v("band",48))).arg(v("speed",120)).arg(v("amount",42)*.8);
    else if(effect.type=="thermal")filters<<QString("pseudocolor=preset=%1").arg(std::clamp(static_cast<int>(v("palette",7)),0,20));
    else if(effect.type=="motion_burn")filters<<QString("lagfun=decay=%1").arg(std::clamp(v("decay",.96),.5,.999));
    else if(effect.type=="block_shuffle")filters<<QString("shufflepixels=mode=block:w=%1:h=%2:seed=%3").arg(std::clamp(static_cast<int>(v("width",32)),2,256)).arg(std::clamp(static_cast<int>(v("height",24)),2,256)).arg(std::clamp(static_cast<int>(v("seed",13)),0,65535));
    else if(effect.type=="shear")filters<<QString("shear=shx=%1:shy=%2:fillcolor=black:interp=bilinear").arg(std::clamp(v("horizontal",.35),-2.0,2.0)).arg(std::clamp(v("vertical",-.12),-2.0,2.0));
    else if(effect.type=="fisheye"){const auto scale=QString("(1+%1*pow(hypot(X-W/2,Y-H/2)/hypot(W/2,H/2),2))").arg(v("strength",.72));filters<<rgbRemap(QString("W/2+(X-W/2)*%1").arg(scale),QString("H/2+(Y-H/2)*%1").arg(scale));}
    else if(effect.type=="tiny_planet"){const auto angle=QString("%1*sin(T*%2)*(1-min(1,hypot(X-W/2,Y-H/2)/(min(W,H)*%3)))").arg(v("strength",2.4)).arg(v("speed",.35)).arg(v("radius",.75));filters<<rgbRemap(QString("W/2+(X-W/2)*cos(%1)-(Y-H/2)*sin(%1)").arg(angle),QString("H/2+(X-W/2)*sin(%1)+(Y-H/2)*cos(%1)").arg(angle));}
    else if(effect.type=="oscilloscope")filters<<QString("oscilloscope=x=.5:y=.5:s=%1:t=%2:o=%3:tx=.5:ty=.86:tw=.82:th=.24:c=7:g=1:st=1:sc=1").arg(v("size",.72)).arg(v("tilt",.22)).arg(v("opacity",.9));
    else if(effect.type=="frame_randomizer")filters<<QString("random=frames=%1:seed=%2").arg(std::clamp(static_cast<int>(v("frames",8)),2,60)).arg(std::clamp(static_cast<int>(v("seed",23)),0,65535));
    else if(effect.type=="motion_amplify")filters<<QString("amplify=radius=%1:factor=%2:threshold=%3:tolerance=1").arg(std::clamp(static_cast<int>(v("radius",2)),1,12)).arg(v("factor",7)).arg(v("threshold",3));
    else if(effect.type=="frame_xor")filters<<QString("tblend=all_mode=xor:all_opacity=%1").arg(std::clamp(v("opacity",1),.05,1.0));
    else if(effect.type=="pixel_bloom")filters<<QString("pixelize=w=%1:h=%2:mode=%3").arg(std::clamp(static_cast<int>(v("width",24)),2,128)).arg(std::clamp(static_cast<int>(v("height",18)),2,128)).arg(std::clamp(static_cast<int>(v("mode",2)),0,2));
    else if(effect.type=="xray_edges")filters<<QString("format=gbrp,kirsch=scale=%1,negate,pseudocolor=preset=%2").arg(v("strength",2)).arg(std::clamp(static_cast<int>(v("palette",7)),0,20));
    else if(effect.type=="horizontal_shuffle")filters<<QString("shufflepixels=mode=horizontal:w=%1:h=%1:seed=%2").arg(std::clamp(static_cast<int>(v("strip",14)),2,256)).arg(std::clamp(static_cast<int>(v("seed",41)),0,65535));
    else if(effect.type=="vertical_shuffle")filters<<QString("shufflepixels=mode=vertical:w=%1:h=%1:seed=%2").arg(std::clamp(static_cast<int>(v("strip",14)),2,256)).arg(std::clamp(static_cast<int>(v("seed",73)),0,65535));
    else if(effect.type=="temporal_mosaic"){const auto grid=std::clamp(static_cast<int>(v("grid",3)),2,5),frames=grid*grid;filters<<QString("tile=layout=%1x%1:nb_frames=%2:overlap=%3:init_padding=%3:padding=%4").arg(grid).arg(frames).arg(frames-1).arg(std::clamp(static_cast<int>(v("border",1)),0,12));}
    else if(effect.type=="dilation_bloom")for(int i=0;i<std::clamp(static_cast<int>(v("iterations",2)),1,8);++i)filters<<"dilation";
    else if(effect.type=="erosion_decay")for(int i=0;i<std::clamp(static_cast<int>(v("iterations",2)),1,8);++i)filters<<"erosion";
    else if(effect.type=="field_corruption")filters<<QString("il=luma_mode=interleave:chroma_mode=interleave:luma_swap=%1:chroma_swap=%2").arg(v("swapLuma",1)>=.5?1:0).arg(v("swapChroma",1)>=.5?1:0);
    else if(effect.type=="chroma_planes"){const auto mode=std::clamp(static_cast<int>(v("mode",1)),0,2);filters<<(mode==0?QString("shuffleplanes=map0=0:map1=1:map2=2"):mode==1?QString("shuffleplanes=map0=0:map1=2:map2=1"):QString("shuffleplanes=map0=2:map1=1:map2=0"));}
    else if(effect.type=="frame_skip")filters<<QString("framestep=%1,fps=30").arg(std::clamp(static_cast<int>(v("step",5)),2,20));
    else if(effect.type=="funhouse"){const auto amount=QString::number(v("amount",28)),spacing=QString::number(std::max(4.0,v("spacing",20))),speed=QString::number(v("speed",7));filters<<rgbRemap(QString("mod(X+%1*sin(Y/%2+T*%3)+W,W)").arg(amount,spacing,speed),QString("mod(Y+%1*cos(X/%2-T*%3)+H,H)").arg(amount,spacing,speed));}
    else if(effect.type=="vortex"){const auto angle=QString("%1*sin(T*%2)*exp(-hypot(X-W/2,Y-H/2)/(min(W,H)*%3))").arg(v("strength",1.7)).arg(v("speed",2)).arg(v("radius",.45));filters<<rgbRemap(QString("W/2+(X-W/2)*cos(%1)-(Y-H/2)*sin(%1)").arg(angle),QString("H/2+(X-W/2)*sin(%1)+(Y-H/2)*cos(%1)").arg(angle));}
    else if(effect.type=="radial_ripple"){const auto scale=QString("(1+%1*sin(hypot(X-W/2,Y-H/2)/%2-T*%3))").arg(v("amount",.18)).arg(std::max(3.0,v("spacing",14))).arg(v("speed",8));filters<<rgbRemap(QString("W/2+(X-W/2)*%1").arg(scale),QString("H/2+(Y-H/2)*%1").arg(scale));}
    else if(effect.type=="melt"){const auto shift=QString("%1*(.5+.5*sin(X/%2+T*%3))").arg(v("amount",34)).arg(std::max(4.0,v("spacing",18))).arg(v("speed",5));filters<<rgbRemap(QString("X"),QString("mod(Y+%1+H,H)").arg(shift));}
    else if(effect.type=="halftone")filters<<QString("format=gray,geq=lum='if(gt(lum(X,Y),128+%1*sin(X/%2)*sin(Y/%2)),255,0)'").arg(v("contrast",55)).arg(std::max(1.0,v("cell",3)));
    else if(effect.type=="temporal_heat")filters<<QString("tblend=all_mode=heat:all_opacity=%1").arg(std::clamp(v("opacity",.8),.05,1.0));
    else if(effect.type=="temporal_stain")filters<<QString("tblend=all_mode=stain:all_opacity=%1").arg(std::clamp(v("opacity",.8),.05,1.0));
    else if(effect.type=="video_feedback")filters<<QString("frei0r=filter_name=vertigo:filter_params='%1|%2'").arg(v("phase",.18)).arg(v("zoom",.72));
    else if(effect.type=="pixel_sort")filters<<"frei0r=filter_name=pixels0rt";
    else if(effect.type=="water_surface")filters<<QString("frei0r=filter_name=water:filter_params='%1|%2'").arg(v("amplitude",.5)).arg(v("frequency",.5));
    else if(effect.type=="elastic_scale")filters<<QString("frei0r=filter_name=elastic_scale:filter_params='%1|%2|%3'").arg(v("centerX",.5)).arg(v("centerY",.5)).arg(v("strength",.65));
    else if(effect.type=="analog_nosync")filters<<QString("frei0r=filter_name=nosync0r:filter_params='%1'").arg(v("strength",.6));
    else if(effect.type=="film_gate_weave")filters<<QString("frei0r=filter_name=gateweave:filter_params='%1|%2|%3'").arg(v("interval",.35)).arg(v("horizontal",.5)).arg(v("vertical",.35));
    else if(effect.type=="nervous_frames")filters<<QString("frei0r=filter_name=nervous:filter_params='%1'").arg(v("strength",.65));
    else if(effect.type=="light_graffiti")filters<<QString("frei0r=filter_name=lightgraffiti:filter_params='%1|%2'").arg(v("sensitivity",.55)).arg(v("decay",.5));
    else if(effect.type=="digital_glitch")filters<<QString("frei0r=filter_name=glitch0r:filter_params='%1|%2|%3|%4'").arg(v("frequency",.55)).arg(v("blockHeight",.28)).arg(v("shift",.8)).arg(v("color",.65));
    else if(effect.type=="ordered_dither")filters<<QString("frei0r=filter_name=dither:filter_params='%1|%2'").arg(v("levels",.5)).arg(v("pattern",.6));
    else if(effect.type=="cmyk_halftone")filters<<QString("frei0r=filter_name=colorhalftone:filter_params='%1|.15|%2|.75'").arg(v("dotSize",.45)).arg(v("angle",.45));
    else if(effect.type=="clone_grid")filters<<QString("frei0r=filter_name=cairoimagegrid:filter_params='%1|%2'").arg(v("columns",.35)).arg(v("rows",.35));
    else if(effect.type=="edge_glow_native")filters<<QString("frei0r=filter_name=edgeglow:filter_params='%1'").arg(v("strength",.7));
    else if(effect.type=="soft_glow_native")filters<<QString("frei0r=filter_name=softglow:filter_params='%1|%2|%3'").arg(v("blur",.6)).arg(v("brightness",.75)).arg(v("sharpness",.45));
    else if(effect.type=="ink_cartoon")filters<<QString("frei0r=filter_name=cartoon:filter_params='%1|%2'").arg(v("triplevel",.55)).arg(v("edgeSpace",.4));
    else if(effect.type=="film_projector")filters<<QString("frei0r=filter_name=filmgrain:filter_params='%1|.2|.2|.2|.1|%2|%3'").arg(v("grain",.35)).arg(v("dust",.25)).arg(v("flicker",.3));
    else if(effect.type=="frame_delay")filters<<QString("frei0r=filter_name=delay0r:filter_params='%1'").arg(v("delay",.55));
}

} // namespace

QString MediaCache::cacheDirectory() {
    const auto overridePath = qEnvironmentVariable("YTP_CACHE_DIR");
    const auto path = overridePath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/media")
        : overridePath;
    QDir{}.mkpath(path);
    return path;
}

QString MediaCache::thumbnailPath(const QString& cacheKey) {
    // Versioned because older fast-seek thumbnails could resolve to the same
    // preceding keyframe for several short library clips.
    return cacheDirectory() + '/' + cacheKey + QStringLiteral("-thumb-v2.jpg");
}

QString MediaCache::timelineThumbnailPath(const QString& cacheKey) {
    return cacheDirectory() + '/' + cacheKey + QStringLiteral("-timeline-filmstrip-v2.jpg");
}

QString MediaCache::timelineFramePath(const QString& mediaId, qint64 sourceTimeMs) {
    const auto identity = mediaId + QLatin1Char('|') + QString::number(sourceTimeMs);
    const auto digest = QString::fromLatin1(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    return cacheDirectory() + '/' + mediaId.left(12) + QLatin1Char('-') + digest +
           QStringLiteral("-timeline-frame-v2.jpg");
}

QString MediaCache::timelineWaveformPath(const QString& cacheKey) {
    return cacheDirectory() + '/' + cacheKey + QStringLiteral("-timeline-wave-v1.png");
}

QString MediaCache::timelineVisualKey(const TimelineItem& item) {
    const auto identity = QString::fromStdString(item.id) + QLatin1Char('|') +
        QString::fromStdString(item.mediaAssetId) + QLatin1Char('|') +
        QString::fromStdString(item.sourceRange.start().toString()) + QLatin1Char('|') +
        QString::fromStdString(item.sourceRange.duration().toString());
    return QString::fromStdString(item.id).left(12) + QLatin1Char('-') +
        QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

QString MediaCache::waveformPath(const QString& cacheKey) {
    return cacheDirectory() + '/' + cacheKey + QStringLiteral("-wave-v3.png");
}
QString MediaCache::proxyPath(const QString& cacheKey) { return cacheDirectory()+'/'+cacheKey+QStringLiteral("-proxy.mp4"); }
QString MediaCache::effectPreviewPath(const QString& cacheKey) { return cacheDirectory()+'/'+cacheKey+QStringLiteral("-preview.jpg"); }
QString MediaCache::playbackPreviewPath(const QString& cacheKey) { return cacheDirectory()+'/'+cacheKey+QStringLiteral("-playback.mp4"); }
QString MediaCache::sequencePreviewPath(const QString& cacheKey){return cacheDirectory()+'/'+cacheKey+QStringLiteral("-sequence.mp4");}
CacheInfo MediaCache::cacheInfo(){CacheInfo info;QDir directory(cacheDirectory());for(const auto&file:directory.entryInfoList(QDir::Files|QDir::NoDotAndDotDot)){++info.files;info.bytes+=file.size();}return info;}
bool MediaCache::clearGenerated(QString* errorMessage){QDir directory(cacheDirectory());bool ok=true;for(const auto&file:directory.entryInfoList(QDir::Files|QDir::NoDotAndDotDot))if(!QFile::remove(file.absoluteFilePath()))ok=false;if(!ok)setError(errorMessage,"One or more cache files could not be removed because they are in use.");return ok;}

bool MediaCache::generateThumbnail(const QString& mediaPath,
                                   const Rational& time,
                                   const QString& cacheKey,
                                   QString* errorMessage) {
    const auto output = thumbnailPath(cacheKey);
    if (QFileInfo::exists(output)) {
        return true;
    }
    const auto seconds = QString::number(static_cast<double>(time.asLongDouble()), 'f', 6);
    if (runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                   QStringLiteral("-y"), QStringLiteral("-i"), mediaPath,
                   QStringLiteral("-ss"), seconds, QStringLiteral("-frames:v"), QStringLiteral("1"),
                   QStringLiteral("-vf"), QStringLiteral("scale=320:-2"), output}, errorMessage)) {
        return true;
    }
    // Audio-only assets still receive a useful thumbnail instead of a blank tile.
    QFile::remove(output);
    const bool waveform = runFfmpeg({
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"), QStringLiteral("-i"), mediaPath, QStringLiteral("-ss"), seconds,
        QStringLiteral("-t"), QStringLiteral("2"), QStringLiteral("-filter_complex"),
        QStringLiteral("aformat=channel_layouts=mono,showwavespic=s=320x180:colors=0xdf4f8b"),
        QStringLiteral("-frames:v"), QStringLiteral("1"), output}, errorMessage);
    if (waveform && errorMessage != nullptr) {
        errorMessage->clear();
    }
    return waveform;
}

bool MediaCache::generateWaveform(const QString& mediaPath,
                                  const QString& cacheKey,
                                  QString* errorMessage) {
    const auto output = waveformPath(cacheKey);
    if (QFileInfo::exists(output)) {
        return true;
    }
    return runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                      QStringLiteral("-y"), QStringLiteral("-i"), mediaPath,
                      QStringLiteral("-filter_complex"),
                      QStringLiteral("aformat=channel_layouts=mono,showwavespic=s=16384x128:colors=0xf4f7ff"),
                      QStringLiteral("-frames:v"), QStringLiteral("1"), output}, errorMessage);
}

bool MediaCache::generateTimelineThumbnail(const QString& mediaPath,
                                           const TimeRange& sourceRange,
                                           const QString& cacheKey,
                                           QString* errorMessage) {
    const auto output = timelineThumbnailPath(cacheKey);
    if (QFileInfo::exists(output)) return true;
    constexpr int sampleCount = 32;
    QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                          QStringLiteral("error"), QStringLiteral("-y")};
    const auto start = static_cast<double>(sourceRange.start().asLongDouble());
    const auto duration = std::max(0.001, static_cast<double>(sourceRange.duration().asLongDouble()));
    for (int sample = 0; sample < sampleCount; ++sample) {
        // Independent input seeks avoid decoding an entire long source just to build its filmstrip.
        const auto fraction = static_cast<double>(sample) / static_cast<double>(sampleCount);
        arguments << QStringLiteral("-ss")
                  << QString::number(start + duration * fraction, 'f', 6)
                  << QStringLiteral("-i") << mediaPath;
    }
    QStringList filters;
    QStringList inputs;
    for (int sample = 0; sample < sampleCount; ++sample) {
        const auto label = QStringLiteral("v%1").arg(sample);
        filters << QStringLiteral("[%1:v]scale=128:72:force_original_aspect_ratio=increase,"
                                  "crop=128:72,setsar=1[%2]").arg(sample).arg(label);
        inputs << QStringLiteral("[%1]").arg(label);
    }
    filters << inputs.join(QString{}) + QStringLiteral("hstack=inputs=%1[out]").arg(sampleCount);
    arguments << QStringLiteral("-filter_complex") << filters.join(QLatin1Char(';'))
              << QStringLiteral("-map") << QStringLiteral("[out]")
              << QStringLiteral("-frames:v") << QStringLiteral("1")
              << QStringLiteral("-q:v") << QStringLiteral("5") << output;
    return runFfmpeg(arguments, errorMessage);
}

bool MediaCache::generateTimelineFrame(const QString& mediaPath,
                                       const Rational& sourceTime,
                                       const QString& mediaId,
                                       qint64 quantizedSourceTimeMs,
                                       QString* errorMessage) {
    const auto output = timelineFramePath(mediaId, quantizedSourceTimeMs);
    if (QFileInfo::exists(output)) return true;
    return runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), mediaPath,
                      QStringLiteral("-ss"),
                      QString::number(static_cast<double>(sourceTime.asLongDouble()), 'f', 6),
                      QStringLiteral("-frames:v"), QStringLiteral("1"),
                      QStringLiteral("-vf"),
                      QStringLiteral("scale=144:81:force_original_aspect_ratio=increase,crop=144:81,setsar=1"),
                      QStringLiteral("-q:v"), QStringLiteral("4"), output}, errorMessage);
}

bool MediaCache::generateTimelineWaveform(const QString& mediaPath,
                                          const TimeRange& sourceRange,
                                          const QString& cacheKey,
                                          QString* errorMessage) {
    const auto output = timelineWaveformPath(cacheKey);
    if (QFileInfo::exists(output)) return true;
    return runFfmpeg({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                      QStringLiteral("-y"), QStringLiteral("-ss"),
                      QString::number(static_cast<double>(sourceRange.start().asLongDouble()), 'f', 6),
                      QStringLiteral("-t"),
                      QString::number(static_cast<double>(sourceRange.duration().asLongDouble()), 'f', 6),
                      QStringLiteral("-i"), mediaPath, QStringLiteral("-filter_complex"),
                      QStringLiteral("aformat=channel_layouts=mono,showwavespic=s=8192x128:colors=0xf4f7ff"),
                      QStringLiteral("-frames:v"), QStringLiteral("1"), output}, errorMessage);
}

bool MediaCache::generateProxy(const QString& mediaPath,const QString& cacheKey,QString* errorMessage){
    const auto output=proxyPath(cacheKey);if(QFileInfo::exists(output))return true;
    return runFfmpeg({"-hide_banner","-loglevel","error","-y","-i",mediaPath,"-vf","scale='min(960,iw)':-2","-c:v","libx264","-preset","ultrafast","-crf","28","-c:a","aac","-b:a","128k",output},errorMessage,-1);
}

bool MediaCache::generateEffectPreview(const QString& mediaPath,const TimelineItem& item,Rational sourceTime,PreviewQuality quality,QString* errorMessage,const std::atomic_bool* cancellationRequested){
    QStringList filters;const auto&t=item.transform;
    if(t.cropLeft>0||t.cropRight>0||t.cropTop>0||t.cropBottom>0)filters<<QString("crop=iw*%1:ih*%2:iw*%3:ih*%4").arg(1-t.cropLeft-t.cropRight,0,'f',6).arg(1-t.cropTop-t.cropBottom,0,'f',6).arg(t.cropLeft,0,'f',6).arg(t.cropTop,0,'f',6);
    if(t.flipHorizontal)filters<<"hflip";if(t.flipVertical)filters<<"vflip";
    if(t.scaleX!=1||t.scaleY!=1)filters<<QString("scale=iw*%1:ih*%2").arg(t.scaleX,0,'f',4).arg(t.scaleY,0,'f',4);
    if(t.rotation!=0)filters<<QString("rotate=%1*PI/180:ow=rotw(iw):oh=roth(ih):c=black").arg(t.rotation,0,'f',3);
    if(t.positionX!=0||t.positionY!=0)filters<<QString("pad=iw+%1:ih+%2:%3:%4:black").arg(std::abs(static_cast<int>(t.positionX))*2).arg(std::abs(static_cast<int>(t.positionY))*2).arg(std::max(0,static_cast<int>(t.positionX))).arg(std::max(0,static_cast<int>(t.positionY)));
    if(t.opacity<1)filters<<QString("format=rgba,colorchannelmixer=aa=%1,format=rgb24").arg(t.opacity,0,'f',4);
    for(const auto&e:item.effects){if(!e.enabled)continue;const auto v=[&](const char*n,double fallback){const auto*p=findParameter(e,n);return p?p->value:fallback;};if(e.type=="brightness_contrast")filters<<QString("eq=brightness=%1:contrast=%2").arg(v("brightness",0)).arg(v("contrast",1));else if(e.type=="saturation")filters<<QString("eq=saturation=%1").arg(v("amount",1));else if(e.type=="hue")filters<<QString("hue=h=%1").arg(v("degrees",0));else if(e.type=="invert")filters<<"negate";else if(e.type=="grayscale")filters<<"hue=s=0";else if(e.type=="blur")filters<<QString("boxblur=%1").arg(std::max(1.0,v("radius",5)/3));else if(e.type=="sharpen")filters<<QString("unsharp=5:5:%1").arg(v("amount",1));else if(e.type=="pixelate"){const auto block=std::clamp(static_cast<int>(v("block",12)),2,100);filters<<QString("pixelize=w=%1:h=%1:mode=avg").arg(block);}else if(e.type=="threshold")filters<<QString("lutyuv=y='if(gte(val,%1),255,0)':u=128:v=128").arg(v("level",.5)*255);appendAdvancedVideoEffect(filters,e);}
    const int width=quality==PreviewQuality::Quarter?480:quality==PreviewQuality::Half?960:1280;filters<<QString("scale='min(%1,iw)':-2").arg(width);
    const auto output=effectPreviewPath(QString::fromStdString(item.id));QFile::remove(output);
    return runFfmpeg({"-hide_banner","-loglevel","error","-y","-ss",QString::number(static_cast<double>(sourceTime.asLongDouble()),'f',6),"-i",mediaPath,"-frames:v","1","-vf",filters.join(','),output},errorMessage,60'000,cancellationRequested);
}

bool MediaCache::generatePlaybackPreview(const QString& mediaPath,const TimelineItem& video,const TimelineItem& audio,const Track& videoTrack,const Track& audioTrack,const Sequence& sequence,QString* errorMessage){
    QStringList vf,af;const auto&t=video.transform;if(t.cropLeft>0||t.cropRight>0||t.cropTop>0||t.cropBottom>0)vf<<QString("crop=iw*%1:ih*%2:iw*%3:ih*%4").arg(1-t.cropLeft-t.cropRight).arg(1-t.cropTop-t.cropBottom).arg(t.cropLeft).arg(t.cropTop);if(t.flipHorizontal)vf<<"hflip";if(t.flipVertical)vf<<"vflip";if(t.scaleX!=1||t.scaleY!=1)vf<<QString("scale=iw*%1:ih*%2").arg(t.scaleX).arg(t.scaleY);if(t.rotation!=0)vf<<QString("rotate=%1*PI/180:ow=rotw(iw):oh=roth(ih):c=black").arg(t.rotation);if(t.positionX!=0||t.positionY!=0)vf<<QString("pad=iw+%1:ih+%2:%3:%4:black").arg(std::abs(static_cast<int>(t.positionX))*2).arg(std::abs(static_cast<int>(t.positionY))*2).arg(std::max(0,static_cast<int>(t.positionX))).arg(std::max(0,static_cast<int>(t.positionY)));if(t.opacity<1)vf<<QString("format=rgba,colorchannelmixer=aa=%1,format=rgb24").arg(t.opacity);if(video.reverse)vf<<"reverse";if(video.speed!=1)vf<<QString("setpts=PTS/%1").arg(video.speed,0,'f',6);
    const auto addVideo=[&](const std::vector<EffectInstance>&effects){
        for(const auto&e:effects){
            if(!e.enabled)continue;
            const auto v=[&](const char*n,double d){const auto*p=findParameter(e,n);return p?p->value:d;};
            if(e.type=="brightness_contrast")vf<<QString("eq=brightness=%1:contrast=%2").arg(v("brightness",0)).arg(v("contrast",1));
            else if(e.type=="saturation")vf<<QString("eq=saturation=%1").arg(v("amount",1));
            else if(e.type=="hue")vf<<QString("hue=h=%1").arg(v("degrees",0));
            else if(e.type=="invert")vf<<"negate";
            else if(e.type=="grayscale")vf<<"hue=s=0";
            else if(e.type=="blur")vf<<QString("boxblur=%1").arg(std::max(1.0,v("radius",5)/3));
            else if(e.type=="sharpen")vf<<QString("unsharp=5:5:%1").arg(v("amount",1));
            else if(e.type=="pixelate"){const auto b=std::clamp(static_cast<int>(v("block",12)),2,100);vf<<QString("pixelize=w=%1:h=%1:mode=avg").arg(b);}
            else if(e.type=="posterize"){const auto l=std::max(2.0,v("levels",6));vf<<QString("lutrgb=r='floor(val*%1/256)*255/(%1-1)':g='floor(val*%1/256)*255/(%1-1)':b='floor(val*%1/256)*255/(%1-1)'").arg(l);}
            else if(e.type=="threshold")vf<<QString("lutyuv=y='if(gte(val,%1),255,0)':u=128:v=128").arg(v("level",.5)*255);
            else if(e.type=="tint"){
                const auto mix=std::clamp(v("mix",.5),0.0,1.0);
                const auto channel=[&](const char*n){return 1.0+(std::clamp(v(n,1),0.0,2.0)-1.0)*mix;};
                vf<<QString("colorchannelmixer=rr=%1:gg=%2:bb=%3").arg(channel("red")).arg(channel("green")).arg(channel("blue"));
            }
        }
    };
    addVideo(video.effects);addVideo(videoTrack.effects);
    for(const auto&e:video.effects)if(e.enabled)appendAdvancedVideoEffect(vf,e);for(const auto&e:videoTrack.effects)if(e.enabled)appendAdvancedVideoEffect(vf,e);
    const int width=sequence.previewQuality==PreviewQuality::Quarter?480:sequence.previewQuality==PreviewQuality::Half?960:1280;vf<<QString("scale='min(%1,iw)':-2").arg(width);
    if(audio.reverse)af<<"areverse";const auto atempo=[&](double factor){while(factor>2){af<<"atempo=2";factor/=2;}while(factor<.5){af<<"atempo=.5";factor*=2;}af<<QString("atempo=%1").arg(factor,0,'f',6);};if(audio.speed!=1){if(audio.preservePitch)atempo(audio.speed);else af<<QString("asetrate=48000*%1,aresample=48000").arg(audio.speed,0,'f',6);}if(audio.pitchSemitones!=0){const auto factor=std::pow(2.0,audio.pitchSemitones/12.0);af<<QString("asetrate=48000*%1,aresample=48000").arg(factor,0,'f',6);atempo(1.0/factor);}
    const auto addAudio=[&](const AudioSettings&settings,const std::vector<EffectInstance>&effects){
        if(settings.gainDb!=0)af<<QString("volume=%1dB").arg(settings.gainDb);
        if(settings.pan!=0)af<<QString("pan=stereo|c0=%1*c0|c1=%2*c1").arg(settings.pan<=0?1:1-settings.pan).arg(settings.pan>=0?1:1+settings.pan);
        for(const auto&e:effects){
            if(!e.enabled)continue;
            const auto v=[&](const char*n,double d){const auto*p=findParameter(e,n);return p?p->value:d;};
            if(e.type=="eq")af<<QString("equalizer=f=100:g=%1,equalizer=f=1000:g=%2,equalizer=f=8000:g=%3").arg(v("lowDb",0)).arg(v("midDb",0)).arg(v("highDb",0));
            else if(e.type=="highpass")af<<QString("highpass=f=%1").arg(v("frequency",80));
            else if(e.type=="lowpass")af<<QString("lowpass=f=%1").arg(v("frequency",16000));
            else if(e.type=="compressor")af<<QString("acompressor=threshold=%1dB:ratio=%2:attack=%3:release=%4").arg(v("threshold",-18)).arg(v("ratio",4)).arg(v("attack",10)).arg(v("release",100));
            else if(e.type=="limiter")af<<QString("alimiter=limit=%1:release=%2:latency=1").arg(std::pow(10.0,v("ceiling",-1)/20)).arg(v("release",50));
            else if(e.type=="normalize")af<<QString("loudnorm=I=%1:TP=-1.5:LRA=11").arg(v("target",-14));
            else if(e.type=="reverb"){const auto room=std::clamp(v("room",.5),0.0,1.0),damping=std::clamp(v("damping",.5),0.0,1.0),mix=std::clamp(v("mix",.25),0.0,1.0);af<<QString("aecho=0.8:%1:%2:%3").arg(.12+mix*.78).arg(45+room*955).arg(.15+(1.0-damping)*.75);}
            else if(e.type=="delay"){const auto mix=std::clamp(v("mix",.3),0.0,1.0);af<<QString("aecho=0.8:%1:%2:%3").arg(.1+mix*.85).arg(v("time",250)).arg(std::clamp(v("feedback",.35),0.0,.95));}
            else if(e.type=="distortion"){const auto drive=std::clamp(v("drive",6),0.0,30.0),mix=std::clamp(v("mix",1),0.0,1.0);af<<QString("volume=%1dB,asoftclip=type=tanh:threshold=%2:output=%3:oversample=2").arg(drive).arg(std::max(.08,1.0-drive/36.0)).arg(std::max(.18,1.0-drive/45.0)*mix+(1.0-mix));}
            else if(e.type=="bitcrush"){const auto samples=std::clamp(48000.0/std::max(1000.0,v("rate",12000)),1.0,250.0);af<<QString("acrusher=bits=%1:samples=%2:mix=1:aa=.15").arg(v("bits",8)).arg(samples);}
            else if(e.type=="noisegate")af<<QString("agate=threshold=%1dB:ratio=10:release=%2").arg(v("threshold",-45)).arg(v("release",100));
            else if(e.type=="telephone"){const auto drive=std::clamp(v("drive",3),0.0,12.0);af<<QString("highpass=f=%1,lowpass=f=%2,volume=%3dB,asoftclip=type=tanh:threshold=.82:output=%4:oversample=2").arg(v("low",320)).arg(v("high",3200)).arg(drive).arg(std::max(.35,1.0-drive/20.0));}
            else if(e.type=="tremolo")af<<QString("tremolo=f=%1:d=%2").arg(v("rate",10)).arg(v("depth",.8));
            else if(e.type=="vibrato")af<<QString("vibrato=f=%1:d=%2").arg(v("rate",6)).arg(v("depth",.5));
            else if(e.type=="flanger")af<<QString("flanger=delay=%1:depth=%2:speed=%3").arg(v("delay",5)).arg(v("depth",3)).arg(v("speed",.5));
            else if(e.type=="bass_boost")af<<QString("bass=g=%1:f=%2").arg(v("gain",10)).arg(v("frequency",120));
            else if(e.type=="treble_boost")af<<QString("treble=g=%1:f=%2").arg(v("gain",10)).arg(v("frequency",6000));
            else if(e.type=="chorus"){const auto depth=std::clamp(v("depth",.55),0.0,1.0),speed=std::clamp(v("speed",1.2),.1,5.0),mix=std::clamp(v("mix",.7),0.0,1.0);af<<QString("chorus=0.6:%1:28|52:%2|%3:%4|%5:2|3.2").arg(.35+.45*mix).arg(.18+.3*depth).arg(.12+.24*depth).arg(speed).arg(speed*.63);}
            else if(e.type=="phaser")af<<QString("aphaser=in_gain=.45:out_gain=%1:delay=3:decay=%2:speed=%3:type=t").arg(std::clamp(v("depth",.8),0.0,1.0)).arg(std::clamp(v("decay",.55),0.0,.95)).arg(std::clamp(v("speed",.8),.1,2.0));
            else if(e.type=="stereo_widen"){const auto width=std::clamp(v("width",.7),0.0,1.0);af<<QString("aformat=channel_layouts=stereo,stereowiden=delay=%1:feedback=%2:crossfeed=%3:drymix=%4").arg(std::clamp(v("delay",18),1.0,100.0)).arg(std::clamp(v("feedback",.35),0.0,.9)).arg(.42*(1.0-width)).arg(.55+.45*width);}
            else if(e.type=="crystalizer")af<<QString("crystalizer=i=%1:c=%2").arg(std::clamp(v("intensity",4.5),-10.0,10.0)).arg(v("clipping",1)>=.5?1:0);
            else if(e.type=="ring_mod"){const auto frequency=std::clamp(v("frequency",70),5.0,2000.0),mix=std::clamp(v("mix",1),0.0,1.0);af<<QString("aeval='val(ch)*(%1+(%2)*sin(2*PI*%3*t))':c=same").arg(1.0-mix).arg(mix).arg(frequency);}
            else if(e.type=="frequency_shift")af<<QString("afreqshift=shift=%1:level=%2:order=%3").arg(v("shift",180)).arg(std::clamp(v("level",.85),0.0,1.0)).arg(std::clamp(static_cast<int>(v("order",12)),1,16));
            else if(e.type=="robotize")af<<QString("afftfilt=real='hypot(re,im)*sin(0)':imag='hypot(re,im)*cos(0)':win_size=%1:overlap=%2").arg(std::clamp(static_cast<int>(v("window",512)),32,4096)).arg(std::clamp(v("overlap",.75),0.0,.95));
            else if(e.type=="whisperize")af<<QString("afftfilt=real='hypot(re,im)*cos((random(0)*2-1)*2*PI)':imag='hypot(re,im)*sin((random(1)*2-1)*2*PI)':win_size=%1:overlap=%2").arg(std::clamp(static_cast<int>(v("window",128)),32,2048)).arg(std::clamp(v("overlap",.8),0.0,.95));
            else if(e.type=="virtual_bass")af<<QString("virtualbass=cutoff=%1:strength=%2").arg(v("cutoff",220)).arg(v("strength",2.2));
            else if(e.type=="haas_spread")af<<QString("aformat=channel_layouts=stereo,haas=left_delay=%1:right_delay=%2:side_gain=%3").arg(v("leftDelay",3)).arg(v("rightDelay",14)).arg(v("sideGain",1.35));
        }
    };
    const bool anySolo=std::any_of(sequence.tracks.begin(),sequence.tracks.end(),[](const auto&track){return track.kind==TrackKind::Audio&&track.solo;});
    const bool audioEnabled=audioTrack.kind==TrackKind::Audio&&!audioTrack.muted&&(!anySolo||audioTrack.solo);
    if(audioEnabled){addAudio(audio.audio,audio.effects);addAudio(audioTrack.audio,audioTrack.effects);addAudio(sequence.masterAudio,sequence.masterEffects);if(sequence.masterLimiter)af<<"alimiter=limit=0.891:latency=1";}
    const auto inputStart=audioEnabled?std::min(video.freezeFrame?video.freezeSourceTime:video.sourceRange.start(),audio.sourceRange.start()):(video.freezeFrame?video.freezeSourceTime:video.sourceRange.start());const auto videoInputEnd=video.freezeFrame?video.freezeSourceTime+Rational{1,20}:video.sourceRange.end();const auto inputEnd=audioEnabled?std::max(videoInputEnd,audio.sourceRange.end()):videoInputEnd;if(video.freezeFrame){const auto offset=static_cast<double>((video.freezeSourceTime-inputStart).asLongDouble());vf.prepend(QString("trim=start=%1:end=%2,setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=%3").arg(offset,0,'f',6).arg(offset+0.05,0,'f',6).arg(static_cast<double>(video.duration.asLongDouble()),0,'f',6));}else vf.prepend(QString("trim=start=%1:end=%2,setpts=PTS-STARTPTS").arg(static_cast<double>((video.sourceRange.start()-inputStart).asLongDouble()),0,'f',6).arg(static_cast<double>((video.sourceRange.end()-inputStart).asLongDouble()),0,'f',6));if(audioEnabled)af.prepend(QString("atrim=start=%1:end=%2,asetpts=PTS-STARTPTS").arg(static_cast<double>((audio.sourceRange.start()-inputStart).asLongDouble()),0,'f',6).arg(static_cast<double>((audio.sourceRange.end()-inputStart).asLongDouble()),0,'f',6));
    const auto output=playbackPreviewPath(QString::fromStdString(video.id));QFile::remove(output);const auto outputDuration=QString::number(static_cast<double>(video.duration.asLongDouble()),'f',6);const auto inputDuration=inputEnd-inputStart;QStringList args{"-hide_banner","-loglevel","error","-y","-ss",QString::number(static_cast<double>(inputStart.asLongDouble()),'f',6),"-t",QString::number(static_cast<double>(inputDuration.asLongDouble()),'f',6),"-i",mediaPath};if(!vf.isEmpty())args<<"-vf"<<vf.join(',');if(audioEnabled&&!af.isEmpty())args<<"-af"<<af.join(',');args<<"-t"<<outputDuration<<"-c:v"<<"libx264"<<"-preset"<<"ultrafast";if(audioEnabled)args<<"-c:a"<<"aac";else args<<"-an";args<<"-movflags"<<"+faststart"<<output;return runFfmpeg(args,errorMessage,-1);
}

} // namespace ytp
