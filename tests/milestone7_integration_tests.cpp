#include "export/render_engine.h"
#include "media/media_analysis.h"
#include "persistence/project_serializer.h"
#include "ytp/remix_toolkit.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

using namespace ytp;

namespace {
TimelineItem itemFor(const MediaAsset& media,const Track& track,Rational start,Rational duration){
    return {.id=createId(),.mediaAssetId=media.id,.trackId=track.id,.timelineStart=start,
            .sourceRange=TimeRange{Rational{},duration},.duration=duration};
}
QByteArray trackingFrame(int width,int height,int left){
    QByteArray frame(width*height,'\0');
    for(int y=7;y<12;++y)for(int x=left;x<left+5;++x)frame[y*width+x]=static_cast<char>(220);
    return frame;
}
}

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    assert(temp.isValid());
    qputenv("YTP_CACHE_DIR",temp.filePath("cache").toUtf8());

    const auto mediaPath=temp.filePath("m7-fixture.mp4");
    QProcess ffmpeg;
    ffmpeg.start(RenderEngine::ffmpegExecutable(),{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i",
        "testsrc2=size=160x90:rate=30:duration=1.2","-f","lavfi","-i","aevalsrc=if(lt(mod(t\\,0.3)\\,0.05)\\,sin(2*PI*440*t)\\,0):s=48000:d=1.2",
        "-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-shortest",mediaPath});
    assert(ffmpeg.waitForFinished(30000)&&ffmpeg.exitCode()==0);

    Project project("Milestone 7");
    MediaAsset media{.id=createId(),.path=mediaPath.toStdString(),.displayName="infomercial",
        .duration=Rational{6,5},.frameRateNumerator=30,.frameRateDenominator=1,.width=160,.height=90,
        .audioSampleRate=48000,.transcript={{.id=createId(),.start=Rational{},.duration=Rational{2,5},.text="but wait"},
                                          {.id=createId(),.start=Rational{2,5},.duration=Rational{2,5},.text="there is more"}}};
    project.addMediaAsset(media);

    const auto exact=RemixToolkit::searchWords(project,"wait",false);
    const auto phonetic=RemixToolkit::searchWords(project,"wate",true);
    assert(!exact.empty()&&!phonetic.empty()&&phonetic.front().score<1.0);
    const auto sentence=RemixToolkit::sentenceSequence(project,{exact.front(),RemixToolkit::searchWords(project,"more",false).front()},"Catchphrase",Rational{1,100},Rational{1,100},true);
    assert(sentence.sequence.duration()>Rational{});
    const auto& sentenceVideo=sentence.sequence.tracks[1].items;
    assert(sentenceVideo.size()==2&&sentenceVideo.front().captionEnabled&&sentenceVideo.front().captionText=="wait");

    auto beats=createDefaultSequence();
    beats.beatGrid=RemixToolkit::estimateBeatGrid({Rational{},Rational{1,2},Rational{1,1},Rational{3,2}},4);
    auto beatItem=itemFor(media,beats.tracks[1],Rational{12,100},Rational{1,1});
    const auto beatId=beatItem.id;
    beats.tracks[1].items.push_back(beatItem);
    RemixToolkit::snapItemsToBeats(beats,{beatId});
    assert(beats.findItem(beatId)->timelineStart==Rational{});
    RemixToolkit::addAudioReactiveKeys(beats,{beatId},"screen_shake","amount",0,20);
    assert(!beats.findItem(beatId)->effects.front().parameters.front().keyframes.empty());

    auto sequences=project.sequences();
    auto main=sequences.front();
    auto video=itemFor(media,main.tracks[1],Rational{},Rational{1,1});
    video.captionEnabled=true;video.captionText="BUT WAIT!";video.captionSize=28;
    main.tracks[1].items.push_back(video);
    main.beatGrid=beats.beatGrid;
    auto child=sentence.sequence;
    sequences={main,child};
    project.setSequences(sequences);
    project.addCompoundClip({.id=createId(),.sequenceId=child.id,.name="Catchphrase",.color="#5aa9e6",.updateAllInstances=true,.createdAtMs=1});
    QString error;
    const auto savePath=temp.filePath("m7.ytp.json");
    assert(ProjectSerializer::save(project,savePath,&error));
    const auto loaded=ProjectSerializer::load(savePath,&error);
    assert(loaded&&loaded->formatVersion()==6&&loaded->compoundClips()==project.compoundClips());
    assert(loaded->sequences().front().beatGrid.enabled&&loaded->sequences().front().tracks[1].items.front().captionText=="BUT WAIT!");

    // Exercise the actual UI-facing advanced controller paths, not only their algorithms.
    {
        ProjectController projectController;assert(projectController.openProject(QUrl::fromLocalFile(savePath)));
        TimelineController controller(&projectController);
        const auto activeVideoId=QString::fromStdString(main.tracks[1].items.front().id);
        const auto activeTrackId=QString::fromStdString(main.tracks[1].id);
        controller.select(activeVideoId);
        assert(controller.detectBeats());
        assert(controller.addMask(0));
        const auto maskId=controller.inspector().value("masks").toList().front().toMap().value("id").toString();
        assert(controller.trackMask(maskId));
        QEventLoop trackLoop;QTimer trackPoll;trackPoll.setInterval(25);QObject::connect(&trackPoll,&QTimer::timeout,&trackLoop,[&]{const auto tasks=controller.backgroundTasks();if(!tasks.isEmpty()&&tasks.back().toMap().value("status")!="Running")trackLoop.quit();});QTimer trackTimeout;trackTimeout.setSingleShot(true);trackTimeout.setInterval(30'000);QObject::connect(&trackTimeout,&QTimer::timeout,&trackLoop,&QEventLoop::quit);trackPoll.start();trackTimeout.start();trackLoop.exec();
        assert(controller.applyTrackedMotion(maskId,0));

        QVariantList words{{QVariantMap{{"mediaId",QString::fromStdString(media.id)},{"startMs",0},{"endMs",400},{"text","but"},{"score",1.0}}},
                           {QVariantMap{{"mediaId",QString::fromStdString(media.id)},{"startMs",400},{"endMs",800},{"text","wait"},{"score",1.0}}}};
        assert(controller.buildSentenceV2(words,"Controller sentence",10,10,activeTrackId,2'000));

        controller.select(activeVideoId);
        assert(controller.createCompoundFromSelection("Controller compound"));
        assert(!controller.compoundClips().isEmpty());
        const auto compoundId=controller.compoundClips().back().toMap().value("id").toString();
        assert(controller.insertCompound(compoundId,activeTrackId,4'000,false));
        assert(!controller.removeCompound(compoundId)); // referenced compounds are protected

        controller.setContinuousCaching(false);
        assert(controller.renderContinuousProgramCache());
        if(!controller.backgroundTasks().isEmpty())controller.cancelBackgroundTask(controller.backgroundTasks().back().toMap().value("id").toString());
        QEventLoop cacheLoop;QTimer cachePoll;cachePoll.setInterval(50);QObject::connect(&cachePoll,&QTimer::timeout,&cacheLoop,[&]{if(!controller.previewRendering())cacheLoop.quit();});QTimer cacheTimeout;cacheTimeout.setSingleShot(true);cacheTimeout.setInterval(60'000);QObject::connect(&cacheTimeout,&QTimer::timeout,&cacheLoop,&QEventLoop::quit);cachePoll.start();cacheTimeout.start();cacheLoop.exec();
        assert(!controller.previewRendering());
        assert(controller.clearMediaCache());
    }

    const auto points=MediaAnalysis::trackGrayFrames({trackingFrame(32,24,4),trackingFrame(32,24,6),trackingFrame(32,24,8)},32,24,4.0/32.0,7.0/24.0,5.0/32.0,5.0/24.0,.1);
    assert(points.size()==3&&points.back().x>points.front().x&&points.back().confidence>0);

    const std::set<std::string> advanced={"rgb_split","chromatic_aberration","wave_warp","lens_warp","kaleidoscope","edge_echo","recursive_trails","time_smear","frame_blend","screen_shake","chroma_key","datamosh","scanlines","vhs_noise","solarize","emboss","neon_edges","vignette","color_cycle","strobe","channel_swap","vertical_roll","bad_tv","cartoon_edges"};
    for(const auto&type:advanced)assert(findEffectDescriptor(type));
    auto renderProject=project;
    auto renderSequence=renderProject.sequences().front();
    std::atomic_bool cancelled{false};
    int rendered=0;
    for(const auto&type:advanced){
        renderSequence.tracks[1].items.front().effects={createEffect(type)};
        const auto output=temp.filePath(QString("effect-%1.mp4").arg(rendered++));
        const auto result=RenderEngine::renderPreviewCache(renderProject,renderSequence,output,160,90,cancelled);
        assert(result.success&&QFileInfo(output).size()>0);
    }

    Project large("100k events");
    MediaAsset longMedia=media;longMedia.id=createId();longMedia.duration=Rational{2000,1};longMedia.path="offline-long.mp4";longMedia.transcript.clear();
    large.addMediaAsset(longMedia);
    auto longSequence=large.sequences().front();
    auto& longTrack=longSequence.tracks[2];
    longTrack.items.reserve(100000);
    QElapsedTimer timer;timer.start();
    for(int i=0;i<100000;++i)longTrack.items.push_back(itemFor(longMedia,longTrack,Rational{i,100},Rational{1,100}));
    large.updateSequence(longSequence);
    assert(!large.validate());
    assert(timer.elapsed()<30000);

    std::cout<<"Milestone 7 passed: sentence/phonetics/captions, beat grid, compounds, tracking, 12 effects, cached render, and 100k events.\n";
    return 0;
}
