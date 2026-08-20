#include "timeline/timeline_editor.h"
#include "media/media_cache.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(const bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct Fixture {
    ytp::ProjectController project;
    std::unique_ptr<ytp::TimelineController> timeline;
    ytp::Id clipId;
    ytp::Id videoTrackId;
    ytp::Id audioTrackId;
    ytp::Id videoItemId;
    ytp::Id audioItemId;

    Fixture() {
        auto seeded = project.project();
        ytp::MediaAsset media{.id=ytp::createId(), .path="surface.mp4", .displayName="Surface",
            .duration=ytp::Rational{20,1}, .frameRateNumerator=30, .frameRateDenominator=1,
            .width=1920, .height=1080, .audioSampleRate=48'000,
            .transcript={{.id=ytp::createId(),.start=ytp::Rational{},.duration=ytp::Rational{1,2},.text="but"},
                         {.id=ytp::createId(),.start=ytp::Rational{1,2},.duration=ytp::Rational{1,2},.text="wait"}}};
        seeded.addMediaAsset(media);
        ytp::LibraryClip clip{.id=ytp::createId(), .mediaAssetId=media.id,
            .sourceRange=ytp::TimeRange{ytp::Rational{}, ytp::Rational{4,1}},
            .name="Surface clip", .thumbnailTime=ytp::Rational{2,1}};
        clipId = clip.id;
        seeded.addLibraryClip(std::move(clip));
        auto sequence = seeded.sequences().front();
        videoTrackId = sequence.tracks[1].id;
        audioTrackId = sequence.tracks[2].id;
        const auto inserted = ytp::TimelineEditor::insertLibraryClip(
            seeded, sequence, clipId, videoTrackId, ytp::Rational{}, ytp::EditMode::Overwrite);
        videoItemId = inserted.itemIds.front();
        audioItemId = inserted.itemIds.back();
        seeded.updateSequence(std::move(sequence));
        check(project.applyProjectEdit(project.project(), std::move(seeded), "Seed surface test"),
              "surface fixture project is installed");
        timeline = std::make_unique<ytp::TimelineController>(&project);
    }
};
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir settingsDirectory;
    check(settingsDirectory.isValid(), "temporary settings directory is available");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());

    {
        Fixture f;
        auto& t = *f.timeline;
        f.project.updateSessionState({{"playheadMs", 1'250}, {"workspaceMode", 2}});
        check(f.project.sessionState().value("playheadMs").toLongLong() == 1'250,
              "workspace state is accepted for exact session persistence");
        const auto& initialSequence = f.project.project().sequences().front();
        const auto* initialVideo = initialSequence.findItem(f.videoItemId);
        auto shiftedVisual = *initialVideo;
        shiftedVisual.sourceRange = ytp::TimeRange{ytp::Rational{1, 1}, ytp::Rational{3, 1}};
        check(ytp::MediaCache::timelineVisualKey(*initialVideo) !=
                  ytp::MediaCache::timelineVisualKey(shiftedVisual),
              "timeline preview cache changes when a clip source range changes");
        check(t.insertClip(QString::fromStdString(f.clipId), QString::fromStdString(f.videoTrackId), 6'000, 0),
              "controller inserts linked clips");
        f.project.flushRecoveryJournal();
        t.select(QString::fromStdString(f.videoItemId));
        check(t.selectedIds().size() == 2, "single click expands linked A/V selection");
        check(t.isRippleMoveFollower(QString::fromStdString(f.videoItemId)) == false,
              "the selected event is never classified as its own ripple follower");
        t.cancelPlaybackPreviewRendering();
        check(t.renameItem(QString::fromStdString(f.videoItemId), QStringLiteral("Reaction close-up")),
              "a timeline event can be renamed independently of its reusable source clip");
        bool renamedVideoVisible=false,renamedAudioVisible=false;
        for(const auto&value:t.items()){
            const auto item=value.toMap();
            if(item.value("itemId").toString()==QString::fromStdString(f.videoItemId))
                renamedVideoVisible=item.value("name").toString()==QStringLiteral("Reaction close-up");
            if(item.value("itemId").toString()==QString::fromStdString(f.audioItemId))
                renamedAudioVisible=item.value("name").toString()==QStringLiteral("Reaction close-up");
        }
        check(renamedVideoVisible&&renamedAudioVisible,
              "renaming either half replaces the generated title across the linked timeline section");
        check(t.moveSelected(1'000), "controller moves selected clips");
        check(t.duplicateSelected(), "controller duplicates selected clips");
        t.copySelected();
        check(t.paste(10'000), "controller copy/paste creates timeline events");
        t.selectAll(); check(!t.selectedIds().isEmpty(), "select all exposes all events");
        t.clearSelection(); check(t.selectedIds().isEmpty(), "clear selection empties selection");
        t.selectBox(0, 20'000, 0, 3); check(!t.selectedIds().isEmpty(), "box selection covers track/time range");
        t.selectFollowing(QString::fromStdString(f.videoItemId)); check(!t.selectedIds().isEmpty(), "select following works");
        t.selectSameSource(QString::fromStdString(f.videoItemId)); check(!t.selectedIds().isEmpty(), "select same source works");
        t.clearSelection(); t.select(QString::fromStdString(f.videoItemId));
        check(t.splitSelected(2'000), "controller splits linked clips");
        check(t.setFades(QString::fromStdString(f.videoItemId), 100, 150), "controller sets fades");
        check(t.slip(QString::fromStdString(f.videoItemId), 500), "controller slips source media");
    }

    {
        Fixture f;
        auto& t = *f.timeline;
        t.select(QString::fromStdString(f.videoItemId));
        check(t.addMarker(5'000, QStringLiteral("Snap target")), "snap target marker is created");
        check(t.snapMove(990) == 1'000,
              "moving a linked selection can snap its trailing edge to a marker");
        t.setSnapping(false);
        check(t.snapMove(990) == 990, "disabled snapping preserves a proposed selection move");
        const auto rows = t.timelineRows();
        qsizetype rowItemCount = 0;
        for (const auto& row : rows) rowItemCount += row.toMap().value("items").toList().size();
        check(rows.size() == t.tracks().size() && rowItemCount == t.items().size(),
              "per-track timeline rows contain each visible clip exactly once");
    }

    {
        Fixture f;
        auto& t = *f.timeline;
        check(t.insertClip(QString::fromStdString(f.clipId), QString::fromStdString(f.videoTrackId),
                           6'000, static_cast<int>(ytp::EditMode::Overwrite)),
              "ripple-delete fixture inserts a downstream linked clip");
        t.select(QString::fromStdString(f.videoItemId));
        t.setPlayheadMs(8'000);
        qint64 compactedBy = 0;
        QObject::connect(&t, &ytp::TimelineController::timelineCompacted,
                         [&compactedBy](qint64 amount) { compactedBy = amount; });
        check(t.deleteSelected(), "controller deletes the selected linked clip");
        check(t.playheadMs() == 4'000 && compactedBy == 4'000,
              "ripple deletion keeps the playhead and viewport offset relative to downstream content");
    }

    {
        Fixture f;
        auto& t = *f.timeline;
        check(t.addTrack(0, QStringLiteral("V3")), "video track can be added");
        QString addedTrack;
        for (const auto& value : t.tracks()) {
            const auto map = value.toMap(); if (map.value("name").toString() == "V3") addedTrack = map.value("trackId").toString();
        }
        check(!addedTrack.isEmpty(), "added track is exposed by the model");
        const auto beforeMove = t.tracks();
        const auto firstTrack = beforeMove.front().toMap().value("trackId").toString();
        check(t.moveTrack(firstTrack, 1), "tracks can be moved down");
        const auto afterMove = t.tracks();
        check(afterMove.at(1).toMap().value("trackId").toString() == firstTrack,
              "moving a track changes its visible order");
        bool normalizedOrder = true;
        for (qsizetype index = 0; index < afterMove.size(); ++index)
            normalizedOrder = normalizedOrder && afterMove.at(index).toMap().value("order").toInt() == index;
        check(normalizedOrder, "track order values are normalized after reordering");
        check(!t.moveTrack(afterMove.front().toMap().value("trackId").toString(), -1),
              "moving the top track farther up is a no-op");
        check(t.setTrackState(addedTrack, QStringLiteral("height"), 120), "track height is editable");
        check(t.setTrackState(addedTrack, QStringLiteral("color"), QStringLiteral("#123456")), "track color is editable");
        check(t.removeTrack(addedTrack), "empty track can be removed");
        check(t.setTrackState(QString::fromStdString(f.audioTrackId), QStringLiteral("muted"), true), "track mute is editable");
        check(t.addMarker(1'500, QStringLiteral("Beat")), "marker can be added");
        const auto markerId = t.markers().front().toMap().value("markerId").toString();
        check(t.removeMarker(markerId), "marker can be removed");
        t.setSnapping(false); check(t.snap(1'234) == 1'234, "disabled snapping preserves time");
        t.setPixelsPerSecond(120); check(t.pixelsPerSecond() == 120, "timeline zoom is editable");
        t.setPixelsPerSecond(10'000); check(t.pixelsPerSecond() == 4'000, "timeline zoom reaches frame-detail scale");
        t.setPixelsPerSecond(1); check(t.pixelsPerSecond() == 5, "timeline zoom clamps to a useful overview scale");
        t.setRippleMode(2); check(t.rippleMode() == 2, "ripple mode is editable");
        check(!t.configureShortcuts({{"split","S"},{"delete","S"}}), "duplicate shortcuts are rejected");
        check(t.configureShortcuts({{"split","K"},{"delete","Delete"}}), "valid shortcuts are stored");
        check(t.shortcut("split") == "K", "stored shortcut is returned");
        t.useVegasShortcuts(); check(t.shortcut("split") == "S", "VEGAS shortcut defaults restore");
        t.setVisibleRange(500, 2'500); check(t.visibleStartMs() == 500 && t.visibleEndMs() == 2'500,
                                             "visible timeline range is tracked");
    }

    {
        Fixture f;
        auto& t = *f.timeline;
        t.select(QString::fromStdString(f.videoItemId));
        check(t.setTransformValue("positionX", 42), "position transform is editable");
        check(t.setTransformValue("cropLeft", .1), "crop transform is editable");
        check(t.setTransformFlag("flipHorizontal", true), "transform flags are editable");
        check(t.addTransformKeyframe("positionX", 500, 80, 2), "transform keyframe is added");
        check(t.setClipAudio(-6, .25), "clip audio updates linked audio event");
        check(t.setSpeed(1.5, true), "speed updates linked events");
        check(t.setPitch(4), "pitch updates linked audio event");
        check(t.setReverse(true), "reverse updates linked events");
        const auto& sequence = f.project.project().sequences().front();
        const auto* video = sequence.findItem(f.videoItemId);
        const auto* audio = sequence.findItem(f.audioItemId);
        check(video && audio && video->speed == 1.5 && audio->speed == 1.5 && video->reverse && audio->reverse,
              "linked speed/reverse remain synchronized");
        check(audio && audio->audio.gainDb == -6 && audio->pitchSemitones == 4,
              "gain and pitch reach the rendered audio event");
        check(t.setFreeze(true, 750), "freeze frame is editable");
        check(t.setTrackAudio(QString::fromStdString(f.audioTrackId), -3, -.2), "track audio is editable");
        check(t.setMasterAudio(-1, .1), "master audio is editable");
        check(t.setMasterLimiter(false), "master limiter is editable");
        check(t.addAudioKeyframe(0, QString::fromStdString(f.audioItemId), "gain", 500, -4, 1),
              "clip audio envelope keyframe is added");
        check(t.addEffect(0, QString::fromStdString(f.videoItemId), "screen_shake"), "clip effect is added");
        auto effects = t.inspector().value("effects").toList();
        check(!effects.isEmpty(), "effect appears in inspector");
        const auto effect = effects.front().toMap();
        const auto effectId = effect.value("id").toString();
        const auto parameter = effect.value("parameters").toList().front().toMap().value("name").toString();
        check(t.setEffectParameter(0, QString::fromStdString(f.videoItemId), effectId, parameter, 4),
              "effect parameter is editable");
        check(t.addKeyframe(0, QString::fromStdString(f.videoItemId), effectId, parameter, 500, 3, 1),
              "effect keyframe is added");
        check(t.bypassEffect(0, QString::fromStdString(f.videoItemId), effectId, true), "effect bypass works");
        check(t.resetEffect(0, QString::fromStdString(f.videoItemId), effectId), "effect reset works");
        check(t.saveEffectPreset("Surface preset"), "effect preset is saved");
        check(t.applyEffectPreset("Surface preset"), "effect preset is applied");
        check(t.removeEffectPreset("Surface preset"), "effect preset is removed");
        const auto appliedEffectId = t.inspector().value("effects").toList().front().toMap().value("id").toString();
        check(t.removeEffect(0, QString::fromStdString(f.videoItemId), appliedEffectId), "clip effect is removed");
        check(t.addMask(0), "mask is added");
        const auto maskId = t.inspector().value("masks").toList().front().toMap().value("id").toString();
        check(t.updateMask(maskId, .1, .2, .5, .4, .1, .8, true), "mask geometry is editable");
        check(t.removeMask(maskId), "mask is removed");
        check(t.setCaption(true, "But wait!", 54, "#ffffff"), "caption is editable");
        t.setPreviewQuality(3);
        check(t.availableEffects().size() >= 10 && t.mixerTracks().size() >= 3,
              "effect catalog and mixer surface are populated");
        check(t.toggleEffectFavorite(QStringLiteral("scanlines")),
              "effects can be saved to the browser's Favorites filter");
        bool scanlinesFavorite=false;
        for(const auto&entry:t.availableEffects()){
            const auto map=entry.toMap();
            if(map.value("type").toString()==QStringLiteral("scanlines"))
                scanlinesFavorite=map.value("favorite").toBool();
        }
        check(scanlinesFavorite,"favorite state is reflected in effect metadata");
        check(t.toggleEffectFavorite(QStringLiteral("scanlines")),"effect favorite can be removed");
        check(t.startLivePreview(0),"continuous Program preview can be started from the controller");
        t.stopLivePreview();
        check(t.livePreviewUrl().isEmpty(),"stopping continuous Program preview releases its source");
    }

    {
        Fixture f;
        auto& t=*f.timeline;
        check(t.insertClip(QString::fromStdString(f.clipId),QString::fromStdString(f.videoTrackId),4'000,1),
              "adjacent clip is inserted for trim/roll tests");
        const auto& sequence=f.project.project().sequences().front();
        const auto& videos=sequence.tracks[1].items;
        check(videos.size()==2,"two adjacent video events exist");
        const auto left=QString::fromStdString(videos[0].id),right=QString::fromStdString(videos[1].id);
        check(t.roll(left,right,4'500),"roll edit moves a shared boundary");
        check(t.trimStart(left,500),"trim start is exposed through the controller");
        check(t.trimEnd(left,3'500),"trim end is exposed through the controller");
        t.clearSelection();t.select(left);t.select(right,true);
        check(t.groupSelected(),"selected events can be grouped");
        check(t.ungroupSelected(),"selected events can be ungrouped");
        check(t.unlinkSelected(),"linked events can be unlinked");
        check(t.linkSelected(),"selected events can be linked");
        check(t.deleteSelected(),"selected events can be deleted");
    }

    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.setTransformValue("positionX",123),"attribute source is customized");
        t.copyAttributes();
        check(t.insertClip(QString::fromStdString(f.clipId),QString::fromStdString(f.videoTrackId),6'000,0),"attribute paste target is inserted");
        const auto target=t.inspector().value("itemId").toString();
        check(t.pasteAttributes(true,true,true,true),"copied clip attributes can be pasted");
        check(t.addEffect(0,target,"screen_shake"),"keyframe-removal effect is added");
        auto effect=t.inspector().value("effects").toList().front().toMap();
        auto parameter=effect.value("parameters").toList().front().toMap();
        check(t.addKeyframe(0,target,effect.value("id").toString(),parameter.value("name").toString(),250,2,1),"removable effect key is added");
        effect=t.inspector().value("effects").toList().front().toMap();parameter=effect.value("parameters").toList().front().toMap();
        const auto keyId=parameter.value("keyframes").toList().front().toMap().value("id").toString();
        check(t.removeKeyframe(0,target,effect.value("id").toString(),parameter.value("name").toString(),keyId),"effect keyframe can be removed");
    }

    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.buildRapidReverse(4,200),"Rapid Reverse controller path works");
    }
    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.buildFrameRepeat(2,3),"Frame Repeat controller path works");
    }
    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.buildRhythmRepeat(0,120,4,100,true),"Rhythm Repeat controller path works");
    }
    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.buildSpeedLadder(4,.5,2,1,false),"Speed Ladder controller path works");
    }
    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.applySafeEarrape(.5),"safe audio destruction controller path works");
        check(t.ytpAudioPresets().size()>=6,"creative audio packs are exposed to QML");
        check(t.applyYtpAudioPreset("robot_radio"),"creative audio pack controller path works");
    }
    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.ytpCombinedPresets().size()>=30,"synchronized audio and visual mutations are exposed to QML");
        check(t.applyYtpCombinedPreset("reality_collapse"),"combined audio and visual mutation controller path works");
    }
    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        check(t.addMarker(2'000,"Word cut"),"Sentence Mixer cut marker is added");
        check(t.buildSentenceMixer("0,0,0"),"Sentence Mixer controller path works");
    }

    {
        Fixture f;auto&t=*f.timeline;
        const auto original=t.activeSequenceId();
        check(t.createSequence("Secondary"),"sequence can be created");
        const auto secondary=t.activeSequenceId();
        check(secondary!=original&&t.switchSequence(original),"sequence can be switched");
        check(t.createAdjustmentClip(QString::fromStdString(f.videoTrackId),8'000,1'000),"adjustment clip can be created");
        check(t.insertNestedSequence(secondary,QString::fromStdString(f.videoTrackId),10'000)==false,
              "empty nested sequences are rejected");
        check(t.removeSequence(secondary),"unused secondary sequence can be removed");
        t.select(QString::fromStdString(f.videoItemId));
        check(t.configureBeatGrid(120,0,4,true),"beat grid can be configured");
        check(t.applyBeatTool(2,"screen_shake","amount"),"beat-aware effect keys can be generated");
    }

    {
        Fixture f;auto&t=*f.timeline;t.select(QString::fromStdString(f.videoItemId));
        t.startMacroRecording();check(t.buildStutter(3,100,false),"recorded macro has an operation");
        check(t.saveRecordedMacro("Surface macro"),"recorded macro is saved");
        check(t.loadMacroEditor("Surface macro"),"saved macro loads in the visual editor");
        t.addMacroEditorStep(5);check(t.macroEditorSteps().size()==2,"macro editor step is added");
        check(t.moveMacroEditorStep(1,-1),"macro editor step can move");
        check(t.removeMacroEditorStep(1),"macro editor step can be removed");
        check(t.saveVisualMacro("Visual macro"),"visual macro is saved");
        check(t.previewMacroVariations("Visual macro",3,42)&&t.macroVariationPreviews().size()==3,
              "macro variations can be previewed");
        check(t.applyMacroScope("Visual macro",0,1,42),"macro can be applied to selection scope");
        t.cancelMacroRecording();
    }

    std::cout << "Timeline controller selection, editing, tracks, shortcuts, linked A/V, effects, masks, captions, and mixer surface passed.\n";
    return failures == 0 ? 0 : 1;
}
