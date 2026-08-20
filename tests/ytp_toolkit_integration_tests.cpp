#include "persistence/project_serializer.h"
#include "export/render_engine.h"
#include "timeline/timeline_editor.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"
#include "ytp/ytp_toolkit.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QProcess>
#include <QFileInfo>
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

using namespace ytp;

namespace {
struct Fixture final { Project project; Sequence sequence; Id videoId; Id audioId; Id tailId; };

Fixture fixture(const QString& mediaPath = QStringLiteral("toolkit-fixture.mp4")) {
    Project project{"Toolkit"}; ProjectSettings settings;settings.frameRateNumerator=30;settings.frameRateDenominator=1;project.setSettings(settings);
    MediaAsset media{.id=createId(),.path=mediaPath.toStdString(),.displayName="fixture",.duration=Rational{20,1},.frameRateNumerator=30,.frameRateDenominator=1,.width=640,.height=360,.audioSampleRate=48000};project.addMediaAsset(media);
    LibraryClip clip{.id=createId(),.mediaAssetId=media.id,.sourceRange=TimeRange{Rational{},Rational{4,1}},.name="phrase",.thumbnailTime=Rational{1,1}};project.addLibraryClip(clip);
    auto sequence=project.sequences().front();sequence.rippleMode=RippleMode::AllTracks;
    const auto inserted=TimelineEditor::insertLibraryClip(project,sequence,clip.id,sequence.tracks[1].id,Rational{},EditMode::Insert);
    const auto tail=TimelineEditor::insertLibraryClip(project,sequence,clip.id,sequence.tracks[1].id,Rational{10,1},EditMode::Overwrite);
    project.updateSequence(sequence);return {project,sequence,inserted.itemIds.front(),inserted.itemIds.back(),tail.itemIds.front()};
}

void assertLinkedPairs(const Sequence& sequence,const std::vector<Id>& ids) {
    std::map<Id,int> links;for(const auto& id:ids){const auto* item=sequence.findItem(id);assert(item);if(!item->linkedGroupId.empty())++links[item->linkedGroupId];}
    for(const auto& [id,count]:links){(void)id;assert(count==2);}
}

void assertContiguousByTrack(const Sequence& sequence,const std::vector<Id>& ids) {
    std::map<Id,std::vector<const TimelineItem*>> tracks;
    for(const auto& id:ids){const auto* item=sequence.findItem(id);assert(item);tracks[item->trackId].push_back(item);}
    for(auto& [trackId,items]:tracks){(void)trackId;std::ranges::sort(items,{},&TimelineItem::timelineStart);for(std::size_t index=1;index<items.size();++index)assert(items[index-1]->timelineEnd()==items[index]->timelineStart);}
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QCoreApplication::setOrganizationName("YTP Toolkit Test");QCoreApplication::setApplicationName("YTP Toolkit Test");QTemporaryDir temp;assert(temp.isValid());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());QSettings{}.clear();

    {
        auto f=fixture();const auto made=YtpToolkit::stutter(f.sequence,{f.videoId},6,Rational{1,10},true);assert(made.itemIds.size()==12);assert((made.duration==Rational{3,5}));assertLinkedPairs(f.sequence,made.itemIds);assertContiguousByTrack(f.sequence,made.itemIds);assert((f.sequence.findItem(f.tailId)->timelineStart==Rational{33,5}));int reversed=0;for(const auto& id:made.itemIds)if(f.sequence.findItem(id)->reverse)++reversed;assert(reversed==6);assert(!f.sequence.validate());
    }
    {
        auto f=fixture();f.sequence.rippleMode=RippleMode::Off;const auto tailLink=f.sequence.findItem(f.tailId)->linkedGroupId;for(auto& track:f.sequence.tracks)for(auto& item:track.items)if(item.linkedGroupId==tailLink)item.timelineStart=Rational{4,1};const auto made=YtpToolkit::stutter(f.sequence,{f.videoId},6,Rational{1,10},false);assertContiguousByTrack(f.sequence,made.itemIds);assert((f.sequence.findItem(f.tailId)->timelineStart==Rational{3,5}));for(const auto& track:f.sequence.tracks)for(const auto& item:track.items)if(item.linkedGroupId==tailLink)assert((item.timelineStart==Rational{3,5}));assert(!f.sequence.validate());
    }
    {
        auto f=fixture();const auto made=YtpToolkit::rapidReverse(f.sequence,{f.videoId},8,Rational{1,10});assert(made.itemIds.size()==16);assert((made.duration==Rational{4,5}));assertLinkedPairs(f.sequence,made.itemIds);for(std::size_t index=0;index<made.itemIds.size();index+=4)assert(f.sequence.findItem(made.itemIds[index])->reverse!=f.sequence.findItem(made.itemIds[index+2])->reverse);
    }
    {
        auto f=fixture();const auto made=YtpToolkit::frameRepeat(f.sequence,{f.videoId},Rational{1,30},3,4);assert(made.itemIds.size()==24);assert((made.duration==Rational{2,5}));std::set<Rational> frames;for(const auto& id:made.itemIds){const auto* item=f.sequence.findItem(id);const auto*track=f.sequence.findTrack(item->trackId);assert(item->freezeFrame==(track->kind==TrackKind::Video));if(item->freezeFrame)frames.insert(item->freezeSourceTime);}assert(frames.size()==3);
    }
    {
        auto f=fixture();(void)TimelineEditor::addMarker(f.sequence,Rational{5,1},"beat 1");(void)TimelineEditor::addMarker(f.sequence,Rational{6,1},"beat 2");const auto before=f.sequence.findTrack(f.sequence.tracks[1].id)->items.size();const auto made=YtpToolkit::rhythmRepeat(f.sequence,{f.videoId},Rational{5,1},120,3,Rational{1,10},true);assert(made.itemIds.size()==6);assert(f.sequence.findTrack(f.sequence.tracks[1].id)->items.size()==before+3);assert(f.sequence.findItem(f.videoId));assert((f.sequence.findItem(made.itemIds[4])->timelineStart==Rational{13,2}));
    }
    {
        auto f=fixture();const auto made=YtpToolkit::speedLadder(f.sequence,{f.videoId},3,.5,2,3,false);assert(made.itemIds.size()==6);assertLinkedPairs(f.sequence,made.itemIds);const auto* first=f.sequence.findItem(made.itemIds[0]);const auto* last=f.sequence.findItem(made.itemIds[4]);assert(std::abs(first->speed-.5)<1e-9&&std::abs(last->speed-2)<1e-9);assert(first->pitchSemitones==0&&last->pitchSemitones==6);
    }
    {
        auto f=fixture();const auto made=YtpToolkit::safeEarrape(f.sequence,{f.videoId},.75);assert(made.itemIds.size()==1);const auto* audio=f.sequence.findItem(made.itemIds.front());assert(audio->effects.size()==4&&audio->audio.gainDb<=12&&f.sequence.masterLimiter);assert(f.sequence.findTrack(audio->trackId)->kind==TrackKind::Audio);
    }
    for(const auto& preset:YtpToolkit::visualPresets()) {
        auto f=fixture();const auto made=YtpToolkit::applyVisualPreset(f.sequence,{f.videoId},preset.id);assert(made.itemIds.size()==1);const auto* video=f.sequence.findItem(f.videoId);assert(!video->effects.empty()||video->transform.flipHorizontal);assert(!f.sequence.validate());
    }
    assert(YtpToolkit::visualPresets().size()>=70);
    for(const auto& preset:YtpToolkit::audioPresets()) {
        auto f=fixture();const auto made=YtpToolkit::applyAudioPreset(f.sequence,{f.videoId},preset.id);assert(made.itemIds.size()==1);const auto* audio=f.sequence.findItem(made.itemIds.front());assert(audio&&f.sequence.findTrack(audio->trackId)->kind==TrackKind::Audio);assert(!audio->effects.empty());assert(f.sequence.masterLimiter);assert(!f.sequence.validate());
    }
    assert(YtpToolkit::audioPresets().size()>=12);
    for(const auto& preset:YtpToolkit::combinedPresets()) {
        auto f=fixture();const auto made=YtpToolkit::applyCombinedPreset(f.sequence,{f.videoId},preset.id);assert(made.itemIds.size()==2);
        const auto* video=f.sequence.findItem(f.videoId);const auto* audio=f.sequence.findItem(f.audioId);assert(video&&audio&&!video->effects.empty()&&!audio->effects.empty());assert(f.sequence.masterLimiter);assert(!f.sequence.validate());
    }
    assert(YtpToolkit::combinedPresets().size()>=36);
    {
        auto f=fixture();(void)TimelineEditor::addMarker(f.sequence,Rational{1,1},"word");(void)TimelineEditor::addMarker(f.sequence,Rational{3,1},"word");const auto made=YtpToolkit::sentenceMixer(f.sequence,{f.videoId},{2,0,1,1});assert(made.itemIds.size()==8);assert((made.duration==Rational{6,1}));assertLinkedPairs(f.sequence,made.itemIds);assert((f.sequence.findItem(f.tailId)->timelineStart==Rational{12,1}));
    }
    {
        auto f=fixture();YtpMacro macro{"Test Macro",{{YtpTool::Stutter,{{"repeats",4},{"sliceMs",100}},{}},{YtpTool::VisualPreset,{},{{"preset","pixel_scream"}}}}};const auto made=YtpToolkit::applyMacro(f.sequence,{f.videoId},macro);assert(made.itemIds.size()==4);for(const auto& id:made.itemIds)assert(!f.sequence.findItem(id)->effects.empty());
    }
    {
        auto f=fixture();(void)TimelineEditor::addMarker(f.sequence,Rational{1,1},"cut");YtpMacro macro{"Words",{{YtpTool::SentenceMixer,{},{{"order","1,0,1"}}}}};const auto made=YtpToolkit::applyMacro(f.sequence,{f.videoId},macro);assert(made.itemIds.size()==6);assert((made.duration==Rational{7,1}));
    }
    {
        auto f=fixture();const RandomizerOptions options{42,1,1,.75,1.5,-6,6,true};const auto planA=YtpToolkit::previewRandomizer(f.sequence,{f.videoId,f.tailId},options);const auto planB=YtpToolkit::previewRandomizer(f.sequence,{f.videoId,f.tailId},options);assert(planA.after==planB.after&&planA.before==f.sequence&&!planA.changes.empty());const auto* video=planA.after.findItem(f.videoId);const auto* audio=planA.after.findItem(f.audioId);assert(video&&audio&&video->timelineStart==audio->timelineStart&&video->duration==audio->duration&&video->speed==audio->speed&&video->reverse==audio->reverse);YtpToolkit::commitRandomizer(f.sequence,planA);assert(f.sequence==planA.after);bool staleRejected=false;try{YtpToolkit::commitRandomizer(f.sequence,planA);}catch(const std::invalid_argument&){staleRejected=true;}assert(staleRejected);
    }

    // A representative generated gag must pass through the full-resolution export graph.
    {
        const auto mediaPath=temp.filePath("render-source.mp4");QProcess ffmpeg;ffmpeg.start("C:/msys64/ucrt64/bin/ffmpeg.exe",{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=160x90:rate=30:duration=4","-f","lavfi","-i","sine=frequency=440:duration=4","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-shortest",mediaPath});assert(ffmpeg.waitForFinished(30000)&&ffmpeg.exitCode()==0);
        auto f=fixture(mediaPath);YtpMacro macro{"Rendered gag",{{YtpTool::Stutter,{{"repeats",4},{"sliceMs",100},{"alternate",1}},{}},{YtpTool::VisualPreset,{},{{"preset","deep_fried"}}},{YtpTool::SafeEarrape,{{"intensity",.5}},{}}}};const auto made=YtpToolkit::applyMacro(f.sequence,{f.videoId},macro);assert(!made.itemIds.empty());f.project.updateSequence(f.sequence);auto preset=*findExportPreset("youtube_720p");preset.width=160;preset.height=90;preset.videoBitrateKbps=500;const auto output=temp.filePath("toolkit-render.mp4");ExportSettings settings{preset,ExportRange::MarkedRegion,Rational{},Rational{2,5},output.toStdString()};std::atomic_bool cancel{false};const auto rendered=RenderEngine::render(f.project,f.sequence,settings,cancel);assert(rendered.success&&QFileInfo(output).size()>0);

        auto feedback=fixture(mediaPath);const auto applied=YtpToolkit::applyCombinedPreset(feedback.sequence,{feedback.videoId},"feedback_scream");assert(applied.itemIds.size()==2);feedback.project.updateSequence(feedback.sequence);const auto feedbackOutput=temp.filePath("feedback-scream-render.mp4");const auto feedbackRendered=RenderEngine::renderPreviewWindow(feedback.project,feedback.sequence,feedbackOutput,160,90,Rational{},Rational{1,1},cancel);if(!feedbackRendered.success)std::cerr<<feedbackRendered.error.toStdString()<<'\n';assert(feedbackRendered.success&&QFileInfo(feedbackOutput).size()>0);
    }

    // Controller path: each toolkit operation is one undoable edit, custom macros persist,
    // and a randomizer preview does not mutate until explicitly committed.
    {
        auto f=fixture();f.project.updateSequence(f.sequence);QString error;const auto path=temp.filePath("toolkit.ytp.json");assert(ProjectSerializer::save(f.project,path,&error));ProjectController projectController;assert(projectController.openProject(QUrl::fromLocalFile(path)));TimelineController controller(&projectController);controller.select(QString::fromStdString(f.videoId));controller.startMacroRecording();assert(controller.buildStutter(4,100,false));controller.setPlayheadMs(50);assert(!controller.instantPreview().value("exact").toBool());assert(controller.applyYtpVisualPreset("deep_fried"));assert(controller.recordedMacroSteps()==2);assert(controller.saveRecordedMacro("My Macro"));assert(controller.ytpMacros().contains("My Macro"));projectController.undo();projectController.undo();controller.select(QString::fromStdString(f.videoId));assert(controller.applyYtpMacro("My Macro"));assert(controller.selectedIds().size()==4);assert(controller.previewRandomizer(99,.5,.5,.8,1.2,-3,3,true));const auto before=projectController.project().sequences().front();assert(!controller.randomizerPreview().isEmpty());controller.cancelRandomizer();assert(projectController.project().sequences().front()==before);assert(controller.previewRandomizer(99,.5,.5,.8,1.2,-3,3,true));assert(controller.commitRandomizer());assert(projectController.project().sequences().front()!=before);assert(controller.removeYtpMacro("My Macro"));
    }

    std::cout<<"All Milestone 5 YTP builders, distortion presets, safe audio, macros, Sentence Mixer, and deterministic randomizer passed.\n";return 0;
}
