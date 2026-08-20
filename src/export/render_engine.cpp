#include "export/render_engine.h"

#include "model/effects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <optional>

namespace ytp {
namespace {

QString number(const Rational& value) { return QString::number(static_cast<double>(value.asLongDouble()), 'f', 6); }
QString number(double value) { return QString::number(value, 'f', 6); }

QString temporalWeights(int frames,double decay){
    frames=std::clamp(frames,2,30);decay=std::clamp(decay,0.0,1.0);
    QStringList values;values.reserve(frames);
    for(int index=0;index<frames;++index)values<<number(std::pow(decay,index));
    return values.join(QLatin1Char(' '));
}

double parameter(const EffectInstance& effect, const char* name, double fallback) {
    if (const auto* value = findParameter(effect, name)) return value->value;
    return fallback;
}
QString animatedMaskValue(const MaskSettings& mask,std::string_view name,double fallback,Rational timelineStart){const auto found=std::find_if(mask.animation.begin(),mask.animation.end(),[&](const auto&p){return p.name==name;});if(found==mask.animation.end()||found->keyframes.empty())return number(fallback);auto keys=found->keyframes;std::sort(keys.begin(),keys.end(),[](const auto&a,const auto&b){return a.time<b.time;});QString expression=number(keys.back().value);for(std::size_t index=keys.size();index-->1;){const auto&a=keys[index-1];const auto&b=keys[index];const auto ta=timelineStart+a.time,tb=timelineStart+b.time;const auto segment=QString("%1+(%2-%1)*(T-%3)/(%4-%3)").arg(number(a.value),number(b.value),number(ta),number(tb));expression=QString("if(lt(T,%1),%2,%3)").arg(number(tb),segment,expression);}return QString("if(lt(T,%1),%2,%3)").arg(number(timelineStart+keys.front().time),number(keys.front().value),expression);}
QString animatedTransformValue(const TransformSettings& transform,std::string_view name,double fallback,Rational timelineStart){const auto found=std::find_if(transform.animation.begin(),transform.animation.end(),[&](const auto&p){return p.name==name;});if(found==transform.animation.end()||found->keyframes.empty())return number(fallback);auto keys=found->keyframes;std::sort(keys.begin(),keys.end(),[](const auto&a,const auto&b){return a.time<b.time;});QString expression=number(keys.back().value);for(std::size_t index=keys.size();index-->1;){const auto&a=keys[index-1];const auto&b=keys[index];const auto ta=timelineStart+a.time,tb=timelineStart+b.time;const auto segment=QString("%1+(%2-%1)*(t-%3)/(%4-%3)").arg(number(a.value),number(b.value),number(ta),number(tb));expression=QString("if(lt(t,%1),%2,%3)").arg(number(tb),segment,expression);}return QString("if(lt(t,%1),%2,%3)").arg(number(timelineStart+keys.front().time),number(keys.front().value),expression);}
QString drawtextEscape(QString value){value.replace('\\',QStringLiteral("\\\\"));value.replace(':',QStringLiteral("\\:"));value.replace('\'',QStringLiteral("\\'"));value.replace('%',QStringLiteral("\\%"));value.replace(',',QStringLiteral("\\,"));return value;}
QString rgbRemap(const QString& x,const QString& y){return QStringLiteral("format=gbrp,geq=r='r(%1,%2)':g='g(%1,%2)':b='b(%1,%2)'").arg(x,y);}

void appendVideoEffects(QStringList& filters, const std::vector<EffectInstance>& effects) {
    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        const auto p = [&](const char* name, double fallback) { return parameter(effect, name, fallback); };
        if (effect.type == "brightness_contrast") filters << QStringLiteral("eq=brightness=%1:contrast=%2").arg(number(p("brightness", 0)), number(p("contrast", 1)));
        else if (effect.type == "saturation") filters << QStringLiteral("eq=saturation=%1").arg(number(p("amount", 1)));
        else if (effect.type == "hue") filters << QStringLiteral("hue=h=%1").arg(number(p("degrees", 0)));
        else if (effect.type == "invert") filters << QStringLiteral("negate");
        else if (effect.type == "grayscale") filters << QStringLiteral("hue=s=0");
        else if (effect.type == "blur") filters << QStringLiteral("boxblur=%1").arg(number(std::max(1.0, p("radius", 5) / 3)));
        else if (effect.type == "sharpen") filters << QStringLiteral("unsharp=5:5:%1").arg(number(p("amount", 1)));
        else if (effect.type == "pixelate") {
            const auto block = std::max(2.0, p("block", 12));
            filters << QStringLiteral("pixelize=w=%1:h=%1:mode=avg").arg(std::clamp(static_cast<int>(block),2,100));
        } else if (effect.type == "posterize") filters << QStringLiteral("elbg=codebook_length=%1:nb_steps=1").arg(std::clamp(static_cast<int>(p("levels", 8)), 2, 64));
        else if (effect.type == "threshold") filters << QStringLiteral("lutyuv=y='if(gte(val,%1),255,0)':u=128:v=128").arg(number(p("level", .5) * 255));
        else if (effect.type == "tint") {
            const auto mix=std::clamp(p("mix",.5),0.0,1.0);
            const auto channel=[&](const char* name){return 1.0+(std::clamp(p(name,1),0.0,2.0)-1.0)*mix;};
            filters << QStringLiteral("colorchannelmixer=rr=%1:gg=%2:bb=%3")
                           .arg(number(channel("red")),number(channel("green")),number(channel("blue")));
        }
        else if(effect.type=="rgb_split")filters<<QStringLiteral("rgbashift=rh=%1:rv=%2:bh=%3:bv=%4:edge=wrap").arg(static_cast<int>(p("offset",12))).arg(static_cast<int>(p("vertical",0))).arg(-static_cast<int>(p("offset",12))).arg(-static_cast<int>(p("vertical",0)));
        else if(effect.type=="chromatic_aberration")filters<<QStringLiteral("chromashift=cbh=%1:crh=%2:edge=wrap").arg(static_cast<int>(p("offset",8))).arg(-static_cast<int>(p("offset",8)));
        else if(effect.type=="wave_warp")filters<<QStringLiteral("scroll=h=%1:v=%2").arg(number(p("amount",.08))).arg(number(p("speed",.15)));
        else if(effect.type=="lens_warp")filters<<QStringLiteral("lenscorrection=k1=%1:k2=%2:i=bilinear").arg(number(p("amount",.35))).arg(number(p("secondary",0)));
        else if(effect.type=="kaleidoscope")filters<<QStringLiteral("frei0r=filter_name=kaleid0sc0pe:filter_params='%1|.5|.5'").arg(number(std::clamp(p("strength",.7),0.0,1.0)));
        else if(effect.type=="edge_echo")filters<<QStringLiteral("edgedetect=mode=colormix:high=%1").arg(number(std::max(.01,p("strength",.4))));
        else if(effect.type=="recursive_trails"){const auto frames=std::clamp(static_cast<int>(p("frames",6)),2,30);filters<<QStringLiteral("tmix=frames=%1:weights='%2'").arg(frames).arg(temporalWeights(frames,p("decay",.8)));}
        else if(effect.type=="time_smear")filters<<QStringLiteral("tmix=frames=%1").arg(std::clamp(static_cast<int>(p("frames",8)),2,30));
        else if(effect.type=="frame_blend")filters<<QStringLiteral("tblend=all_mode=average");
        else if(effect.type=="screen_shake"){const int amount=std::max(0,static_cast<int>(p("amount",12)));filters<<QStringLiteral("crop=iw-%1:ih-%1*ih/iw:x='%2+%2*sin(n*%3)':y='%2*ih/iw+%2*ih/iw*cos(n*%3)'").arg(amount*2).arg(amount).arg(number(p("speed",18)/10));}
        else if(effect.type=="chroma_key")filters<<QStringLiteral("chromakey=0x00FF00:%1:%2").arg(number(p("similarity",.15))).arg(number(p("blend",.05)));
        else if(effect.type=="datamosh")filters<<QStringLiteral("tblend=all_mode=difference,lagfun=decay=%1").arg(number(p("decay",.9)));
        else if(effect.type=="scanlines")filters<<QStringLiteral("drawgrid=w=iw:h=%1:t=1:c=black@%2").arg(std::max(2,static_cast<int>(p("spacing",4)))).arg(number(p("opacity",.28)));
        else if(effect.type=="vhs_noise")filters<<QStringLiteral("noise=alls=%1:allf=t+u,chromashift=cbh=%2:crh=%3:edge=wrap").arg(number(p("strength",18))).arg(static_cast<int>(p("chroma",5))).arg(-static_cast<int>(p("chroma",5)));
        else if(effect.type=="solarize"){const auto threshold=number(std::clamp(p("threshold",.5),0.0,1.0)*255.0);filters<<QStringLiteral("lutrgb=r='if(gt(val,%1),255-val,val)':g='if(gt(val,%1),255-val,val)':b='if(gt(val,%1),255-val,val)'").arg(threshold);}
        else if(effect.type=="emboss")filters<<QStringLiteral("format=gbrp,convolution='-2 -1 0 -1 1 1 0 1 2:-2 -1 0 -1 1 1 0 1 2:-2 -1 0 -1 1 1 0 1 2:0 0 0 0 1 0 0 0 0',eq=contrast=%1").arg(number(1.0+std::max(0.0,p("amount",1))*.35));
        else if(effect.type=="neon_edges")filters<<QStringLiteral("edgedetect=mode=colormix:high=%1,eq=saturation=%2:contrast=1.25").arg(number(std::max(.01,p("strength",.35)))).arg(number(p("saturation",2.4)));
        else if(effect.type=="vignette")filters<<QStringLiteral("vignette=angle=%1").arg(number(std::clamp(p("strength",.65),.05,1.0)));
        else if(effect.type=="color_cycle")filters<<QStringLiteral("hue=H='2*PI*t*%1'").arg(number(p("speed",.6)));
        else if(effect.type=="strobe"){const auto rate=std::max(1.0,p("rate",8));const auto period=1.0/rate;filters<<QStringLiteral("drawbox=x=0:y=0:w=iw:h=ih:c=white@0.88:t=fill:enable='lt(mod(t,%1),%2)'").arg(number(period)).arg(number(period*std::clamp(p("duty",.45),.05,.95)));}
        else if(effect.type=="channel_swap"){const auto mix=std::clamp(p("mix",1),0.0,1.0);filters<<QStringLiteral("colorchannelmixer=rr=%1:rb=%2:gg=1:br=%2:bb=%1").arg(number(1.0-mix)).arg(number(mix));}
        else if(effect.type=="vertical_roll")filters<<QStringLiteral("scroll=v=%1").arg(number(p("speed",.18)));
        else if(effect.type=="bad_tv")filters<<QStringLiteral("noise=alls=%1:allf=t+u,drawgrid=w=iw:h=4:t=1:c=black@%2,scroll=v=%3").arg(number(p("noise",16))).arg(number(p("scanlines",.25))).arg(number(p("roll",.03)));
        else if(effect.type=="cartoon_edges")filters<<QStringLiteral("edgedetect=mode=colormix:high=%1,eq=saturation=%2:contrast=1.2").arg(number(std::max(.01,p("edge",.28)))).arg(number(p("saturation",2)));
        else if(effect.type=="impact_zoom"){const auto zoom=std::clamp(p("zoom",1.4),1.0,3.0),ox=std::clamp(p("offsetX",0),-1.0,1.0),oy=std::clamp(p("offsetY",0),-1.0,1.0);filters<<rgbRemap(QStringLiteral("W/2+(X-W/2)/%1+%2*W*(1-1/%1)/2").arg(number(zoom),number(ox)),QStringLiteral("H/2+(Y-H/2)/%1+%2*H*(1-1/%1)/2").arg(number(zoom),number(oy)));}
        else if(effect.type=="spin")filters<<QStringLiteral("rotate='%1*t':ow=iw:oh=ih:c=black").arg(number(p("speed",1.2)));
        else if(effect.type=="pendulum")filters<<QStringLiteral("rotate='%1*PI/180*sin(2*PI*t*%2)':ow=iw:oh=ih:c=black").arg(number(p("angle",14)),number(p("speed",1.6)));
        else if(effect.type=="perspective_tilt"){const auto amount=std::clamp(p("amount",.18),-.45,.45),vertical=std::clamp(p("vertical",.10),-.4,.4);filters<<QStringLiteral("perspective=x0='W*%1':y0='H*%2':x1='W*(1-%1)':y1='H*%3':x2=0:y2=H:x3=W:y3=H:sense=destination:eval=init").arg(number(std::max(0.0,amount)),number(std::max(0.0,vertical)),number(std::max(0.0,-vertical)));}
        else if(effect.type=="elastic_wave")filters<<QStringLiteral("format=gbrp,geq=r='r(mod(X+%1*sin(Y/%2+T*%3)+W,W),Y)':g='g(mod(X+%4*sin(Y/%2+T*%3*.87)+W,W),Y)':b='b(mod(X+%5*sin(Y/%2+T*%3*1.13)+W,W),Y)'").arg(number(p("amount",24)),number(std::max(4.0,p("spacing",18))),number(p("speed",8)),number(p("amount",24)*.72),number(p("amount",24)*1.18));
        else if(effect.type=="glitch_bands")filters<<QStringLiteral("format=gbrp,geq=r='r(mod(X+%1*gt(mod(Y+floor(T*%3),%2),%2*.66)+W,W),Y)':g='g(X,Y)':b='b(mod(X-%4*gt(mod(Y+floor(T*%3*.79),%2*1.16),%2*.78)+W,W),Y)'").arg(number(p("amount",42)),number(std::max(8.0,p("band",48))),number(p("speed",120)),number(p("amount",42)*.8));
        else if(effect.type=="thermal")filters<<QStringLiteral("pseudocolor=preset=%1").arg(std::clamp(static_cast<int>(p("palette",7)),0,20));
        else if(effect.type=="motion_burn")filters<<QStringLiteral("lagfun=decay=%1").arg(number(std::clamp(p("decay",.96),.5,.999)));
        else if(effect.type=="block_shuffle")filters<<QStringLiteral("shufflepixels=mode=block:w=%1:h=%2:seed=%3").arg(std::clamp(static_cast<int>(p("width",32)),2,256)).arg(std::clamp(static_cast<int>(p("height",24)),2,256)).arg(std::clamp(static_cast<int>(p("seed",13)),0,65535));
        else if(effect.type=="shear")filters<<QStringLiteral("shear=shx=%1:shy=%2:fillcolor=black:interp=bilinear").arg(number(std::clamp(p("horizontal",.35),-2.0,2.0)),number(std::clamp(p("vertical",-.12),-2.0,2.0)));
        else if(effect.type=="fisheye"){const auto strength=number(p("strength",.72));const auto scale=QStringLiteral("(1+%1*pow(hypot(X-W/2,Y-H/2)/hypot(W/2,H/2),2))").arg(strength);filters<<rgbRemap(QStringLiteral("W/2+(X-W/2)*%1").arg(scale),QStringLiteral("H/2+(Y-H/2)*%1").arg(scale));}
        else if(effect.type=="tiny_planet"){const auto angle=QStringLiteral("%1*sin(T*%2)*(1-min(1,hypot(X-W/2,Y-H/2)/(min(W,H)*%3)))").arg(number(p("strength",2.4)),number(p("speed",.35)),number(p("radius",.75)));filters<<rgbRemap(QStringLiteral("W/2+(X-W/2)*cos(%1)-(Y-H/2)*sin(%1)").arg(angle),QStringLiteral("H/2+(X-W/2)*sin(%1)+(Y-H/2)*cos(%1)").arg(angle));}
        else if(effect.type=="oscilloscope")filters<<QStringLiteral("oscilloscope=x=.5:y=.5:s=%1:t=%2:o=%3:tx=.5:ty=.86:tw=.82:th=.24:c=7:g=1:st=1:sc=1").arg(number(p("size",.72)),number(p("tilt",.22)),number(p("opacity",.9)));
        else if(effect.type=="frame_randomizer")filters<<QStringLiteral("random=frames=%1:seed=%2").arg(std::clamp(static_cast<int>(p("frames",8)),2,60)).arg(std::clamp(static_cast<int>(p("seed",23)),0,65535));
        else if(effect.type=="motion_amplify")filters<<QStringLiteral("amplify=radius=%1:factor=%2:threshold=%3:tolerance=1").arg(std::clamp(static_cast<int>(p("radius",2)),1,12)).arg(number(p("factor",7)),number(p("threshold",3)));
        else if(effect.type=="frame_xor")filters<<QStringLiteral("tblend=all_mode=xor:all_opacity=%1").arg(number(std::clamp(p("opacity",1),.05,1.0)));
        else if(effect.type=="pixel_bloom")filters<<QStringLiteral("pixelize=w=%1:h=%2:mode=%3").arg(std::clamp(static_cast<int>(p("width",24)),2,128)).arg(std::clamp(static_cast<int>(p("height",18)),2,128)).arg(std::clamp(static_cast<int>(p("mode",2)),0,2));
        else if(effect.type=="xray_edges")filters<<QStringLiteral("format=gbrp,kirsch=scale=%1,negate,pseudocolor=preset=%2").arg(number(p("strength",2))).arg(std::clamp(static_cast<int>(p("palette",7)),0,20));
        else if(effect.type=="horizontal_shuffle")filters<<QStringLiteral("shufflepixels=mode=horizontal:w=%1:h=%1:seed=%2").arg(std::clamp(static_cast<int>(p("strip",14)),2,256)).arg(std::clamp(static_cast<int>(p("seed",41)),0,65535));
        else if(effect.type=="vertical_shuffle")filters<<QStringLiteral("shufflepixels=mode=vertical:w=%1:h=%1:seed=%2").arg(std::clamp(static_cast<int>(p("strip",14)),2,256)).arg(std::clamp(static_cast<int>(p("seed",73)),0,65535));
        else if(effect.type=="temporal_mosaic"){const auto grid=std::clamp(static_cast<int>(p("grid",3)),2,5),frames=grid*grid;filters<<QStringLiteral("tile=layout=%1x%1:nb_frames=%2:overlap=%3:init_padding=%3:padding=%4").arg(grid).arg(frames).arg(frames-1).arg(std::clamp(static_cast<int>(p("border",1)),0,12));}
        else if(effect.type=="dilation_bloom")for(int i=0;i<std::clamp(static_cast<int>(p("iterations",2)),1,8);++i)filters<<QStringLiteral("dilation");
        else if(effect.type=="erosion_decay")for(int i=0;i<std::clamp(static_cast<int>(p("iterations",2)),1,8);++i)filters<<QStringLiteral("erosion");
        else if(effect.type=="field_corruption")filters<<QStringLiteral("il=luma_mode=interleave:chroma_mode=interleave:luma_swap=%1:chroma_swap=%2").arg(p("swapLuma",1)>=.5?1:0).arg(p("swapChroma",1)>=.5?1:0);
        else if(effect.type=="chroma_planes"){const auto mode=std::clamp(static_cast<int>(p("mode",1)),0,2);filters<<(mode==0?QStringLiteral("shuffleplanes=map0=0:map1=1:map2=2"):mode==1?QStringLiteral("shuffleplanes=map0=0:map1=2:map2=1"):QStringLiteral("shuffleplanes=map0=2:map1=1:map2=0"));}
        else if(effect.type=="frame_skip")filters<<QStringLiteral("framestep=%1,fps=30").arg(std::clamp(static_cast<int>(p("step",5)),2,20));
        else if(effect.type=="funhouse"){const auto amount=number(p("amount",28)),spacing=number(std::max(4.0,p("spacing",20))),speed=number(p("speed",7));filters<<rgbRemap(QStringLiteral("mod(X+%1*sin(Y/%2+T*%3)+W,W)").arg(amount,spacing,speed),QStringLiteral("mod(Y+%1*cos(X/%2-T*%3)+H,H)").arg(amount,spacing,speed));}
        else if(effect.type=="vortex"){const auto angle=QStringLiteral("%1*sin(T*%2)*exp(-hypot(X-W/2,Y-H/2)/(min(W,H)*%3))").arg(number(p("strength",1.7)),number(p("speed",2)),number(p("radius",.45)));filters<<rgbRemap(QStringLiteral("W/2+(X-W/2)*cos(%1)-(Y-H/2)*sin(%1)").arg(angle),QStringLiteral("H/2+(X-W/2)*sin(%1)+(Y-H/2)*cos(%1)").arg(angle));}
        else if(effect.type=="radial_ripple"){const auto scale=QStringLiteral("(1+%1*sin(hypot(X-W/2,Y-H/2)/%2-T*%3))").arg(number(p("amount",.18)),number(std::max(3.0,p("spacing",14))),number(p("speed",8)));filters<<rgbRemap(QStringLiteral("W/2+(X-W/2)*%1").arg(scale),QStringLiteral("H/2+(Y-H/2)*%1").arg(scale));}
        else if(effect.type=="melt"){const auto shift=QStringLiteral("%1*(.5+.5*sin(X/%2+T*%3))").arg(number(p("amount",34)),number(std::max(4.0,p("spacing",18))),number(p("speed",5)));filters<<rgbRemap(QStringLiteral("X"),QStringLiteral("mod(Y+%1+H,H)").arg(shift));}
        else if(effect.type=="halftone")filters<<QStringLiteral("format=gray,geq=lum='if(gt(lum(X,Y),128+%1*sin(X/%2)*sin(Y/%2)),255,0)'").arg(number(p("contrast",55)),number(std::max(1.0,p("cell",3))));
        else if(effect.type=="temporal_heat")filters<<QStringLiteral("tblend=all_mode=heat:all_opacity=%1").arg(number(std::clamp(p("opacity",.8),.05,1.0)));
        else if(effect.type=="temporal_stain")filters<<QStringLiteral("tblend=all_mode=stain:all_opacity=%1").arg(number(std::clamp(p("opacity",.8),.05,1.0)));
        else if(effect.type=="video_feedback"){
            const auto zoom=std::clamp(p("zoom",.72),0.0,1.0);
            const auto frames=std::clamp(static_cast<int>(std::lround(3.0+zoom*9.0)),3,12);
            const auto scale=1.025+zoom*.055;
            const auto drift=std::max(1.0,zoom*5.0);
            filters<<QStringLiteral("tmix=frames=%1:weights='%2'").arg(frames).arg(temporalWeights(frames,.68+.25*zoom));
            filters<<QStringLiteral("scale=ceil(iw*%1/2)*2:ceil(ih*%1/2)*2:flags=fast_bilinear").arg(number(scale));
            filters<<QStringLiteral("crop=trunc(iw/%1/2)*2:trunc(ih/%1/2)*2:x='(in_w-out_w)/2+%2*sin(n*%3)':y='(in_h-out_h)/2+%2*cos(n*%3)'")
                         .arg(number(scale),number(drift),number(std::max(.01,p("phase",.18))));
        }
        else if(effect.type=="pixel_sort")filters<<QStringLiteral("frei0r=filter_name=pixels0rt");
        else if(effect.type=="water_surface")filters<<QStringLiteral("frei0r=filter_name=water:filter_params='%1|%2'").arg(number(p("amplitude",.5)),number(p("frequency",.5)));
        else if(effect.type=="elastic_scale")filters<<QStringLiteral("frei0r=filter_name=elastic_scale:filter_params='%1|%2|%3'").arg(number(p("centerX",.5)),number(p("centerY",.5)),number(p("strength",.65)));
        else if(effect.type=="analog_nosync")filters<<QStringLiteral("frei0r=filter_name=nosync0r:filter_params='%1'").arg(number(p("strength",.6)));
        else if(effect.type=="film_gate_weave")filters<<QStringLiteral("frei0r=filter_name=gateweave:filter_params='%1|%2|%3'").arg(number(p("interval",.35)),number(p("horizontal",.5)),number(p("vertical",.35)));
        else if(effect.type=="nervous_frames")filters<<QStringLiteral("frei0r=filter_name=nervous:filter_params='%1'").arg(number(p("strength",.65)));
        else if(effect.type=="light_graffiti")filters<<QStringLiteral("frei0r=filter_name=lightgraffiti:filter_params='%1|%2'").arg(number(p("sensitivity",.55)),number(p("decay",.5)));
        else if(effect.type=="digital_glitch")filters<<QStringLiteral("frei0r=filter_name=glitch0r:filter_params='%1|%2|%3|%4'").arg(number(p("frequency",.55)),number(p("blockHeight",.28)),number(p("shift",.8)),number(p("color",.65)));
        else if(effect.type=="ordered_dither")filters<<QStringLiteral("frei0r=filter_name=dither:filter_params='%1|%2'").arg(number(p("levels",.5)),number(p("pattern",.6)));
        else if(effect.type=="cmyk_halftone")filters<<QStringLiteral("frei0r=filter_name=colorhalftone:filter_params='%1|.15|%2|.75'").arg(number(p("dotSize",.45)),number(p("angle",.45)));
        else if(effect.type=="clone_grid")filters<<QStringLiteral("frei0r=filter_name=cairoimagegrid:filter_params='%1|%2'").arg(number(p("columns",.35)),number(p("rows",.35)));
        else if(effect.type=="edge_glow_native")filters<<QStringLiteral("edgedetect=mode=colormix:high=%1,gblur=sigma=%2").arg(number(std::max(.01,1.0-std::clamp(p("strength",.7),0.0,1.0)*.8)),number(.3+std::clamp(p("strength",.7),0.0,1.0)*1.2));
        else if(effect.type=="soft_glow_native")filters<<QStringLiteral("frei0r=filter_name=softglow:filter_params='%1|%2|%3'").arg(number(p("blur",.6)),number(p("brightness",.75)),number(p("sharpness",.45)));
        else if(effect.type=="ink_cartoon")filters<<QStringLiteral("frei0r=filter_name=cartoon:filter_params='%1|%2'").arg(number(p("triplevel",.55)),number(p("edgeSpace",.4)));
        else if(effect.type=="film_projector")filters<<QStringLiteral("frei0r=filter_name=filmgrain:filter_params='%1|.2|.2|.2|.1|%2|%3'").arg(number(p("grain",.35)),number(p("dust",.25)),number(p("flicker",.3)));
        else if(effect.type=="frame_delay")filters<<QStringLiteral("frei0r=filter_name=delay0r:filter_params='%1'").arg(number(p("delay",.55)));
    }
}

void appendAudioEffects(QStringList& filters, const std::vector<EffectInstance>& effects) {
    for (const auto& effect : effects) {
        if (!effect.enabled) continue;
        const auto p = [&](const char* name, double fallback) { return parameter(effect, name, fallback); };
        if (effect.type == "eq") filters << QStringLiteral("equalizer=f=100:t=q:w=1:g=%1,equalizer=f=1000:t=q:w=1:g=%2,equalizer=f=8000:t=q:w=1:g=%3").arg(number(p("lowDb",0)),number(p("midDb",0)),number(p("highDb",0)));
        else if (effect.type == "highpass") filters << QStringLiteral("highpass=f=%1").arg(number(p("frequency", 120)));
        else if (effect.type == "lowpass") filters << QStringLiteral("lowpass=f=%1").arg(number(p("frequency", 8000)));
        else if (effect.type == "compressor") filters << QStringLiteral("acompressor=threshold=%1dB:ratio=%2:attack=%3:release=%4").arg(number(p("threshold", -18)), number(p("ratio", 4)), number(p("attack", 20)), number(p("release", 250)));
        else if (effect.type == "limiter") filters << QStringLiteral("alimiter=limit=%1:release=%2:latency=1").arg(number(std::pow(10.0, p("ceiling", -1) / 20.0)),number(p("release",50)));
        else if (effect.type == "normalize") filters << QStringLiteral("loudnorm=I=%1:TP=-1.5:LRA=11").arg(number(p("target", -14)));
        else if (effect.type == "reverb") {const auto room=std::clamp(p("room",.5),0.0,1.0),damping=std::clamp(p("damping",.5),0.0,1.0),mix=std::clamp(p("mix",.25),0.0,1.0);filters << QStringLiteral("aecho=0.8:%1:%2:%3").arg(number(.12+mix*.78),number(45+room*955),number(.15+(1.0-damping)*.75));}
        else if (effect.type == "delay") {const auto mix=std::clamp(p("mix",.3),0.0,1.0);filters << QStringLiteral("aecho=0.8:%1:%2:%3").arg(number(.1+mix*.85),number(p("time",250)),number(std::clamp(p("feedback",.35),0.0,.95)));}
        else if (effect.type == "distortion") {const auto drive=std::clamp(p("drive",6),0.0,30.0),mix=std::clamp(p("mix",1),0.0,1.0);filters << QStringLiteral("volume=%1dB,asoftclip=type=tanh:threshold=%2:output=%3:oversample=2").arg(number(drive),number(std::max(.08,1.0-drive/36.0)),number(std::max(.18,1.0-drive/45.0)*mix+(1.0-mix)));}
        else if (effect.type == "bitcrush") {const auto samples=std::clamp(48000.0/std::max(1000.0,p("rate",12000)),1.0,250.0);filters << QStringLiteral("acrusher=bits=%1:samples=%2:mix=1:aa=0.15").arg(number(p("bits",8)),number(samples));}
        else if (effect.type == "noisegate") filters << QStringLiteral("agate=threshold=%1dB:ratio=10:release=%2").arg(number(p("threshold", -45)),number(p("release",100)));
        else if(effect.type=="telephone"){const auto drive=std::clamp(p("drive",3),0.0,12.0);filters<<QStringLiteral("highpass=f=%1,lowpass=f=%2,volume=%3dB,asoftclip=type=tanh:threshold=.82:output=%4:oversample=2").arg(number(p("low",320)),number(p("high",3200)),number(drive),number(std::max(.35,1.0-drive/20.0)));}
        else if(effect.type=="tremolo")filters<<QStringLiteral("tremolo=f=%1:d=%2").arg(number(p("rate",10)),number(p("depth",.8)));
        else if(effect.type=="vibrato")filters<<QStringLiteral("vibrato=f=%1:d=%2").arg(number(p("rate",6)),number(p("depth",.5)));
        else if(effect.type=="flanger")filters<<QStringLiteral("flanger=delay=%1:depth=%2:speed=%3").arg(number(p("delay",5)),number(p("depth",3)),number(p("speed",.5)));
        else if(effect.type=="bass_boost")filters<<QStringLiteral("bass=g=%1:f=%2").arg(number(p("gain",10)),number(p("frequency",120)));
        else if(effect.type=="treble_boost")filters<<QStringLiteral("treble=g=%1:f=%2").arg(number(p("gain",10)),number(p("frequency",6000)));
        else if(effect.type=="chorus"){const auto depth=std::clamp(p("depth",.55),0.0,1.0),speed=std::clamp(p("speed",1.2),.1,5.0),mix=std::clamp(p("mix",.7),0.0,1.0);filters<<QStringLiteral("chorus=0.6:%1:28|52:%2|%3:%4|%5:2|3.2").arg(number(.35+.45*mix),number(.18+.3*depth),number(.12+.24*depth),number(speed),number(speed*.63));}
        else if(effect.type=="phaser")filters<<QStringLiteral("aphaser=in_gain=.45:out_gain=%1:delay=3:decay=%2:speed=%3:type=t").arg(number(std::clamp(p("depth",.8),0.0,1.0)),number(std::clamp(p("decay",.55),0.0,.95)),number(std::clamp(p("speed",.8),.1,2.0)));
        else if(effect.type=="stereo_widen"){const auto width=std::clamp(p("width",.7),0.0,1.0);filters<<QStringLiteral("aformat=channel_layouts=stereo,stereowiden=delay=%1:feedback=%2:crossfeed=%3:drymix=%4").arg(number(std::clamp(p("delay",18),1.0,100.0)),number(std::clamp(p("feedback",.35),0.0,.9)),number(.42*(1.0-width)),number(.55+.45*width));}
        else if(effect.type=="crystalizer")filters<<QStringLiteral("crystalizer=i=%1:c=%2").arg(number(std::clamp(p("intensity",4.5),-10.0,10.0))).arg(p("clipping",1)>=.5?1:0);
        else if(effect.type=="ring_mod"){const auto frequency=std::clamp(p("frequency",70),5.0,2000.0),mix=std::clamp(p("mix",1),0.0,1.0);filters<<QStringLiteral("tremolo=f=%1:d=%2").arg(number(frequency),number(mix));}
        else if(effect.type=="frequency_shift")filters<<QStringLiteral("afreqshift=shift=%1:level=%2:order=%3").arg(number(p("shift",180)),number(std::clamp(p("level",.85),0.0,1.0))).arg(std::clamp(static_cast<int>(p("order",12)),1,16));
        else if(effect.type=="robotize")filters<<QStringLiteral("afftfilt=real='hypot(re,im)*sin(0)':imag='hypot(re,im)*cos(0)':win_size=%1:overlap=%2").arg(std::clamp(static_cast<int>(p("window",512)),32,4096)).arg(number(std::clamp(p("overlap",.75),0.0,.95)));
        else if(effect.type=="whisperize")filters<<QStringLiteral("afftfilt=real='hypot(re,im)*cos((random(0)*2-1)*2*PI)':imag='hypot(re,im)*sin((random(1)*2-1)*2*PI)':win_size=%1:overlap=%2").arg(std::clamp(static_cast<int>(p("window",128)),32,2048)).arg(number(std::clamp(p("overlap",.8),0.0,.95)));
        else if(effect.type=="virtual_bass")filters<<QStringLiteral("virtualbass=cutoff=%1:strength=%2").arg(number(p("cutoff",220)),number(p("strength",2.2)));
        else if(effect.type=="haas_spread")filters<<QStringLiteral("aformat=channel_layouts=stereo,haas=left_delay=%1:right_delay=%2:side_gain=%3").arg(number(p("leftDelay",3)),number(p("rightDelay",14)),number(p("sideGain",1.35)));
    }
}

void appendAtempo(QStringList& filters, double speed) {
    while (speed > 2.0) { filters << QStringLiteral("atempo=2"); speed /= 2.0; }
    while (speed < 0.5) { filters << QStringLiteral("atempo=0.5"); speed /= 0.5; }
    filters << QStringLiteral("atempo=%1").arg(number(speed));
}

struct Graph final { QStringList inputs; QString filter; QString videoLabel; QString audioLabel; };

Sequence flattenSequence(const Project& project,const Sequence& root){
    Sequence output=root;for(auto&t:output.tracks)t.items.clear();
    const auto speedRatio=[](double speed){return Rational{static_cast<std::int64_t>(std::llround(speed*1'000'000.0)),1'000'000};};
    const auto sourceSlice=[&](const TimelineItem&item,Rational offset,Rational duration){const auto ratio=speedRatio(item.speed);const auto sourceOffset=offset*ratio;const auto sourceDuration=duration*ratio;return item.reverse?TimeRange{item.sourceRange.end()-sourceOffset-sourceDuration,sourceDuration}:TimeRange{item.sourceRange.start()+sourceOffset,sourceDuration};};
    const auto targetTrack=[&](const Sequence&source,const Track&track)->Track*{if(source.id==root.id)return output.findTrack(track.id);std::size_t ordinal=0;for(const auto&candidate:source.tracks){if(candidate.kind!=track.kind)continue;if(candidate.id==track.id)break;++ordinal;}std::size_t current=0;for(auto&candidate:output.tracks)if(candidate.kind==track.kind){if(current++==ordinal)return &candidate;}for(auto&candidate:output.tracks)if(candidate.kind==track.kind)return &candidate;return nullptr;};
    std::function<void(const Sequence&,TimeRange,Rational,Rational,bool,std::vector<EffectInstance>)> add;
    add=[&](const Sequence&source,TimeRange window,Rational outputOffset,Rational parentSpeed,bool parentReverse,std::vector<EffectInstance> inherited){
        const bool anySolo=std::any_of(source.tracks.begin(),source.tracks.end(),[](const auto&track){return track.kind==TrackKind::Audio&&track.solo;});
        for(const auto&track:source.tracks){if((track.kind==TrackKind::Video&&!track.visible)||(track.kind==TrackKind::Audio&&(track.muted||(anySolo&&!track.solo))))continue;for(const auto&item:track.items){const auto intersectionStart=std::max(item.timelineStart,window.start());const auto intersectionEnd=std::min(item.timelineEnd(),window.end());if(intersectionEnd<=intersectionStart)continue;const auto localOffset=intersectionStart-item.timelineStart;const auto localDuration=intersectionEnd-intersectionStart;const auto mappedStart=parentReverse?outputOffset+(window.end()-intersectionEnd)/parentSpeed:outputOffset+(intersectionStart-window.start())/parentSpeed;const auto range=sourceSlice(item,localOffset,localDuration);
            if(!item.nestedSequenceId.empty()){if(const auto*child=project.findSequence(item.nestedSequenceId)){auto nestedEffects=inherited;nestedEffects.insert(nestedEffects.end(),item.effects.begin(),item.effects.end());if(source.id!=root.id){nestedEffects.insert(nestedEffects.end(),track.effects.begin(),track.effects.end());nestedEffects.insert(nestedEffects.end(),source.masterEffects.begin(),source.masterEffects.end());}add(*child,range,mappedStart,parentSpeed*speedRatio(item.speed),parentReverse!=item.reverse,std::move(nestedEffects));}continue;}
            auto*target=targetTrack(source,track);if(!target)continue;auto copy=item;copy.timelineStart=mappedStart;copy.duration=localDuration/parentSpeed;copy.sourceRange=range;copy.speed=static_cast<double>((speedRatio(item.speed)*parentSpeed).asLongDouble());copy.reverse=item.reverse!=parentReverse;const auto fadeInPart=localOffset<item.fadeIn?std::min(localDuration,item.fadeIn-localOffset):Rational{};const auto segmentEnd=localOffset+localDuration;const auto fadeOutStart=item.duration-item.fadeOut;const auto fadeOutPart=segmentEnd>fadeOutStart?std::min(localDuration,segmentEnd-std::max(localOffset,fadeOutStart)):Rational{};copy.fadeIn=(parentReverse?fadeOutPart:fadeInPart)/parentSpeed;copy.fadeOut=(parentReverse?fadeInPart:fadeOutPart)/parentSpeed;copy.effects.insert(copy.effects.end(),inherited.begin(),inherited.end());if(source.id!=root.id){copy.effects.insert(copy.effects.end(),track.effects.begin(),track.effects.end());copy.effects.insert(copy.effects.end(),source.masterEffects.begin(),source.masterEffects.end());if(track.kind==TrackKind::Audio){copy.audio.gainDb+=track.audio.gainDb+source.masterAudio.gainDb;copy.audio.pan=std::clamp(copy.audio.pan+track.audio.pan+source.masterAudio.pan,-1.0,1.0);}}copy.trackId=target->id;target->items.push_back(std::move(copy));
        }}
    };add(root,TimeRange{Rational{},root.duration()},Rational{},Rational{1,1},false,{});return output;
}

void cropParameter(EffectParameter& parameter, Rational offset, Rational duration) {
    if (parameter.keyframes.empty()) return;
    const auto end = offset + duration;
    std::vector<Keyframe> cropped;
    cropped.push_back(Keyframe{.id=createId(), .time=Rational{},
                               .value=evaluateParameter(parameter, offset),
                               .interpolation=parameter.keyframes.front().interpolation});
    for (const auto& key : parameter.keyframes)
        if (key.time > offset && key.time < end) {
            auto shifted = key;
            shifted.time = key.time - offset;
            cropped.push_back(std::move(shifted));
        }
    if (duration > Rational{})
        cropped.push_back(Keyframe{.id=createId(), .time=duration,
                                   .value=evaluateParameter(parameter, end),
                                   .interpolation=parameter.keyframes.back().interpolation});
    parameter.value = cropped.front().value;
    parameter.keyframes = std::move(cropped);
}

Sequence cropSequence(const Sequence& source, const TimeRange& window) {
    auto output = source;
    output.markers.clear();
    const auto speedRatio=[](double speed){return Rational{static_cast<std::int64_t>(std::llround(speed*1'000'000.0)),1'000'000};};
    for (auto& track : output.tracks) {
        std::vector<TimelineItem> croppedItems;
        for (const auto& original : track.items) {
            const auto intersectionStart = std::max(original.timelineStart, window.start());
            const auto intersectionEnd = std::min(original.timelineEnd(), window.end());
            if (intersectionEnd <= intersectionStart) continue;
            const auto localOffset = intersectionStart - original.timelineStart;
            const auto localDuration = intersectionEnd - intersectionStart;
            auto item = original;
            item.timelineStart = intersectionStart - window.start();
            item.duration = localDuration;
            if (!item.freezeFrame && !item.adjustmentClip) {
                const auto ratio = speedRatio(item.speed);
                const auto sourceOffset = localOffset * ratio;
                const auto sourceDuration = localDuration * ratio;
                item.sourceRange = item.reverse
                    ? TimeRange{original.sourceRange.end() - sourceOffset - sourceDuration, sourceDuration}
                    : TimeRange{original.sourceRange.start() + sourceOffset, sourceDuration};
            }
            item.fadeIn = localOffset < original.fadeIn
                ? std::min(localDuration, original.fadeIn - localOffset) : Rational{};
            const auto originalSliceEnd = localOffset + localDuration;
            const auto fadeOutStart = original.duration - original.fadeOut;
            item.fadeOut = originalSliceEnd > fadeOutStart
                ? std::min(localDuration, originalSliceEnd - std::max(localOffset, fadeOutStart)) : Rational{};
            for (auto& parameter : item.transform.animation) cropParameter(parameter, localOffset, localDuration);
            for (auto& effect : item.effects)
                for (auto& parameter : effect.parameters) cropParameter(parameter, localOffset, localDuration);
            for (auto& mask : item.masks)
                for (auto& parameter : mask.animation) cropParameter(parameter, localOffset, localDuration);
            croppedItems.push_back(std::move(item));
        }
        track.items = std::move(croppedItems);
    }
    return output;
}

Graph buildGraph(const Project& project, const Sequence& sequence, const ExportPreset& preset,
                 const std::optional<TimeRange>& window = std::nullopt,
                 bool optimizeForPreview = false) {
    auto renderSequence=flattenSequence(project,sequence);
    if (window) renderSequence=cropSequence(renderSequence,*window);
    Graph graph;
    const auto duration = window ? window->duration() : renderSequence.duration();
    graph.inputs << "-f" << "lavfi" << "-t" << number(duration) << "-i"
                 << QStringLiteral("color=c=black:s=%1x%2:r=%3/%4").arg(preset.audioOnly ? 16 : preset.width).arg(preset.audioOnly ? 16 : preset.height).arg(project.settings().frameRateNumerator).arg(project.settings().frameRateDenominator)
                 << "-f" << "lavfi" << "-t" << number(duration) << "-i"
                 << QStringLiteral("anullsrc=r=%1:cl=stereo").arg(preset.audioSampleRate);
    QStringList filters;
    filters << (preset.audioOnly ? QStringLiteral("[0:v]nullsink") : QStringLiteral("[0:v]setpts=PTS-STARTPTS[vbase0]"));
    filters << QStringLiteral("[1:a]asetpts=PTS-STARTPTS[abase]");
    int inputIndex = 2;
    struct ReusableInput final { int index{-1}; bool videoUsed{false}; bool audioUsed{false}; };
    QHash<QString,ReusableInput> reusableInputs;
    int videoIndex = 0;
    int audioIndex = 0;
    QString currentVideo = QStringLiteral("vbase0");
    QStringList audioLabels{QStringLiteral("[abase]")};
    const bool anySolo = std::any_of(renderSequence.tracks.begin(), renderSequence.tracks.end(), [](const Track& track) { return track.kind == TrackKind::Audio && track.solo; });

    // The first video track is visually highest in the timeline, so composite
    // video tracks from bottom to top. Audio track order does not affect mixing.
    std::vector<const Track*> renderTracks;
    for(auto iterator=renderSequence.tracks.rbegin();iterator!=renderSequence.tracks.rend();++iterator)
        if(iterator->kind==TrackKind::Video)renderTracks.push_back(&*iterator);
    for(const auto&track:renderSequence.tracks)if(track.kind==TrackKind::Audio)renderTracks.push_back(&track);
    for (const auto* trackPointer : renderTracks) {
        const auto& track=*trackPointer;
        if (track.kind == TrackKind::Video && !track.visible) continue;
        if (track.kind == TrackKind::Video && preset.audioOnly) continue;
        if (track.kind == TrackKind::Audio && (track.muted || (anySolo && !track.solo))) continue;
        for (const auto& item : track.items) {
            const auto* media = project.findMediaAsset(item.mediaAssetId);
            if (!media || !QFileInfo::exists(QString::fromStdString(media->path))) continue;
            if (track.kind == TrackKind::Video && media->width <= 0) continue;
            if (track.kind == TrackKind::Audio && media->audioSampleRate <= 0) continue;
            // Seek and bound each input at the demuxer. Previously every item reopened its
            // media at timestamp zero and decoded forward until trim reached the source range;
            // a late clip could therefore spend seconds decoding frames that were discarded.
            const auto inputStart=item.freezeFrame&&track.kind==TrackKind::Video
                ? item.freezeSourceTime : item.sourceRange.start();
            const auto inputDuration=item.freezeFrame&&track.kind==TrackKind::Video
                ? Rational{1,20} : item.sourceRange.duration();
            const auto boundedDuration=std::max(inputDuration,Rational{1,1000});
            const auto inputKey=QStringLiteral("%1|%2|%3").arg(QString::fromStdString(media->id),number(inputStart),number(boundedDuration));
            const bool wantsVideo=track.kind==TrackKind::Video;
            int clipInputIndex=-1;
            auto reusable=reusableInputs.find(inputKey);
            if(reusable!=reusableInputs.end()&&(wantsVideo?!reusable->videoUsed:!reusable->audioUsed)){
                clipInputIndex=reusable->index;
                if(wantsVideo)reusable->videoUsed=true;else reusable->audioUsed=true;
            }else{
                clipInputIndex=inputIndex++;
                graph.inputs << "-ss" << number(inputStart)
                             << "-t" << number(boundedDuration)
                             << "-i" << QString::fromStdString(media->path);
                if(reusable==reusableInputs.end())
                    reusableInputs.insert(inputKey,ReusableInput{clipInputIndex,wantsVideo,!wantsVideo});
            }
            if (track.kind == TrackKind::Video && !preset.audioOnly) {
                QStringList chain;
                if (item.freezeFrame) {
                    chain << QStringLiteral("trim=duration=0.001");
                    chain << QStringLiteral("tpad=stop_mode=clone:stop_duration=%1").arg(number(item.duration));
                } else {
                    chain << QStringLiteral("trim=duration=%1").arg(number(item.sourceRange.duration()));
                    if (item.reverse) chain << QStringLiteral("reverse");
                    if (std::abs(item.speed - 1.0) > .000001) chain << QStringLiteral("setpts=PTS/%1").arg(number(item.speed));
                }
                if(item.fadeIn>Rational{})chain<<QStringLiteral("fade=t=in:st=0:d=%1:alpha=1").arg(number(item.fadeIn));
                if(item.fadeOut>Rational{})chain<<QStringLiteral("fade=t=out:st=%1:d=%2:alpha=1").arg(number(item.duration-item.fadeOut),number(item.fadeOut));
                chain << QStringLiteral("setpts=PTS-STARTPTS+%1/TB").arg(number(item.timelineStart));
                if (item.transform.cropLeft > 0 || item.transform.cropRight > 0 || item.transform.cropTop > 0 || item.transform.cropBottom > 0)
                    chain << QStringLiteral("crop=iw*%1:ih*%2:iw*%3:ih*%4").arg(number(1-item.transform.cropLeft-item.transform.cropRight), number(1-item.transform.cropTop-item.transform.cropBottom), number(item.transform.cropLeft), number(item.transform.cropTop));
                if (item.transform.flipHorizontal) chain << QStringLiteral("hflip");
                if (item.transform.flipVertical) chain << QStringLiteral("vflip");
                // Effects in Program preview should process the preview canvas, not every pixel
                // of a 1080p/4K source that will be thrown away by the final downscale.
                if(optimizeForPreview)
                    chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease:flags=fast_bilinear")
                                 .arg(preset.width).arg(preset.height);
                appendVideoEffects(chain, item.effects);
                appendVideoEffects(chain, track.effects);
                if(!optimizeForPreview||std::abs(item.transform.scaleX-1.0)>.000001||std::abs(item.transform.scaleY-1.0)>.000001)
                    chain << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease%3").arg(std::max(2, static_cast<int>(preset.width * item.transform.scaleX))).arg(std::max(2, static_cast<int>(preset.height * item.transform.scaleY))).arg(optimizeForPreview?QStringLiteral(":flags=fast_bilinear"):QString{});
                if (item.transform.rotation != 0) chain << QStringLiteral("rotate=%1*PI/180:ow=rotw(iw):oh=roth(ih):c=black@0").arg(number(item.transform.rotation));
                chain << QStringLiteral("format=rgba,colorchannelmixer=aa=%1").arg(number(item.transform.opacity));
                if(item.captionEnabled&&!item.captionText.empty())chain<<QStringLiteral("drawtext=text='%1':fontcolor=%2:fontsize=%3:borderw=4:bordercolor=black:x=(w-text_w)/2:y=h-text_h-48").arg(drawtextEscape(QString::fromStdString(item.captionText)),QString::fromStdString(item.captionColor),number(std::clamp(item.captionSize,12.0,200.0)));
                for(const auto&mask:item.masks){const auto x=animatedMaskValue(mask,"x",mask.x,item.timelineStart),y=animatedMaskValue(mask,"y",mask.y,item.timelineStart),w=animatedMaskValue(mask,"width",mask.width,item.timelineStart),h=animatedMaskValue(mask,"height",mask.height,item.timelineStart);QString expression;if(mask.shape==MaskShape::Ellipse)expression=QStringLiteral("if(lte(pow((X/W-(%1+(%3)/2))/((%3)/2),2)+pow((Y/H-(%2+(%4)/2))/((%4)/2),2),1),%5,%6)").arg(x,y,w,h,number(mask.inverted?0:mask.opacity),number(mask.inverted?mask.opacity:0));else expression=QStringLiteral("if(between(X/W,%1,(%1)+(%2))*between(Y/H,%3,(%3)+(%4)),%5,%6)").arg(x,w,y,h,number(mask.inverted?0:mask.opacity),number(mask.inverted?mask.opacity:0));chain<<QStringLiteral("geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='%1'").arg(expression);if(mask.feather>0)chain<<QStringLiteral("gblur=sigma=%1:planes=8").arg(number(mask.feather*30));}
                const auto clipLabel = QStringLiteral("vc%1").arg(videoIndex);
                filters << QStringLiteral("[%1:v]%2[%3]").arg(clipInputIndex).arg(chain.join(',')).arg(clipLabel);
                const auto next = QStringLiteral("vbase%1").arg(videoIndex + 1);
                const auto x = QStringLiteral("(W-w)/2+(%1)").arg(animatedTransformValue(item.transform,"positionX",item.transform.positionX,item.timelineStart));
                const auto y = QStringLiteral("(H-h)/2+(%1)").arg(animatedTransformValue(item.transform,"positionY",item.transform.positionY,item.timelineStart));
                filters << QStringLiteral("[%1][%2]overlay=x=%3:y=%4:eof_action=pass[%5]").arg(currentVideo, clipLabel, x, y, next);
                currentVideo = next;
                ++videoIndex;
            } else if (track.kind == TrackKind::Audio) {
                QStringList chain;
                chain << QStringLiteral("atrim=duration=%1").arg(number(item.sourceRange.duration()));
                if (item.reverse) chain << QStringLiteral("areverse");
                if (std::abs(item.speed - 1.0) > .000001) {
                    if(item.preservePitch)appendAtempo(chain,item.speed);
                    else chain<<QStringLiteral("asetrate=%1*%2,aresample=%1").arg(preset.audioSampleRate).arg(number(item.speed));
                }
                if(std::abs(item.pitchSemitones)>.000001){const auto factor=std::pow(2.0,item.pitchSemitones/12.0);chain<<QStringLiteral("asetrate=%1*%2,aresample=%1").arg(preset.audioSampleRate).arg(number(factor));appendAtempo(chain,1.0/factor);}
                if(item.fadeIn>Rational{})chain<<QStringLiteral("afade=t=in:st=0:d=%1").arg(number(item.fadeIn));
                if(item.fadeOut>Rational{})chain<<QStringLiteral("afade=t=out:st=%1:d=%2").arg(number(item.duration-item.fadeOut),number(item.fadeOut));
                const auto totalGain = item.audio.gainDb + track.audio.gainDb + renderSequence.masterAudio.gainDb;
                chain << QStringLiteral("volume=%1dB").arg(number(totalGain));
                if (item.audio.pan != 0 || track.audio.pan != 0 || renderSequence.masterAudio.pan != 0) {
                    const auto pan = std::clamp(item.audio.pan + track.audio.pan + renderSequence.masterAudio.pan, -1.0, 1.0);
                    chain << QStringLiteral("pan=stereo|c0=c0*%1|c1=c1*%2").arg(number(pan > 0 ? 1-pan : 1), number(pan < 0 ? 1+pan : 1));
                }
                appendAudioEffects(chain, item.effects);
                appendAudioEffects(chain, track.effects);
                // Echo, reverb, FFT and feedback-style filters may emit a tail after their
                // input reaches EOF. Keep every clip contribution bounded to its timeline
                // duration so preview chunks and exports cannot wait on an effect tail.
                chain << QStringLiteral("atrim=duration=%1").arg(number(item.duration));
                const auto delay = static_cast<qint64>(std::llround(item.timelineStart.asLongDouble() * 1000.0L));
                chain << QStringLiteral("adelay=%1|%1").arg(delay);
                const auto label = QStringLiteral("ac%1").arg(audioIndex++);
                filters << QStringLiteral("[%1:a]%2[%3]").arg(clipInputIndex).arg(chain.join(',')).arg(label);
                audioLabels << QStringLiteral("[%1]").arg(label);
            }
        }
    }
    if(!preset.audioOnly)for(const auto&track:renderSequence.tracks)if(track.kind==TrackKind::Video)for(const auto&item:track.items)if(item.adjustmentClip&&!item.effects.empty()){
        QStringList chain;appendVideoEffects(chain,item.effects);if(chain.empty())continue;const auto source=QStringLiteral("adjSource%1").arg(videoIndex),adjusted=QStringLiteral("adjusted%1").arg(videoIndex),next=QStringLiteral("vbase%1").arg(videoIndex+1);filters<<QStringLiteral("[%1]split=2[%2][%3]").arg(currentVideo,source,adjusted);filters<<QStringLiteral("[%1]%2[%3fx]").arg(adjusted,chain.join(','),adjusted);filters<<QStringLiteral("[%1][%2fx]overlay=enable='between(t,%3,%4)'[%5]").arg(source,adjusted,number(item.timelineStart),number(item.timelineEnd()),next);currentVideo=next;++videoIndex;
    }
    if (!preset.audioOnly) {
        filters << QStringLiteral("[%1]format=yuv420p[vout]").arg(currentVideo);
        graph.videoLabel = QStringLiteral("[vout]");
    }
    QStringList masterChain{QStringLiteral("amix=inputs=%1:duration=longest:normalize=0").arg(audioLabels.size())};
    appendAudioEffects(masterChain, renderSequence.masterEffects);
    if (renderSequence.masterLimiter) masterChain << QStringLiteral("alimiter=limit=0.95");
    filters << QStringLiteral("%1%2[aout]").arg(audioLabels.join(QString{}), masterChain.join(','));
    graph.audioLabel = QStringLiteral("[aout]");
    graph.filter = filters.join(';');
    return graph;
}

QString failureSummary(const QByteArray& stderrData) {
    const auto text = QString::fromUtf8(stderrData).trimmed();
    const auto lines = text.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QStringLiteral("FFmpeg failed without an error message.") : lines.sliced(std::max<qsizetype>(0, lines.size() - 8)).join('\n');
}

} // namespace

QString RenderEngine::ffmpegExecutable() {
    const auto executable = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    return executable.isEmpty() ? QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe") : executable;
}

bool RenderEngine::encoderAvailable(const QString& encoder) {
    QProcess process;
    process.start(ffmpegExecutable(), {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    return process.waitForFinished(10'000) && process.exitCode() == 0 && QString::fromUtf8(process.readAllStandardOutput()).contains(encoder);
}

RenderResult RenderEngine::render(const Project& project, const Sequence& sequence,
                                  const ExportSettings& settings, const std::atomic_bool& cancelled,
                                  ProgressCallback progress) {
    RenderResult result;
    const auto output = QString::fromStdString(settings.outputPath);
    result.logPath = output + QStringLiteral(".render.log");
    const auto validation = settings.validate(sequence.duration());
    if (validation) { result.error = QString::fromStdString(*validation); return result; }
    if (QFileInfo::exists(output) && !settings.overwrite) {
        result.error = QStringLiteral("The output file already exists."); return result;
    }
    if (!QFileInfo::exists(ffmpegExecutable())) { result.error = QStringLiteral("FFmpeg was not found. Reinstall the portable package or configure PATH."); return result; }
    const auto videoEncoder = QString::fromStdString(settings.preset.videoCodec);
    const auto audioEncoder = QString::fromStdString(settings.preset.audioCodec);
    const bool videoEncoderReady = settings.preset.audioOnly || encoderAvailable(videoEncoder);
    const bool audioEncoderReady = encoderAvailable(audioEncoder);
    if (!videoEncoderReady || !audioEncoderReady) {
        const auto missing = !videoEncoderReady ? videoEncoder : audioEncoder;
        result.error = QStringLiteral("Required encoder is unavailable: %1").arg(missing);
        return result;
    }
    if (!QDir{}.mkpath(QFileInfo(output).absolutePath())) {
        result.error = QStringLiteral("Could not create the output directory."); return result;
    }
    const auto graph = buildGraph(project, sequence, settings.preset);
    QStringList args{QStringLiteral("-hide_banner"), settings.overwrite ? QStringLiteral("-y") : QStringLiteral("-n")};
    args << graph.inputs << QStringLiteral("-filter_complex") << graph.filter;
    if (!settings.preset.audioOnly) args << QStringLiteral("-map") << graph.videoLabel;
    args << QStringLiteral("-map") << graph.audioLabel;
    const auto start = settings.range == ExportRange::MarkedRegion ? settings.rangeStart : Rational{};
    const auto end = settings.range == ExportRange::MarkedRegion ? settings.rangeEnd : sequence.duration();
    args << QStringLiteral("-ss") << number(start) << QStringLiteral("-t") << number(end - start);
    if (!settings.preset.audioOnly) args << QStringLiteral("-c:v") << QString::fromStdString(settings.preset.videoCodec)
        << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(settings.preset.videoBitrateKbps)
        << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    args << QStringLiteral("-c:a") << QString::fromStdString(settings.preset.audioCodec)
         << QStringLiteral("-ar") << QString::number(settings.preset.audioSampleRate);
    if (settings.preset.audioBitrateKbps > 0) args << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(settings.preset.audioBitrateKbps);
    args << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats") << output;

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpegExecutable(), args);
    if (!process.waitForStarted(5'000)) { result.error = QStringLiteral("Could not start FFmpeg: %1").arg(process.errorString()); return result; }
    QByteArray progressData;
    const auto totalUs = std::max<qint64>(1, static_cast<qint64>((end - start).asLongDouble() * 1'000'000.0L));
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        progressData += process.readAllStandardOutput();
        qsizetype newline = -1;
        while ((newline = progressData.indexOf('\n')) >= 0) {
            const auto line = progressData.first(newline).trimmed();
            progressData.remove(0, newline + 1);
            if (!line.startsWith("out_time_us=")) continue;
            bool ok = false; const auto value = line.mid(12).trimmed().toLongLong(&ok);
            if (ok && progress) progress(std::clamp(static_cast<double>(value) / totalUs, 0.0, 1.0), QStringLiteral("Rendering"));
        }
        if (cancelled.load()) { process.kill(); process.waitForFinished(5'000); result.cancelled = true; result.error = QStringLiteral("Render cancelled."); break; }
    }
    const auto stderrData = process.readAllStandardError();
    QFile log(result.logPath);
    if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&log); stream << "FFmpeg: " << ffmpegExecutable() << '\n' << "Arguments: " << args.join(' ') << "\n\n" << QString::fromUtf8(stderrData);
    }
    if (result.cancelled) { QFile::remove(output); return result; }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo::exists(output) || QFileInfo(output).size() == 0) {
        QFile::remove(output); result.error = failureSummary(stderrData); return result;
    }
    result.success = true;
    if (progress) progress(1.0, QStringLiteral("Complete"));
    return result;
}

bool RenderEngine::snapshot(const Project& project, const Sequence& sequence, const Rational& time,
                            const QString& outputPath, QString* errorMessage) {
    if (time < Rational{} || time >= sequence.duration()) { if (errorMessage) *errorMessage = QStringLiteral("Snapshot time is outside the sequence."); return false; }
    auto preset = *findExportPreset("youtube_1080p");
    preset.width = project.settings().width; preset.height = project.settings().height;
    const auto graph = buildGraph(project, sequence, preset);
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-y")};
    args << graph.inputs << QStringLiteral("-filter_complex") << (graph.filter + QStringLiteral(";[aout]anullsink"))
         << QStringLiteral("-map") << graph.videoLabel << QStringLiteral("-ss") << number(time)
         << QStringLiteral("-frames:v") << QStringLiteral("1") << outputPath;
    QProcess process; process.start(ffmpegExecutable(), args);
    if (!process.waitForFinished(120'000) || process.exitCode() != 0 || QFileInfo(outputPath).size() == 0) {
        if (process.state() != QProcess::NotRunning) process.kill();
        if (errorMessage) *errorMessage = failureSummary(process.readAllStandardError());
        QFile::remove(outputPath); return false;
    }
    return true;
}

RenderResult RenderEngine::renderPreviewCache(const Project& project,const Sequence& sequence,const QString& outputPath,int width,int height,const std::atomic_bool& cancelled,ProgressCallback progress){
    return renderPreviewWindow(project,sequence,outputPath,width,height,Rational{},sequence.duration(),cancelled,std::move(progress));
}

RenderResult RenderEngine::renderPreviewWindow(const Project& project,const Sequence& sequence,const QString& outputPath,int width,int height,Rational start,Rational duration,const std::atomic_bool& cancelled,ProgressCallback progress){
    RenderResult result;
    if(sequence.duration()<=Rational{}||start<Rational{}||start>=sequence.duration()||duration<=Rational{}){result.error="Preview range is empty or outside the sequence.";return result;}
    duration=std::min(duration,sequence.duration()-start);
    auto preset=*findExportPreset("youtube_720p");preset.width=std::max(320,width);preset.height=std::max(180,height);preset.videoCodec="libx264";preset.audioCodec="aac";
    const auto graph=buildGraph(project,sequence,preset,TimeRange{start,duration},true);QFile::remove(outputPath);
    QStringList args{"-hide_banner","-y"};args<<graph.inputs<<"-filter_complex"<<graph.filter<<"-map"<<graph.videoLabel<<"-map"<<graph.audioLabel<<"-t"<<number(duration)<<"-c:v"<<"libx264"<<"-preset"<<"ultrafast"<<"-tune"<<"zerolatency"<<"-crf"<<"30"<<"-c:a"<<"aac"<<"-b:a"<<"128k"<<"-movflags"<<"+faststart"<<"-progress"<<"pipe:1"<<"-nostats"<<outputPath;
    QProcess process;process.setProcessChannelMode(QProcess::SeparateChannels);process.start(ffmpegExecutable(),args);if(!process.waitForStarted(5000)){result.error=process.errorString();return result;}
    QByteArray data,stderrData;const auto total=std::max<qint64>(1,static_cast<qint64>(duration.asLongDouble()*1'000'000));while(process.state()!=QProcess::NotRunning){process.waitForReadyRead(100);data+=process.readAllStandardOutput();stderrData+=process.readAllStandardError();qsizetype newline;while((newline=data.indexOf('\n'))>=0){const auto line=data.first(newline).trimmed();data.remove(0,newline+1);if(line.startsWith("out_time_us=")){bool ok=false;const auto value=line.mid(12).toLongLong(&ok);if(ok&&progress)progress(std::clamp(static_cast<double>(value)/total,0.0,1.0),"Caching Program playback");}}if(cancelled.load()){process.kill();process.waitForFinished(5000);QFile::remove(outputPath);result.cancelled=true;result.error="Preview cache cancelled.";return result;}}
    stderrData+=process.readAllStandardError();if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0||!QFileInfo::exists(outputPath)||QFileInfo(outputPath).size()==0){result.error=QStringLiteral("FFmpeg preview failed (exit %1, %2):\n%3").arg(process.exitCode()).arg(process.errorString(),failureSummary(stderrData));QFile::remove(outputPath);return result;}result.success=true;if(progress)progress(1,"Program cache ready");return result;
}

QStringList RenderEngine::previewStreamArguments(const Project& project,const Sequence& sequence,
                                                 int width,int height,Rational start,
                                                 const QString& outputUrl,
                                                 Rational outputTimestampOffset){
    if(sequence.duration()<=Rational{}||start<Rational{}||start>=sequence.duration()||outputUrl.isEmpty())return {};
    // Keep startup proportional to the nearby timeline, not the entire project.
    // FFmpeg opens and probes every graph input before publishing the first packet.
    const auto duration=std::min(Rational{12,1},sequence.duration()-start);
    auto preset=*findExportPreset("youtube_720p");
    preset.width=std::max(320,width);preset.height=std::max(180,height);
    preset.videoCodec="libx264";preset.audioCodec="aac";
    const auto graph=buildGraph(project,sequence,preset,TimeRange{start,duration},true);
    QStringList args{"-hide_banner","-loglevel","error"};
    args<<graph.inputs<<"-filter_complex"<<graph.filter<<"-map"<<graph.videoLabel<<"-map"<<graph.audioLabel
        <<"-t"<<number(duration)<<"-c:v"<<"libx264"<<"-preset"<<"ultrafast"<<"-tune"<<"zerolatency"
        <<"-crf"<<"31"<<"-g"<<"15"<<"-keyint_min"<<"15"<<"-sc_threshold"<<"0"<<"-pix_fmt"<<"yuv420p"
        <<"-bsf:v"<<"filter_units=remove_types=6"
        <<"-c:a"<<"aac"<<"-b:a"<<"96k"<<"-f"<<"mpegts"<<"-mpegts_flags"<<"+resend_headers+initial_discontinuity"<<"-muxdelay"<<"0"<<"-muxpreload"<<"0"
        <<"-flush_packets"<<"1"<<"-output_ts_offset"<<number(outputTimestampOffset)
        <<"-progress"<<"pipe:2"<<"-nostats"<<outputUrl;
    return args;
}

} // namespace ytp
