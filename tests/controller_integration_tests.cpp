#include "persistence/project_serializer.h"
#include "persistence/session_serializer.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <functional>
#include <iostream>
#include <numeric>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool waitUntil(const std::function<bool()>& condition, const int timeoutMs = 30'000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents();
    return condition();
}

bool createFixture(const QString& path) {
    auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        ffmpeg = QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe");
    }
    QProcess process;
    process.start(ffmpeg, {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("testsrc2=size=320x180:rate=30:duration=3"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("sine=frequency=440:sample_rate=48000:duration=3"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
        QStringLiteral("ultrafast"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path
    });
    return process.waitForStarted(5'000) && process.waitForFinished(30'000) && process.exitCode() == 0;
}

bool createAudioFixture(const QString& path) {
    auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) ffmpeg = QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe");
    QProcess process;
    process.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                           "-i", "sine=frequency=330:sample_rate=48000:duration=2", path});
    return process.waitForStarted(5'000) && process.waitForFinished(30'000) && process.exitCode() == 0;
}

QVariant role(const ytp::ClipLibraryModel* model, const int roleId, const int row = 0) {
    return model->data(model->index(row), roleId);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("YTPEditorTests"));
    QCoreApplication::setOrganizationName(QStringLiteral("YTPEditorTests"));

    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory is available");
    qputenv("YTP_CACHE_DIR", directory.filePath(QStringLiteral("cache")).toUtf8());
    qputenv("YTP_AUTOSAVE_INTERVAL_MS", "50");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    const auto mediaPath = directory.filePath(QStringLiteral("source.mp4"));
    check(createFixture(mediaPath), "synthetic A/V fixture is generated");

    ytp::ProjectController controller;
    check(controller.importMedia(QUrl::fromLocalFile(mediaPath)), "background media import starts");
    check(waitUntil([&] { return !controller.busy() && !controller.sourceUrl().isEmpty(); }),
          "background media import and frame indexing finish");
    check(controller.sourceName() == QStringLiteral("source.mp4"), "import selects the source");
    check(controller.mediaLibrary()->rowCount() == 1, "import appears in the distinct Project Media model");
    check(controller.clipLibrary()->rowCount() == 1, "first import creates a reusable full-source clip");
    const auto& seededSequence = controller.project().sequences().front();
    const auto seededItems = std::accumulate(seededSequence.tracks.begin(), seededSequence.tracks.end(), 0,
        [](const int count, const ytp::Track& track) { return count + static_cast<int>(track.items.size()); });
    check(seededItems == 2, "first A/V import places linked video and audio events on the timeline");
    const auto mediaId = controller.mediaLibrary()->data(
        controller.mediaLibrary()->index(0), ytp::MediaLibraryModel::IdRole).toString();
    check(controller.setMediaBin(mediaId, QStringLiteral("Infomercials")),
          "project media can be organized into bins");
    controller.mediaLibrary()->setBinFilter(QStringLiteral("Infomercials"));
    check(controller.mediaLibrary()->rowCount() == 1, "Project Media bin filtering works");
    controller.mediaLibrary()->setBinFilter(QStringLiteral("Other"));
    check(controller.mediaLibrary()->rowCount() == 0, "Project Media hides other bins");
    controller.mediaLibrary()->setBinFilter({});
    check(controller.sourceDurationMs() >= 2'990 && controller.sourceDurationMs() <= 3'010,
          "probe reports the three-second source duration");
    check(waitUntil([&] {
        return !controller.sourceWaveformUrl().isEmpty() &&
               QFileInfo::exists(controller.sourceWaveformUrl().toLocalFile());
    }),
          "background import creates a waveform cache image");
    check(waitUntil([&] {
        const auto thumbnail = controller.mediaLibrary()->data(
            controller.mediaLibrary()->index(0), ytp::MediaLibraryModel::ThumbnailUrlRole).toUrl();
        return !thumbnail.isEmpty() && QFileInfo::exists(thumbnail.toLocalFile());
    }), "Project Media publishes a scannable source thumbnail");
    check(controller.stepFrame(500, 1) > 500 && controller.stepFrame(500, -1) < 500,
          "frame stepping uses indexed source timestamps");

    ytp::TimelineController previewTimeline(&controller);
    check(previewTimeline.sourceThumbnailUrl(mediaId, 400).isEmpty() &&
              previewTimeline.sourceThumbnailUrl(mediaId, 1'900).isEmpty(),
          "uncached source-time thumbnails are queued without blocking the UI thread");
    QUrl sourceFrameA;
    QUrl sourceFrameB;
    check(waitUntil([&] {
        sourceFrameA = previewTimeline.sourceThumbnailUrl(mediaId, 400);
        sourceFrameB = previewTimeline.sourceThumbnailUrl(mediaId, 1'900);
        return !sourceFrameA.isEmpty() && !sourceFrameB.isEmpty() &&
               QFileInfo::exists(sourceFrameA.toLocalFile()) && QFileInfo::exists(sourceFrameB.toLocalFile());
    }), "timestamp-keyed timeline frames are generated asynchronously on demand");
    const QImage sourceImageA(sourceFrameA.toLocalFile());
    const QImage sourceImageB(sourceFrameB.toLocalFile());
    check(sourceFrameA != sourceFrameB && !sourceImageA.isNull() && !sourceImageB.isNull() &&
              sourceImageA != sourceImageB,
          "different source timestamps use distinct cache entries and distinct frames");
    int playheadSignals = 0;
    int programPreviewSignals = 0;
    QObject::connect(&previewTimeline, &ytp::TimelineController::playheadChanged,
                     [&playheadSignals] { ++playheadSignals; });
    QObject::connect(&previewTimeline, &ytp::TimelineController::programPreviewChanged,
                     [&programPreviewSignals] { ++programPreviewSignals; });
    previewTimeline.setPlayheadMs(1'500);
    check(previewTimeline.playheadMs() == 1'500 && playheadSignals == 1,
          "timeline playhead accepts an in-range preview seek");
    const auto previewSignalsBeforePlaybackTick=programPreviewSignals;
    previewTimeline.setPlaybackPlayheadMs(1'520);
    check(previewTimeline.playheadMs()==1'520&&playheadSignals==2&&programPreviewSignals==previewSignalsBeforePlaybackTick,
          "playback-clock ticks move the playhead without invalidating Program previews");
    previewTimeline.setPlayheadMs(99'000);
    check(previewTimeline.playheadMs() == previewTimeline.durationMs(),
          "timeline playhead clamps seeks to sequence duration");
    previewTimeline.setPlayheadMs(1'000);
    check(waitUntil([&] { return !previewTimeline.programImageUrl().isEmpty(); }, 10'000),
          "timeline seek refreshes the Program frame");
    const auto instantPreview=previewTimeline.instantPreview();
    check(!instantPreview.value("url").toUrl().isEmpty() &&
          std::abs(instantPreview.value("sourcePositionMs").toLongLong()-1'000)<=1 &&
          instantPreview.value("audioEnabled").toBool() && instantPreview.value("exact").toBool(),
          "ordinary linked A/V clips expose an immediate source-synchronized preview mapping");
    check(previewTimeline.renderPlaybackPreview(),
          "active timeline clip starts a rendered playback preview");
    check(waitUntil([&] {
        return !previewTimeline.previewRendering() && !previewTimeline.playbackPreviewUrl().isEmpty();
    }, 20'000), "rendered Program preview becomes playable");
    check(previewTimeline.playbackPreviewStartMs() <= 1'000 &&
          previewTimeline.playbackPreviewStartMs() + previewTimeline.playbackPreviewDurationMs() > 1'000,
          "rendered preview exposes its playhead-relative timeline window");
    const auto cachedPlaybackUrl=previewTimeline.playbackPreviewUrl();
    check(previewTimeline.renderPlaybackPreview() && !previewTimeline.previewRendering() &&
              previewTimeline.playbackPreviewUrl()==cachedPlaybackUrl,
          "revisiting a rendered timeline window reuses it immediately without another render");

    {
        ytp::ProjectController surfaceController;
        check(surfaceController.importMedia(QUrl::fromLocalFile(mediaPath)) &&
              waitUntil([&] { return !surfaceController.busy() && !surfaceController.sourceUrl().isEmpty(); }),
              "project surface fixture imports");
        const auto surfaceMediaId = surfaceController.mediaLibrary()->data(
            surfaceController.mediaLibrary()->index(0), ytp::MediaLibraryModel::IdRole).toString();
        check(surfaceController.activateMedia(surfaceMediaId), "Project Media can be activated in Source");
        surfaceController.dismissFirstRunTutorial();
        surfaceController.showFirstRunTutorial();
        check(surfaceController.firstRunTutorialVisible(), "guided tour can be reopened");
        surfaceController.reportPlaybackError(QStringLiteral("decoder test"));
        check(surfaceController.statusMessage().contains(QStringLiteral("decoder test")),
              "playback errors reach visible project status");
        surfaceController.searchTranscript(QStringLiteral("missing"));
        check(surfaceController.transcriptResults().isEmpty(), "empty transcript search is safe");
        check(surfaceController.createClipFromTranscript(surfaceMediaId, 200, 500, QStringLiteral("Transcript clip")),
              "transcript result can create a reusable clip");
        check(surfaceController.clipLibrary()->rowCount() == 2,
              "transcript-created clip appears beside full source");
        check(surfaceController.transcribeCurrentMedia(QUrl::fromLocalFile(directory.filePath("missing-model.bin"))),
              "local transcription task accepts a selected source");
        check(waitUntil([&] { return !surfaceController.transcribing(); }, 10'000),
              "failed local transcription exits cleanly without hanging");
        surfaceController.discardJournal();
        surfaceController.recoverJournal();
        const auto archivePath = directory.filePath(QStringLiteral("collected-project.zip"));
        check(surfaceController.archiveProject(QUrl::fromLocalFile(archivePath)) && QFileInfo::exists(archivePath),
              "project, media, and metadata collect into an archive");
        surfaceController.discardRecovery();
        check(!surfaceController.recoveryAvailable(), "discard recovery leaves no pending recovery prompt");
    }

    {
        ytp::ProjectController legacyController;
        auto legacy = legacyController.project();
        ytp::MediaAsset media{.id=ytp::createId(),.path=mediaPath.toStdString(),.displayName="Legacy Source",
            .duration=ytp::Rational{3,1},.frameRateNumerator=30,.frameRateDenominator=1,
            .width=320,.height=180,.audioSampleRate=48'000};
        const auto legacyMediaId = media.id;
        legacy.addMediaAsset(std::move(media));
        check(legacyController.applyProjectEdit(legacyController.project(),std::move(legacy),"Legacy media-only project"),
              "legacy media-only state is installed");
        check(legacyController.activateMedia(QString::fromStdString(legacyMediaId)),
              "legacy source becomes current media");
        check(legacyController.addCurrentSourceToTimeline(),
              "empty legacy timeline can place its current full source");
        const auto& sequence = legacyController.project().sequences().front();
        const auto count = std::accumulate(sequence.tracks.begin(),sequence.tracks.end(),0,
            [](const int total,const ytp::Track& track){return total+static_cast<int>(track.items.size());});
        check(count==2,"legacy full-source action creates linked V1/A1 events");
    }

    check(!controller.createLibraryClip(QStringLiteral("Invalid"), 1'500, 500),
          "invalid In/Out selection is rejected");
    check(controller.createLibraryClip(QStringLiteral("Reusable phrase"), 500, 1'500),
          "valid In/Out selection creates a library clip");
    check(controller.clipLibrary()->rowCount() == 2, "marked clip appears alongside the full-source clip");
    const auto createdClipId = role(controller.clipLibrary(), ytp::ClipLibraryModel::IdRole).toString();
    const auto* createdClip = controller.project().findLibraryClip(createdClipId.toStdString());
    check(createdClip && createdClip->thumbnailTime == createdClip->sourceRange.start(),
          "a clipped source thumbnail uses the first frame of its marked range");
    const auto videoTrackId = QString::fromStdString(controller.project().sequences().front().tracks[1].id);
    check(previewTimeline.insertClip(createdClipId, videoTrackId, 3'500, 0),
          "marked source clip inserts as linked timeline video and audio");
    int rangedTimelineItems=0;
    for(const auto&value:previewTimeline.items()){
        const auto item=value.toMap();
        if(item.value("libraryClipId").toString()!=createdClipId)continue;
        ++rangedTimelineItems;
        check(item.value("sourceStartMs").toLongLong()==500&&item.value("durationMs").toLongLong()==1'000,
              "an inserted reusable clip keeps its marked source start and duration instead of expanding to full media");
    }
    check(rangedTimelineItems==2,"the marked source range reaches both linked timeline items");
    const auto insertedTimelineItems = previewTimeline.items();
    const auto insertedTimelineId = insertedTimelineItems.isEmpty()
        ? QString{} : insertedTimelineItems.back().toMap().value("itemId").toString();
    previewTimeline.select(insertedTimelineId, false);
    check(!insertedTimelineId.isEmpty() && previewTimeline.isSelected(insertedTimelineId),
          "constant-time selection lookup reflects the timeline selection");
    const auto libraryCountBeforeSelectionCreate=controller.project().libraryClips().size();
    check(previewTimeline.createLibraryClipFromSelection(QStringLiteral("Selected timeline range"))&&
              controller.project().libraryClips().size()==libraryCountBeforeSelectionCreate+1,
          "a selected timeline segment can be saved directly as a reusable clip");
    if(controller.project().libraryClips().size()==libraryCountBeforeSelectionCreate+1){
        const auto&timelineClip=controller.project().libraryClips().back();
        const auto timelineClipId=QString::fromStdString(timelineClip.id);
        check(timelineClip.sourceRange==ytp::TimeRange{ytp::Rational{1,2},ytp::Rational{1,1}},
              "timeline selection clip creation preserves the selected segment source range");
        check(controller.deleteLibraryClip(timelineClipId),"timeline-selection test clip is removed");
    }
    check(waitUntil([&] {
        QStringList audioWaveforms;
        for (const auto& value : previewTimeline.items()) {
            const auto item = value.toMap();
            if (item.value("kind").toInt() != static_cast<int>(ytp::TrackKind::Audio)) continue;
            const auto url = item.value("waveformUrl").toUrl();
            if (!url.isEmpty() && QFileInfo::exists(url.toLocalFile()) &&
                !audioWaveforms.contains(url.toLocalFile())) audioWaveforms.push_back(url.toLocalFile());
        }
        return audioWaveforms.size() >= 2;
    }), "each timeline audio source range receives its own waveform thumbnail");
    QImage timelineFilmstrip;
    for (const auto& value : previewTimeline.items()) {
        const auto item = value.toMap();
        if (item.value("kind").toInt() != static_cast<int>(ytp::TrackKind::Video)) continue;
        const auto path = item.value("thumbnailUrl").toUrl().toLocalFile();
        if (!path.isEmpty()) { timelineFilmstrip.load(path); break; }
    }
    check(!timelineFilmstrip.isNull() && timelineFilmstrip.width() == 4'096 &&
              timelineFilmstrip.copy(0, 0, 128, 72) !=
                  timelineFilmstrip.copy(timelineFilmstrip.width() - 128, 0, 128, 72),
          "timeline video preview is a changing 32-frame source-relative filmstrip");
    check(waitUntil([&] {
        const auto thumbnail = role(controller.clipLibrary(), ytp::ClipLibraryModel::ThumbnailUrlRole).toUrl();
        return !thumbnail.isEmpty() && QFileInfo::exists(thumbnail.toLocalFile());
    }), "thumbnail renders in the background");
    check(role(controller.clipLibrary(),ytp::ClipLibraryModel::DurationRole,0).toString()!=
              role(controller.clipLibrary(),ytp::ClipLibraryModel::DurationRole,1).toString(),
          "different reusable clip ranges display different precise durations");
    check(waitUntil([&] {
        const auto first=role(controller.clipLibrary(),ytp::ClipLibraryModel::ThumbnailUrlRole,0).toUrl().toLocalFile();
        const auto second=role(controller.clipLibrary(),ytp::ClipLibraryModel::ThumbnailUrlRole,1).toUrl().toLocalFile();
        if(first.isEmpty()||second.isEmpty()||!QFileInfo::exists(first)||!QFileInfo::exists(second))return false;
        QImage firstImage(first),secondImage(second);
        return !firstImage.isNull()&&!secondImage.isNull()&&firstImage!=secondImage;
    }), "reusable clips from different source ranges show different first-frame thumbnails");

    const auto firstClipId = role(controller.clipLibrary(), ytp::ClipLibraryModel::IdRole).toString();
    const auto oldThumbnailUrl = role(controller.clipLibrary(),
                                      ytp::ClipLibraryModel::ThumbnailUrlRole).toUrl();
    check(controller.updateLibraryClip(firstClipId, QStringLiteral("But wait"),
                                       QStringLiteral("speech, sales, speech"),
                                       QStringLiteral("Clean phrase"), QStringLiteral("#ff5b9e"),
                                       QStringLiteral("Salesman"), true, 600, 1'600),
          "clip metadata and source range update through an undoable command");
    check(role(controller.clipLibrary(), ytp::ClipLibraryModel::NameRole).toString() == "But wait" &&
          role(controller.clipLibrary(), ytp::ClipLibraryModel::BinRole).toString() == "Salesman" &&
          role(controller.clipLibrary(), ytp::ClipLibraryModel::FavoriteRole).toBool(),
          "name, bin, and favorite state reach the library model");
    check(role(controller.clipLibrary(), ytp::ClipLibraryModel::TagsRole).toStringList().size() == 2,
          "duplicate tags are normalized");
    check(waitUntil([&] {
        const auto revised = role(controller.clipLibrary(),
                                  ytp::ClipLibraryModel::ThumbnailUrlRole).toUrl();
        return !revised.isEmpty() && revised != oldThumbnailUrl &&
               QFileInfo::exists(revised.toLocalFile());
    }), "editing a clip range changes its thumbnail URL identity and invalidates QML image caching");

    controller.clipLibrary()->setFavoritesOnly(true);
    check(controller.clipLibrary()->rowCount() == 1, "favorite filtering includes favorites");
    controller.clipLibrary()->setBinFilter(QStringLiteral("Other"));
    check(controller.clipLibrary()->rowCount() == 0, "bin filtering hides other bins");
    controller.clipLibrary()->setBinFilter(QStringLiteral("Salesman"));
    check(controller.clipLibrary()->rowCount() == 1, "bin filtering matches case-insensitively");
    controller.clipLibrary()->setFavoritesOnly(false);
    controller.clipLibrary()->setBinFilter({});

    qint64 requestedSeek = -1;
    qint64 requestedRangeIn = -1;
    qint64 requestedRangeOut = -1;
    QObject::connect(&controller, &ytp::ProjectController::seekSourceRequested,
                     [&requestedSeek](const qint64 value) { requestedSeek = value; });
    QObject::connect(&controller, &ytp::ProjectController::seekSourceRangeRequested,
                     [&requestedRangeIn, &requestedRangeOut](const qint64 inMs, const qint64 outMs) {
                         requestedRangeIn = inMs;
                         requestedRangeOut = outMs;
                     });
    check(controller.activateLibraryClip(firstClipId) && requestedSeek == 600 &&
              requestedRangeIn == 600 && requestedRangeOut == 1'600,
          "activating a library clip loads its source range and records recent use");

    check(controller.createLibraryClip(QStringLiteral("Alpha"), 0, 300),
          "second reusable clip is created");
    controller.clipLibrary()->setSortMode(QStringLiteral("Name"));
    check(role(controller.clipLibrary(), ytp::ClipLibraryModel::NameRole).toString() == "Alpha",
          "name sorting is deterministic");
    controller.clipLibrary()->setSortMode(QStringLiteral("Recent"));

    check(controller.deleteLibraryClip(firstClipId), "library clip deletion succeeds");
    check(controller.clipLibrary()->rowCount() == 2, "delete removes one clip");
    controller.undo();
    check(controller.clipLibrary()->rowCount() == 3, "delete undo restores clip and order");
    controller.redo();
    check(controller.clipLibrary()->rowCount() == 2, "delete redo removes it again");
    controller.undo();

    const auto projectPath = directory.filePath(QStringLiteral("roundtrip.ytps"));
    check(controller.saveProject(QUrl::fromLocalFile(projectPath)), "controller saves the project");
    check(QFileInfo::exists(projectPath), "project file exists after save");
    controller.newProject();
    check(controller.clipLibrary()->rowCount() == 0, "new project clears the library");
    check(controller.openProject(QUrl::fromLocalFile(projectPath)), "controller reopens the project");
    check(controller.clipLibrary()->rowCount() == 3, "reopened project restores the library");
    check(controller.sourceUrl() == QUrl::fromLocalFile(mediaPath), "reopened project restores its source path");

    QString loadError;
    const auto loadedSession = ytp::SessionSerializer::load(projectPath, &loadError);
    check(loadedSession.has_value() && loadedSession->project.mediaAssets().front().width == 320 &&
          loadedSession->project.mediaAssets().front().height == 180 &&
          loadedSession->project.mediaAssets().front().audioSampleRate == 48'000,
          "probed video and audio metadata persist");

    const auto reopenedId = role(controller.clipLibrary(), ytp::ClipLibraryModel::IdRole).toString();
    check(controller.updateLibraryClip(reopenedId, QStringLiteral("Autosaved change"),
                                       QString(), QString(), QStringLiteral("#df4f8b"),
                                       QStringLiteral("Clips"), false, 0, 300),
          "post-save edit makes the project dirty");
    const auto latestAutosave = projectPath + QStringLiteral(".recovery.ytps");
    check(waitUntil([&] { return QFileInfo::exists(latestAutosave); }, 3'000),
          "autosave timer writes one compact rolling recovery session");
    check(QFileInfo::exists(projectPath + QStringLiteral(".manual.ytps")),
          "manual save retains a separate baseline backup");

    {
        ytp::ProjectController recoveryController;
        check(waitUntil([&] { return recoveryController.recoveryAvailable(); }, 2'000),
              "startup detects a newer autosave");
        recoveryController.recoverAutosave();
        check(recoveryController.dirty() && recoveryController.clipLibrary()->rowCount() == 3,
              "autosave recovery restores unsaved project state");
        recoveryController.newProject();
    }

    check(controller.saveProject(), "manual save clears the prior rolling recovery");
    controller.updateSessionState({{"playheadMs", 2'222}, {"timelineContentX", 144.0}});
    check(waitUntil([&] { return QFileInfo::exists(latestAutosave); }, 3'000),
          "workspace-only movement also refreshes autosave recovery");
    const auto workspaceRecovery = ytp::SessionSerializer::load(latestAutosave, &loadError);
    check(workspaceRecovery && workspaceRecovery->workspace.value("playheadMs").toLongLong() == 2'222,
          "autosave preserves the latest playhead and viewport state without requiring an edit");
    check(controller.saveProject(), "workspace autosave can be committed as the new manual baseline");

    const auto replacementPath = directory.filePath(QStringLiteral("relocated.mp4"));
    check(waitUntil([&] {
        return QFileInfo::exists(replacementPath) || QFile::rename(mediaPath, replacementPath);
    }, 10'000),
          "test source is moved to simulate missing media");
    check(controller.openProject(QUrl::fromLocalFile(projectPath)), "project opens even with missing media");
    check(controller.hasMissingMedia() && controller.missingMediaName() == "source.mp4",
          "missing media is detected and identified");
    check(controller.relinkMissingMedia(QUrl::fromLocalFile(replacementPath)),
          "background media relink starts");
    check(waitUntil([&] { return !controller.busy() && !controller.hasMissingMedia(); }),
          "replacement media is validated and relinked");
    check(controller.sourceUrl() == QUrl::fromLocalFile(replacementPath),
          "relinked media becomes the active source");

    const auto audioPath = directory.filePath(QStringLiteral("effect.wav"));
    check(createAudioFixture(audioPath), "audio-only fixture is generated");
    ytp::ProjectController audioController;
    check(audioController.importMedia(QUrl::fromLocalFile(audioPath)) &&
          waitUntil([&] { return !audioController.busy() && !audioController.sourceUrl().isEmpty(); }),
          "audio-only media imports and probes");
    check(audioController.createLibraryClip(QStringLiteral("Audio sting"), 200, 1'200),
          "audio-only source range becomes a reusable clip");
    check(waitUntil([&] {
        const auto thumbnail = role(audioController.clipLibrary(),
                                    ytp::ClipLibraryModel::ThumbnailUrlRole).toUrl();
        return !thumbnail.isEmpty() && QFileInfo::exists(thumbnail.toLocalFile());
    }), "audio-only clips receive waveform thumbnails");

    if (failures == 0) {
        std::cout << "All controller integration tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
