#include "export/export_settings.h"
#include "export/render_engine.h"
#include "persistence/project_serializer.h"
#include "timeline/effects_editor.h"
#include "timeline/timeline_editor.h"
#include "ui/export_controller.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cassert>
#include <iostream>
#include <thread>

using namespace ytp;

namespace {
bool ffmpeg(const QStringList& arguments, int timeout = 120'000) {
    QProcess process; process.start("C:/msys64/ucrt64/bin/ffmpeg.exe", arguments);
    return process.waitForFinished(timeout) && process.exitCode() == 0;
}

QString probe(const QString& path, const QString& entries) {
    QProcess process; process.start("C:/msys64/ucrt64/bin/ffprobe.exe", {"-v","error","-show_entries",entries,"-of","default=noprint_wrappers=1",path});
    assert(process.waitForFinished(30'000) && process.exitCode() == 0); return QString::fromUtf8(process.readAllStandardOutput());
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QCoreApplication::setOrganizationName("YTP Export Test"); QCoreApplication::setApplicationName("YTP Export Test");
    QTemporaryDir temp; assert(temp.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path()); QSettings{}.clear();
    const auto original = temp.filePath("original.mp4");
    const auto fakeProxy = temp.filePath("proxy.mp4");
    assert(ffmpeg({"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","color=red:size=320x180:rate=30:duration=3","-f","lavfi","-i","sine=frequency=880:duration=3","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-shortest",original}));
    assert(ffmpeg({"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","color=blue:size=80x44:rate=10:duration=3","-f","lavfi","-i","sine=frequency=220:duration=3","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-shortest",fakeProxy}));

    Project project{"Alpha Export"}; ProjectSettings projectSettings; projectSettings.width=320; projectSettings.height=180; projectSettings.frameRateNumerator=30; projectSettings.frameRateDenominator=1; project.setSettings(projectSettings);
    MediaAsset media{.id=createId(),.path=original.toStdString(),.displayName="original",.duration=Rational{3,1},.frameRateNumerator=30,.frameRateDenominator=1,.width=320,.height=180,.audioSampleRate=48000,.proxyPath=fakeProxy.toStdString(),.proxyReady=true}; project.addMediaAsset(media);
    LibraryClip clip{.id=createId(),.mediaAssetId=media.id,.sourceRange=TimeRange{Rational{},Rational{3,1}},.name="red original",.thumbnailTime=Rational{1,1}}; project.addLibraryClip(clip);
    auto sequence=project.sequences().front(); auto inserted=TimelineEditor::insertLibraryClip(project,sequence,clip.id,sequence.tracks[1].id,Rational{},EditMode::Insert); assert(inserted.itemIds.size()==2);
    const auto effect=EffectsEditor::addEffect(sequence,EffectTarget::Item,inserted.itemIds.front(),"brightness_contrast"); EffectsEditor::setParameter(sequence,EffectTarget::Item,inserted.itemIds.front(),effect,"contrast",1.1);
    project.updateSequence(sequence); assert(!project.validate());

    assert(exportPresets().size() >= 6); assert(findExportPreset("youtube_1080p")); assert(findExportPreset("audio_wav"));
    ExportSettings invalid{*findExportPreset("youtube_720p"),ExportRange::MarkedRegion,Rational{2,1},Rational{1,1},"x.mp4"}; assert(invalid.validate(sequence.duration()));

    auto preset=*findExportPreset("youtube_720p"); preset.width=320; preset.height=180; preset.videoBitrateKbps=1000;
    const auto fullPath=temp.filePath("full.mp4"); ExportSettings full{preset,ExportRange::EntireSequence,{}, {},fullPath.toStdString()}; std::atomic_bool noCancel{false}; double lastProgress=0;
    const auto fullResult=RenderEngine::render(project,sequence,full,noCancel,[&](double value,const QString&){lastProgress=value;}); assert(fullResult.success); assert(lastProgress==1.0); assert(QFileInfo(fullResult.logPath).exists());
    const auto duplicateResult=RenderEngine::render(project,sequence,full,noCancel); assert(!duplicateResult.success&&duplicateResult.error.contains("exists"));
    const auto fullProbe=probe(fullPath,"stream=width,height:format=duration"); assert(fullProbe.contains("width=320")&&fullProbe.contains("height=180"));
    auto creativeProject=project;auto creativeSequence=sequence;
    auto* creativeVideo=creativeSequence.findItem(inserted.itemIds.front());auto* creativeAudio=creativeSequence.findItem(inserted.itemIds.back());assert(creativeVideo&&creativeAudio);
    for(const auto* type:{"impact_zoom","spin","pendulum","perspective_tilt","elastic_wave","glitch_bands","thermal","motion_burn","block_shuffle","shear","fisheye","tiny_planet","oscilloscope","frame_randomizer","motion_amplify","frame_xor","pixel_bloom","xray_edges"})creativeVideo->effects.push_back(createEffect(type));
    for(const auto* type:{"chorus","phaser","stereo_widen","crystalizer","ring_mod","frequency_shift","robotize","whisperize","virtual_bass","haas_spread"})creativeAudio->effects.push_back(createEffect(type));
    creativeProject.updateSequence(creativeSequence);const auto creativePath=temp.filePath("creative-effects.mp4");ExportSettings creative{preset,ExportRange::MarkedRegion,Rational{},Rational{1,1},creativePath.toStdString()};const auto creativeResult=RenderEngine::render(creativeProject,creativeSequence,creative,noCancel);assert(creativeResult.success);assert(probe(creativePath,"stream=codec_type:format=duration").contains("codec_type=video"));
    // The native high-variety processors must work in the full export graph, not only in cached preview renders.
    for(const auto* type:{"video_feedback","pixel_sort","water_surface","elastic_scale","analog_nosync","film_gate_weave","nervous_frames","light_graffiti","digital_glitch","ordered_dither","cmyk_halftone","clone_grid","edge_glow_native","soft_glow_native","ink_cartoon","film_projector","frame_delay"}){
        auto nativeProject=project;auto nativeSequence=sequence;auto* nativeVideo=nativeSequence.findItem(inserted.itemIds.front());assert(nativeVideo);nativeVideo->effects={createEffect(type)};nativeProject.updateSequence(nativeSequence);
        const auto nativePath=temp.filePath(QString("native-%1.mp4").arg(type));ExportSettings nativeSettings{preset,ExportRange::MarkedRegion,Rational{},Rational{2,5},nativePath.toStdString()};const auto nativeResult=RenderEngine::render(nativeProject,nativeSequence,nativeSettings,noCancel);if(!nativeResult.success)qCritical()<<"Native export failure"<<type<<nativeResult.error;assert(nativeResult.success);const auto nativeProbe=probe(nativePath,"stream=width,height");assert(nativeProbe.contains("width=320")&&nativeProbe.contains("height=180"));
    }
    // The export must stay red/full-resolution even though a blue 80x44 proxy is marked ready.
    const auto sample=temp.filePath("sample.txt"); QProcess pixels; pixels.start("C:/msys64/ucrt64/bin/ffmpeg.exe",{"-v","error","-ss","1","-i",fullPath,"-vf","crop=1:1:160:90,format=rgb24","-frames:v","1","-f","rawvideo","pipe:1"}); assert(pixels.waitForFinished(30000)&&pixels.exitCode()==0); const auto rgb=pixels.readAllStandardOutput(); assert(rgb.size()>=3&&static_cast<unsigned char>(rgb[0])>static_cast<unsigned char>(rgb[2])); (void)sample;

    const auto regionPath=temp.filePath("region.mp4"); ExportSettings region{preset,ExportRange::MarkedRegion,Rational{1,2},Rational{3,2},regionPath.toStdString()}; assert(RenderEngine::render(project,sequence,region,noCancel).success); const auto regionProbe=probe(regionPath,"format=duration"); assert(regionProbe.contains("duration=1."));
    auto distantProject=project;auto distantSequence=sequence;for(auto&track:distantSequence.tracks)for(auto&item:track.items)item.timelineStart+=Rational{600,1};distantProject.updateSequence(distantSequence);
      const auto distantPreview=temp.filePath("distant-preview.mp4");QElapsedTimer distantTimer;distantTimer.start();const auto distantResult=RenderEngine::renderPreviewWindow(distantProject,distantSequence,distantPreview,320,180,Rational{600,1},Rational{2,1},noCancel);assert(distantResult.success&&distantTimer.elapsed()<15'000);const auto distantProbe=probe(distantPreview,"stream=codec_type,start_time:format=duration");assert(distantProbe.contains("codec_type=video")&&distantProbe.contains("codec_type=audio")&&distantProbe.contains("duration=2."));
      // A late source range with expensive effects must be bounded-seeked and
      // processed at preview resolution instead of decoding from zero at source resolution.
      const auto largeSource=temp.filePath("large-late-source.mp4");assert(ffmpeg({"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","color=c=0x3050a0:size=1920x1080:rate=30:duration=18","-f","lavfi","-i","sine=frequency=330:duration=18","-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p","-c:a","aac","-shortest",largeSource}));
      Project fastProject{"Preview performance"};auto fastSettings=projectSettings;fastSettings.width=1920;fastSettings.height=1080;fastProject.setSettings(fastSettings);MediaAsset largeMedia{.id=createId(),.path=largeSource.toStdString(),.displayName="late HD source",.duration=Rational{18,1},.frameRateNumerator=30,.frameRateDenominator=1,.width=1920,.height=1080,.audioSampleRate=48000};fastProject.addMediaAsset(largeMedia);LibraryClip lateClip{.id=createId(),.mediaAssetId=largeMedia.id,.sourceRange=TimeRange{Rational{16,1},Rational{2,1}},.name="late range",.thumbnailTime=Rational{16,1}};fastProject.addLibraryClip(lateClip);auto fastSequence=fastProject.sequences().front();const auto fastInserted=TimelineEditor::insertLibraryClip(fastProject,fastSequence,lateClip.id,fastSequence.tracks[1].id,Rational{},EditMode::Insert);auto*fastVideo=fastSequence.findItem(fastInserted.itemIds.front());assert(fastVideo);for(const auto*type:{"video_feedback","pixel_sort","radial_ripple"})fastVideo->effects.push_back(createEffect(type));fastProject.updateSequence(fastSequence);const auto fastPreview=temp.filePath("fast-preview.mp4");QElapsedTimer fastTimer;fastTimer.start();const auto fastResult=RenderEngine::renderPreviewWindow(fastProject,fastSequence,fastPreview,640,360,Rational{},Rational{2,1},noCancel);const auto fastElapsed=fastTimer.elapsed();if(!fastResult.success)qCritical()<<fastResult.error;std::cout<<"HD late-range heavy preview: "<<fastElapsed<<" ms\n";assert(fastResult.success&&fastElapsed<8'000);assert(probe(fastPreview,"stream=width,height").contains("width=640"));
      const auto audioPreset=*findExportPreset("audio_wav"); const auto wavPath=temp.filePath("audio.wav"); ExportSettings audio{audioPreset,ExportRange::MarkedRegion,Rational{},Rational{1,1},wavPath.toStdString()}; assert(RenderEngine::render(project,sequence,audio,noCancel).success); assert(probe(wavPath,"stream=codec_type").contains("codec_type=audio"));
    const auto snapshot=temp.filePath("snapshot.png"); QString error; assert(RenderEngine::snapshot(project,sequence,Rational{1,1},snapshot,&error)); assert(QFileInfo(snapshot).size()>0);

    std::atomic_bool cancelled{true}; const auto cancelledPath=temp.filePath("cancelled.mp4"); ExportSettings cancelSettings{preset,ExportRange::EntireSequence,{}, {},cancelledPath.toStdString()}; const auto cancelResult=RenderEngine::render(project,sequence,cancelSettings,cancelled); assert(cancelResult.cancelled&&!QFileInfo::exists(cancelledPath));
    auto missingCodec=preset; missingCodec.videoCodec="encoder_that_does_not_exist"; ExportSettings codecError{missingCodec,ExportRange::EntireSequence,{}, {},temp.filePath("bad.mp4").toStdString()}; const auto failed=RenderEngine::render(project,sequence,codecError,noCancel); assert(!failed.success&&failed.error.contains("unavailable"));

    const auto projectPath=temp.filePath("queue.ytp.json"); assert(ProjectSerializer::save(project,projectPath,&error)); ProjectController projectController; assert(projectController.openProject(QUrl::fromLocalFile(projectPath))); TimelineController timelineController(&projectController); ExportController exportController(&projectController,&timelineController);
    const auto queuedPath=temp.filePath("queued.mp4"); QVariantMap custom{{"container","mp4"},{"width",320},{"height",180},{"videoCodec","libx264"},{"audioCodec","aac"},{"videoBitrateKbps",800},{"audioBitrateKbps",128},{"audioSampleRate",48000},{"audioOnly",false}};
    const auto mainSequenceId=timelineController.activeSequenceId();assert(timelineController.createSequence("Empty export check"));assert(!exportController.enqueue(QUrl::fromLocalFile(temp.filePath("must-not-render.mp4")),"youtube_720p",0,0,false,custom));assert(timelineController.switchSequence(mainSequenceId));
    assert(exportController.enqueue(QUrl::fromLocalFile(queuedPath),"youtube_720p",0,3000,false,custom)); QEventLoop loop; QTimer poll; poll.setInterval(50); QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(!exportController.busy())loop.quit();}); QTimer timeout; timeout.setSingleShot(true); timeout.setInterval(120000); QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit); poll.start(); timeout.start(); loop.exec(); assert(!exportController.busy()); assert(QFileInfo(queuedPath).size()>0); assert(exportController.jobs().front().toMap().value("state")=="Complete");
    const auto completedJobId=exportController.jobs().front().toMap().value("jobId").toString();assert(!exportController.logUrl(completedJobId).isEmpty());
    const auto controllerSnapshot=temp.filePath("controller-snapshot.png");assert(exportController.saveSnapshot(QUrl::fromLocalFile(controllerSnapshot),1'000));assert(QFileInfo(controllerSnapshot).size()>0);
    assert(exportController.saveCustomPreset("My YouTube",custom)); assert(exportController.presets().size()>=7);QString customPresetId;for(const auto&value:exportController.presets()){const auto map=value.toMap();if(map.value("name")=="My YouTube")customPresetId=map.value("id").toString();}assert(!customPresetId.isEmpty()&&exportController.removeCustomPreset(customPresetId));
    const auto cancelQueuePath=temp.filePath("queue-cancel.mp4");assert(exportController.enqueue(QUrl::fromLocalFile(cancelQueuePath),"youtube_720p",0,3000,false,custom));const auto cancelJobId=exportController.jobs().front().toMap().value("jobId").toString();assert(exportController.cancel(cancelJobId));QEventLoop cancelLoop;QTimer cancelPoll;cancelPoll.setInterval(25);QObject::connect(&cancelPoll,&QTimer::timeout,&cancelLoop,[&]{if(!exportController.busy())cancelLoop.quit();});QTimer cancelTimeout;cancelTimeout.setSingleShot(true);cancelTimeout.setInterval(120000);QObject::connect(&cancelTimeout,&QTimer::timeout,&cancelLoop,&QEventLoop::quit);cancelPoll.start();cancelTimeout.start();cancelLoop.exec();assert(!exportController.busy());
    exportController.clearFinished(); assert(exportController.jobs().isEmpty());
    std::cout << "Export presets, full-resolution rendering, marked ranges, audio-only output, snapshot, queue, progress, logs, cancellation, and codec failures passed.\n";
    return 0;
}
