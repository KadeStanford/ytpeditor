#include "ui/project_controller.h"
#include "commands/timeline_commands.h"

#include "commands/library_commands.h"
#include "media/media_cache.h"
#include "media/media_probe.h"
#include "media/media_analysis.h"
#include "timeline/timeline_editor.h"
#include "ytp/remix_toolkit.h"
#include "persistence/project_serializer.h"
#include "persistence/session_serializer.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>

namespace ytp {

bool ProjectController::applySequenceEdit(Sequence before, Sequence after, std::string description) {
    if (after.validate()) return false;
    auto candidate = project_;
    candidate.updateSequence(after);
    if (candidate.validate()) return false;
    commands_.execute(std::make_unique<UpdateSequenceCommand>(project_, std::move(before),
                                                              std::move(after), std::move(description)));
    setDirty(true);
    setStatus(QStringLiteral("Timeline updated."));
    emit projectChanged();
    emit historyChanged();
    return true;
}
bool ProjectController::applySequencesEdit(std::vector<Sequence> before,std::vector<Sequence> after,std::string description){
    auto candidate=project_;try{candidate.setSequences(after);}catch(...){return false;}if(candidate.validate())return false;
    commands_.execute(std::make_unique<UpdateSequencesCommand>(project_,std::move(before),std::move(after),std::move(description)));
    setDirty(true);setStatus(QStringLiteral("Sequences updated."));emit projectChanged();emit historyChanged();return true;
}
bool ProjectController::applyProjectEdit(Project before,Project after,std::string description){if(after.validate())return false;commands_.execute(std::make_unique<UpdateProjectCommand>(project_,std::move(before),std::move(after),std::move(description)));clipLibrary_.refresh();mediaLibrary_.refresh();setDirty(true);setStatus("Project structure updated.");emit projectChanged();emit historyChanged();return true;}
namespace {

struct MediaWorkResult final {
    std::optional<MediaProbeResult> probe;
    QVector<qint64> frameTimestamps;
    QString error;
    QString cacheWarning;
    bool waveformGenerated{false};
};

qint64 toMilliseconds(const Rational& value) {
    return static_cast<qint64>(value.asLongDouble() * 1'000.0L);
}

std::string fingerprintForFile(const QFileInfo& file) {
    return QStringLiteral("%1:%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()).toStdString();
}

bool mediaMatchesFingerprint(const MediaAsset& asset) {
    const QFileInfo file(QString::fromStdString(asset.path));
    return file.exists() && (asset.fingerprint.empty() || asset.fingerprint == fingerprintForFile(file));
}

QString recoveryPath(const QString& projectPath) { return projectPath + QStringLiteral(".recovery.ytps"); }
QString manualBackupPath(const QString& projectPath) { return projectPath + QStringLiteral(".manual.ytps"); }
QString journalPath(){const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);QDir{}.mkpath(directory);return directory+QStringLiteral("/untitled-session.recovery.ytps");}

void removeAutosaves(const QString& projectPath) {
    QFile::remove(recoveryPath(projectPath));
    for (int generation = 1; generation <= 3; ++generation)
        QFile::remove(projectPath + QStringLiteral(".autosave.%1").arg(generation));
}

MediaWorkResult analyzeMedia(const QString& path, const QString& cacheKey,
                             const bool generateWaveform) {
    MediaWorkResult work;
    work.probe = MediaProbe::probe(path, &work.error);
    if (!work.probe) {
        return work;
    }
    work.frameTimestamps = MediaProbe::frameTimestampsMs(path, &work.cacheWarning);
    if (generateWaveform) {
        QString waveformError;
        work.waveformGenerated = MediaCache::generateWaveform(path, cacheKey, &waveformError);
        if (!work.waveformGenerated && work.cacheWarning.isEmpty()) {
            work.cacheWarning = waveformError;
        }
    }
    return work;
}

std::vector<std::string> parseTags(const QString& value) {
    std::vector<std::string> tags;
    for (const auto& part : value.split(',', Qt::SkipEmptyParts)) {
        const auto tag = part.trimmed();
        if (tag.isEmpty()) {
            continue;
        }
        const auto asString = tag.toStdString();
        if (std::find(tags.begin(), tags.end(), asString) == tags.end()) {
            tags.push_back(asString);
        }
    }
    return tags;
}

} // namespace

ProjectController::ProjectController(QObject* parent)
    : QObject(parent), clipLibrary_(this), mediaLibrary_(this) {
    firstRunTutorialVisible_ = !QSettings{}.value(QStringLiteral("onboarding/completed"), false).toBool();
    clipLibrary_.setProject(&project_);
    mediaLibrary_.setProject(&project_);
    const int requestedInterval = qEnvironmentVariableIntValue("YTP_AUTOSAVE_INTERVAL_MS");
    autosaveTimer_.setInterval(requestedInterval > 0 ? requestedInterval : 30'000);
    connect(&autosaveTimer_, &QTimer::timeout, this, &ProjectController::autosave);
    autosaveTimer_.start();
    journalTimer_.setSingleShot(true);journalTimer_.setInterval(750);connect(&journalTimer_,&QTimer::timeout,this,&ProjectController::writeJournal);journalAvailable_=QFileInfo::exists(journalPath());
    thumbnailDebounce_.setSingleShot(true);
    thumbnailDebounce_.setInterval(350);
    connect(&thumbnailDebounce_, &QTimer::timeout, this, &ProjectController::generateMissingThumbnails);
    connect(this, &ProjectController::projectChanged, &thumbnailDebounce_, qOverload<>(&QTimer::start));

    QTimer::singleShot(0, this, [this] {
        const auto lastProject = QSettings{}.value(QStringLiteral("lastProjectPath")).toString();
        if (!lastProject.isEmpty()) {
            checkRecoveryFor(lastProject);
        }
    });
}

void ProjectController::dismissFirstRunTutorial() {
    if (!firstRunTutorialVisible_) return;
    firstRunTutorialVisible_ = false;
    QSettings{}.setValue(QStringLiteral("onboarding/completed"), true);
    emit firstRunTutorialChanged();
}

void ProjectController::showFirstRunTutorial() {
    if (firstRunTutorialVisible_) return;
    firstRunTutorialVisible_ = true;
    emit firstRunTutorialChanged();
}

QString ProjectController::projectName() const {
    return QString::fromStdString(project_.name());
}

void ProjectController::newProject() {
    ++importGeneration_;
    commands_.clear();
    project_ = Project{};
    projectFilePath_.clear();
    sessionState_.clear();
    sessionStateDirty_ = false;
    currentMediaId_.clear();
    frameTimestampsMs_.clear();
    missingMediaIds_.clear();
    missingMediaName_.clear();
    recoveryAvailable_=false;recoveryAutosavePath_.clear();recoveryProjectPath_.clear();
    transcriptResults_.clear();wordResults_.clear();transcribing_=false;
    clipLibrary_.setProject(&project_);
    mediaLibrary_.setProject(&project_);
    selectSource(nullptr);
    setBusy(false);
    setDirty(false);
    setStatus(QStringLiteral("New project created."));
    emit projectChanged();
    emit historyChanged();
    emit missingMediaChanged();
    emit recoveryAvailableChanged();
    emit transcriptChanged();
    emit sessionStateChanged();
    emit sessionRestoreRequested();
}

void ProjectController::updateSessionState(const QVariantMap& values) {
    bool changed=false;
    for(auto it=values.cbegin();it!=values.cend();++it)if(sessionState_.value(it.key())!=it.value()){
        sessionState_.insert(it.key(),it.value());changed=true;
    }
    if(changed){sessionStateDirty_=true;if(dirty_)journalTimer_.start();emit sessionStateChanged();}
}

bool ProjectController::importMedia(const QUrl& fileUrl) {
    if (busy_) {
        setStatus(QStringLiteral("Wait for the current media task to finish."));
        return false;
    }
    const auto path = fileUrl.toLocalFile();
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) {
        setStatus(QStringLiteral("The selected media file does not exist."));
        return false;
    }

    const Id assetId = createId();
    const int generation = ++importGeneration_;
    setBusy(true);
    setStatus(QStringLiteral("Importing and indexing %1 in the background…").arg(file.fileName()));

    auto* watcher = new QFutureWatcher<MediaWorkResult>(this);
    connect(watcher, &QFutureWatcher<MediaWorkResult>::finished, this,
            [this, watcher, path, assetId, generation] {
        const auto work = watcher->result();
        watcher->deleteLater();
        if (generation != importGeneration_) {
            setBusy(false);
            return;
        }
        setBusy(false);
        if (!work.probe) {
            setStatus(QStringLiteral("Import failed: ") + work.error);
            return;
        }

        const QFileInfo file(path);
        MediaAsset asset{
            .id = assetId,
            .path = path.toStdString(),
            .displayName = file.fileName().toStdString(),
            .duration = work.probe->duration,
            .frameRateNumerator = work.probe->frameRateNumerator,
            .frameRateDenominator = work.probe->frameRateDenominator,
            .width = work.probe->width,
            .height = work.probe->height,
            .audioSampleRate = work.probe->audioSampleRate,
            .fingerprint = fingerprintForFile(file),
            .createdAtMs = QDateTime::currentMSecsSinceEpoch(),
            .lastUsedAtMs = QDateTime::currentMSecsSinceEpoch()
        };
        const bool seedFirstTimeline = project_.mediaAssets().empty() &&
            std::all_of(project_.sequences().begin(), project_.sequences().end(), [](const Sequence& sequence) {
                return std::all_of(sequence.tracks.begin(), sequence.tracks.end(),
                    [](const Track& track) { return track.items.empty(); });
            });
        if(seedFirstTimeline&&work.probe->width>0&&work.probe->height>0){
            auto settings=project_.settings();settings.width=work.probe->width;settings.height=work.probe->height;
            if(work.probe->frameRateNumerator>0&&work.probe->frameRateDenominator>0){settings.frameRateNumerator=work.probe->frameRateNumerator;settings.frameRateDenominator=work.probe->frameRateDenominator;}
            if(work.probe->audioSampleRate>0)settings.audioSampleRate=work.probe->audioSampleRate;
            project_.setSettings(settings);
        }
        currentMediaId_ = asset.id;
        project_.addMediaAsset(std::move(asset));
        mediaLibrary_.refresh();
        frameTimestampsMs_.clear();
        selectSource(currentMedia());
        bool placedOnTimeline = false;
        if (seedFirstTimeline && currentMedia() && currentMedia()->duration > Rational{} &&
            !project_.sequences().empty()) {
            const auto* media = currentMedia();
            const auto now = QDateTime::currentMSecsSinceEpoch();
            LibraryClip fullSource{
                .id = createId(),
                .mediaAssetId = media->id,
                .sourceRange = TimeRange{Rational{}, media->duration},
                .name = file.completeBaseName().toStdString(),
                .thumbnailTime = Rational{},
                .createdAtMs = now,
                .lastUsedAtMs = now
            };
            const auto clipId = fullSource.id;
            const auto thumbnailTime = fullSource.thumbnailTime;
            project_.addLibraryClip(std::move(fullSource));
            auto* sequence = project_.findSequence(project_.sequences().front().id);
            if (sequence) {
                const bool hasVideo = media->width > 0 && media->height > 0;
                const auto preferredName = hasVideo ? std::string_view{"V1"} : std::string_view{"A1"};
                auto target = std::find_if(sequence->tracks.begin(), sequence->tracks.end(),
                    [hasVideo, preferredName](const Track& track) {
                        const auto kind = hasVideo ? TrackKind::Video : TrackKind::Audio;
                        return track.kind == kind && track.name == preferredName;
                    });
                if (target == sequence->tracks.end()) {
                    target = std::find_if(sequence->tracks.begin(), sequence->tracks.end(),
                        [hasVideo](const Track& track) {
                            return track.kind == (hasVideo ? TrackKind::Video : TrackKind::Audio);
                        });
                }
                if (target != sequence->tracks.end()) {
                    const auto inserted = TimelineEditor::insertLibraryClip(
                        project_, *sequence, clipId, target->id, Rational{}, EditMode::Overwrite);
                    placedOnTimeline = !inserted.itemIds.empty();
                }
            }
            clipLibrary_.refresh();
            auto* thumbnailWatcher = new QFutureWatcher<bool>(this);
            connect(thumbnailWatcher, &QFutureWatcher<bool>::finished, this, [this, thumbnailWatcher] {
                const bool created=thumbnailWatcher->result();thumbnailWatcher->deleteLater();
                clipLibrary_.refresh();
                mediaLibrary_.refresh();
                if(created)emit mediaCacheChanged();
            });
            thumbnailWatcher->setFuture(QtConcurrent::run([path, thumbnailTime, clipId] {
                QString error;
                return MediaCache::generateThumbnail(path, thumbnailTime,
                                                     QString::fromStdString(clipId), &error);
            }));
        }
        startSourceAnalysis(*currentMedia());
        setDirty(true);
        setStatus(placedOnTimeline
            ? QStringLiteral("Imported %1 and placed the full source on V1/A1; analysis is building in the background.").arg(file.fileName())
            : QStringLiteral("Imported %1 to Project Media; analysis is building in the background.").arg(file.fileName()));
        emit projectChanged();
    });
    watcher->setFuture(QtConcurrent::run([path] {
        MediaWorkResult work;
        work.probe = MediaProbe::probe(path, &work.error);
        return work;
    }));
    return true;
}

bool ProjectController::createLibraryClip(const QString& name, qint64 inMs, qint64 outMs) {
    const auto* media = currentMedia();
    if (!media) {
        setStatus(QStringLiteral("Import and select source media first."));
        return false;
    }
    return createLibraryClipForMedia(QString::fromStdString(media->id), name, inMs, outMs);
}

bool ProjectController::createLibraryClipForMedia(const QString& mediaId, const QString& name,
                                                  qint64 inMs, qint64 outMs) {
    const auto* media = project_.findMediaAsset(mediaId.toStdString());
    if (!media) {
        setStatus(QStringLiteral("The selected timeline clip has no available source media."));
        return false;
    }
    inMs = std::max<qint64>(0, inMs);
    outMs = std::min(outMs, toMilliseconds(media->duration));
    if (outMs <= inMs) {
        setStatus(QStringLiteral("Out must be later than In."));
        return false;
    }

    const Rational start{inMs, 1'000};
    const Rational duration{outMs - inMs, 1'000};
    const Rational thumbnailTime = start;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    LibraryClip clip{
        .id = createId(),
        .mediaAssetId = media->id,
        .sourceRange = TimeRange{start, duration},
        .name = name.trimmed().isEmpty()
            ? QStringLiteral("Clip %1").arg(project_.libraryClips().size() + 1).toStdString()
            : name.trimmed().toStdString(),
        .thumbnailTime = thumbnailTime,
        .createdAtMs = now,
        .lastUsedAtMs = now
    };
    const auto clipId = clip.id;
    const auto mediaPath = QString::fromStdString(media->path);
    commands_.execute(std::make_unique<AddLibraryClipCommand>(project_, std::move(clip)));
    clipLibrary_.refresh();
    setDirty(true);
    setStatus(QStringLiteral("Created reusable library clip; thumbnail is rendering…"));
    emit historyChanged();
    emit projectChanged();

    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
        const bool created = watcher->result();
        watcher->deleteLater();
        clipLibrary_.refresh();
        setStatus(created ? QStringLiteral("Reusable library clip is ready.")
                          : QStringLiteral("Clip created, but its thumbnail failed."));
        if(created)emit mediaCacheChanged();
    });
    watcher->setFuture(QtConcurrent::run([mediaPath, thumbnailTime, clipId] {
        QString error;
        return MediaCache::generateThumbnail(mediaPath, thumbnailTime,
                                             QString::fromStdString(clipId), &error);
    }));
    return true;
}

bool ProjectController::addCurrentSourceToTimeline() {
    const auto* media = currentMedia();
    if (!media || project_.sequences().empty()) {
        setStatus(QStringLiteral("Import and select source media first."));
        return false;
    }
    auto* sequence = project_.findSequence(project_.sequences().front().id);
    if (!sequence || std::any_of(sequence->tracks.begin(), sequence->tracks.end(),
                                 [](const Track& track) { return !track.items.empty(); })) {
        setStatus(QStringLiteral("The timeline already contains clips; drag additional media from the library."));
        return false;
    }

    auto fullClip = std::find_if(project_.libraryClips().begin(), project_.libraryClips().end(),
        [media](const LibraryClip& clip) {
            return clip.mediaAssetId == media->id && clip.sourceRange.start() == Rational{} &&
                   clip.sourceRange.duration() == media->duration;
        });
    if (fullClip == project_.libraryClips().end()) {
        if (!createLibraryClip(QFileInfo(QString::fromStdString(media->path)).completeBaseName(),
                               0, toMilliseconds(media->duration))) {
            return false;
        }
        fullClip = std::prev(project_.libraryClips().end());
    }

    const bool hasVideo = media->width > 0 && media->height > 0;
    auto before = *sequence;
    auto after = before;
    const auto preferredName = hasVideo ? std::string_view{"V1"} : std::string_view{"A1"};
    auto target = std::find_if(after.tracks.begin(), after.tracks.end(),
        [hasVideo, preferredName](const Track& track) {
            return track.kind == (hasVideo ? TrackKind::Video : TrackKind::Audio) &&
                   track.name == preferredName;
        });
    if (target == after.tracks.end()) {
        target = std::find_if(after.tracks.begin(), after.tracks.end(), [hasVideo](const Track& track) {
            return track.kind == (hasVideo ? TrackKind::Video : TrackKind::Audio);
        });
    }
    if (target == after.tracks.end()) {
        setStatus(QStringLiteral("No compatible timeline track is available."));
        return false;
    }
    const auto inserted = TimelineEditor::insertLibraryClip(
        project_, after, fullClip->id, target->id, Rational{}, EditMode::Overwrite);
    if (inserted.itemIds.empty() ||
        !applySequenceEdit(std::move(before), std::move(after), "Place full source on timeline")) {
        setStatus(QStringLiteral("The full source could not be placed on the timeline."));
        return false;
    }
    setStatus(hasVideo ? QStringLiteral("Placed the full source on V1/A1.")
                       : QStringLiteral("Placed the full source on A1."));
    return true;
}

bool ProjectController::updateLibraryClip(const QString& clipId, const QString& name,
                                          const QString& tags, const QString& notes,
                                          const QString& color, const QString& bin,
                                          const bool favorite, qint64 inMs, qint64 outMs) {
    const auto* existing = project_.findLibraryClip(clipId.toStdString());
    if (!existing) {
        setStatus(QStringLiteral("The selected library clip no longer exists."));
        return false;
    }
    const auto* media = project_.findMediaAsset(existing->mediaAssetId);
    if (!media || name.trimmed().isEmpty() || bin.trimmed().isEmpty()) {
        setStatus(QStringLiteral("Clip name and bin are required."));
        return false;
    }
    inMs = std::max<qint64>(0, inMs);
    outMs = std::min(outMs, toMilliseconds(media->duration));
    if (outMs <= inMs) {
        setStatus(QStringLiteral("Out must be later than In."));
        return false;
    }

    LibraryClip updated = *existing;
    updated.name = name.trimmed().toStdString();
    updated.tags = parseTags(tags);
    updated.notes = notes.toStdString();
    updated.color = color.isEmpty() ? "#df4f8b" : color.toStdString();
    updated.bin = bin.trimmed().toStdString();
    updated.favorite = favorite;
    updated.sourceRange = TimeRange{Rational{inMs, 1'000}, Rational{outMs - inMs, 1'000}};
    updated.thumbnailTime = updated.sourceRange.start();
    const bool rangeChanged = updated.sourceRange != existing->sourceRange;
    const auto before = *existing;
    commands_.execute(std::make_unique<UpdateLibraryClipCommand>(project_, before, updated));
    clipLibrary_.refresh();
    setDirty(true);
    setStatus(QStringLiteral("Updated library clip metadata."));
    emit historyChanged();
    emit projectChanged();

    if (rangeChanged) {
        const auto cacheKey = QString::fromStdString(updated.id);
        QFile::remove(MediaCache::thumbnailPath(cacheKey));
        const auto mediaPath = QString::fromStdString(media->path);
        const auto thumbnailTime = updated.thumbnailTime;
        auto* watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
            const bool created=watcher->result();watcher->deleteLater();
            clipLibrary_.refresh();
            if(created)emit mediaCacheChanged();
        });
        watcher->setFuture(QtConcurrent::run([mediaPath, thumbnailTime, cacheKey] {
            QString error;
            return MediaCache::generateThumbnail(mediaPath, thumbnailTime, cacheKey, &error);
        }));
    }
    return true;
}

bool ProjectController::deleteLibraryClip(const QString& clipId) {
    const auto id = clipId.toStdString();
    const auto* clip = project_.findLibraryClip(id);
    const auto index = project_.libraryClipIndex(id);
    if (!clip || !index) {
        return false;
    }
    commands_.execute(std::make_unique<RemoveLibraryClipCommand>(project_, *clip, *index));
    clipLibrary_.refresh();
    setDirty(true);
    setStatus(QStringLiteral("Deleted library clip. Undo is available."));
    emit historyChanged();
    emit projectChanged();
    return true;
}

bool ProjectController::activateLibraryClip(const QString& clipId) {
    const auto* existing = project_.findLibraryClip(clipId.toStdString());
    if (!existing) {
        return false;
    }
    LibraryClip used = *existing;
    used.lastUsedAtMs = QDateTime::currentMSecsSinceEpoch();
    project_.updateLibraryClip(used);
    const auto* media = project_.findMediaAsset(used.mediaAssetId);
    if (media && media->id != currentMediaId_) {
        frameTimestampsMs_.clear();
        selectSource(media);
        startSourceAnalysis(*media);
    }
    clipLibrary_.refresh();
    setDirty(true);
    const auto sourceInMs=toMilliseconds(used.sourceRange.start());
    const auto sourceOutMs=toMilliseconds(used.sourceRange.end());
    emit seekSourceRequested(sourceInMs);
    emit seekSourceRangeRequested(sourceInMs,sourceOutMs);
    setStatus(QStringLiteral("Loaded clip range into the Source Viewer."));
    return true;
}

bool ProjectController::activateMedia(const QString& mediaId) {
    const auto* existing = project_.findMediaAsset(mediaId.toStdString());
    if (!existing || !QFileInfo::exists(QString::fromStdString(existing->path))) {
        setStatus(QStringLiteral("This media is missing and must be relinked."));
        return false;
    }
    MediaAsset used = *existing;
    used.lastUsedAtMs = QDateTime::currentMSecsSinceEpoch();
    project_.updateMediaAsset(used);
    frameTimestampsMs_.clear();
    selectSource(project_.findMediaAsset(used.id));
    startSourceAnalysis(used);
    mediaLibrary_.refresh();
    setDirty(true);
    setStatus(QStringLiteral("Selected project media."));
    return true;
}

bool ProjectController::setMediaBin(const QString& mediaId, const QString& bin) {
    const auto* existing = project_.findMediaAsset(mediaId.toStdString());
    if (!existing || bin.trimmed().isEmpty()) {
        return false;
    }
    MediaAsset updated = *existing;
    updated.bin = bin.trimmed().toStdString();
    project_.updateMediaAsset(std::move(updated));
    mediaLibrary_.refresh();
    setDirty(true);
    setStatus(QStringLiteral("Updated media bin."));
    emit projectChanged();
    return true;
}

qint64 ProjectController::stepFrame(const qint64 currentPositionMs, const int direction) const {
    if (direction == 0) {
        return currentPositionMs;
    }
    if (!frameTimestampsMs_.isEmpty()) {
        if (direction > 0) {
            const auto next = std::upper_bound(frameTimestampsMs_.begin(), frameTimestampsMs_.end(), currentPositionMs);
            return next == frameTimestampsMs_.end() ? frameTimestampsMs_.back() : *next;
        }
        const auto current = std::lower_bound(frameTimestampsMs_.begin(), frameTimestampsMs_.end(), currentPositionMs);
        if (current == frameTimestampsMs_.begin()) {
            return frameTimestampsMs_.front();
        }
        return *(current - 1);
    }
    const auto* media = currentMedia();
    if (media && media->frameRateNumerator > 0) {
        const auto current=Rational{std::clamp(currentPositionMs,qint64{0},sourceDurationMs_),1'000};
        const auto frame=frameDuration(media->frameRateNumerator,media->frameRateDenominator);
        const auto scaled=current/frame;
        std::int64_t index;
        if(direction>0){index=static_cast<std::int64_t>(std::floor(scaled.asLongDouble()))+1;while(toMilliseconds(timeAtFrame(index,media->frameRateNumerator,media->frameRateDenominator))<=currentPositionMs)++index;}
        else{index=static_cast<std::int64_t>(std::ceil(scaled.asLongDouble()))-1;while(index>0&&toMilliseconds(timeAtFrame(index,media->frameRateNumerator,media->frameRateDenominator))>=currentPositionMs)--index;}
        return std::clamp(toMilliseconds(timeAtFrame(std::max<std::int64_t>(0,index),media->frameRateNumerator,media->frameRateDenominator)),qint64{0},sourceDurationMs_);
    }
    return std::clamp(currentPositionMs + direction * 10, qint64{0}, sourceDurationMs_);
}

bool ProjectController::saveProject(const QUrl& fileUrl) {
    const auto requestedPath = fileUrl.isEmpty() ? projectFilePath_ : fileUrl.toLocalFile();
    if (requestedPath.isEmpty()) {
        setStatus(QStringLiteral("Choose a project file first."));
        return false;
    }
    QString error;
    if (!SessionSerializer::save(project_, sessionState_, requestedPath, &error)) {
        setStatus(QStringLiteral("Save failed: ") + error);
        return false;
    }
    QString backupError;
    const bool backupSaved = SessionSerializer::save(
        project_, sessionState_, manualBackupPath(requestedPath), &backupError);
    projectFilePath_ = requestedPath;
    QSettings{}.setValue(QStringLiteral("lastProjectPath"), projectFilePath_);
    removeAutosaves(projectFilePath_);
    recoveryAvailable_ = false;
    emit recoveryAvailableChanged();
    setDirty(false);
    sessionStateDirty_ = false;
    setStatus(backupSaved
        ? QStringLiteral("Session saved with a manual-save backup.")
        : QStringLiteral("Session saved, but the manual backup failed: ") + backupError);
    return true;
}

bool ProjectController::openProject(const QUrl& fileUrl) {
    QString error;
    const auto path=fileUrl.toLocalFile();
    std::optional<Project> loaded;
    QVariantMap restoredWorkspace;
    if(SessionSerializer::isSessionFile(path)){
        auto session=SessionSerializer::load(path,&error);
        if(session){loaded=std::move(session->project);restoredWorkspace=std::move(session->workspace);}
    }else loaded=ProjectSerializer::load(path,&error);
    if (!loaded) {
        setStatus(QStringLiteral("Open failed: ") + error);
        return false;
    }
    ++importGeneration_;
    commands_.clear();
    project_ = std::move(*loaded);
    sessionState_=std::move(restoredWorkspace);
    projectFilePath_ = fileUrl.toLocalFile();
    QSettings{}.setValue(QStringLiteral("lastProjectPath"), projectFilePath_);
    clipLibrary_.setProject(&project_);
    mediaLibrary_.setProject(&project_);
    frameTimestampsMs_.clear();
    transcriptResults_.clear();wordResults_.clear();transcribing_=false;emit transcriptChanged();
    refreshMissingMedia();
    const auto requestedMediaId=sessionState_.value(QStringLiteral("currentMediaId")).toString().toStdString();
    auto firstAvailable = std::find_if(project_.mediaAssets().begin(), project_.mediaAssets().end(),
        [&](const MediaAsset& asset) { return asset.id==requestedMediaId&&mediaMatchesFingerprint(asset); });
    if(firstAvailable==project_.mediaAssets().end())firstAvailable=std::find_if(project_.mediaAssets().begin(), project_.mediaAssets().end(),
        [](const MediaAsset& asset) { return mediaMatchesFingerprint(asset); });
    if (firstAvailable != project_.mediaAssets().end()) {
        selectSource(&*firstAvailable);
        startSourceAnalysis(*firstAvailable);
    } else {
        selectSource(nullptr);
    }
    generateMissingThumbnails();
    setDirty(false);
    sessionStateDirty_ = false;
    checkRecoveryFor(projectFilePath_);
    setStatus(hasMissingMedia()
        ? QStringLiteral("Project opened with missing media. Relink is required.")
        : QStringLiteral("Project opened."));
    emit projectChanged();
    emit historyChanged();
    emit sessionStateChanged();
    emit sessionRestoreRequested();
    return true;
}

bool ProjectController::relinkMissingMedia(const QUrl& fileUrl) {
    if (busy_ || missingMediaIds_.empty()) {
        return false;
    }
    const auto path = fileUrl.toLocalFile();
    if (!QFileInfo::exists(path)) {
        setStatus(QStringLiteral("Replacement media does not exist."));
        return false;
    }
    const auto assetId = missingMediaIds_.front();
    const auto* original = project_.findMediaAsset(assetId);
    if (!original) {
        refreshMissingMedia();
        return false;
    }
    const auto originalAsset = *original;
    const int generation = ++importGeneration_;
    setBusy(true);
    setStatus(QStringLiteral("Validating replacement media in the background…"));

    auto* watcher = new QFutureWatcher<MediaWorkResult>(this);
    connect(watcher, &QFutureWatcher<MediaWorkResult>::finished, this,
            [this, watcher, path, originalAsset, generation] {
        const auto work = watcher->result();
        watcher->deleteLater();
        if (generation != importGeneration_) {
            return;
        }
        setBusy(false);
        if (!work.probe) {
            setStatus(QStringLiteral("Relink failed: ") + work.error);
            return;
        }
        const QFileInfo file(path);
        MediaAsset replacement = originalAsset;
        replacement.path = path.toStdString();
        replacement.displayName = file.fileName().toStdString();
        replacement.duration = work.probe->duration;
        replacement.frameRateNumerator = work.probe->frameRateNumerator;
        replacement.frameRateDenominator = work.probe->frameRateDenominator;
        replacement.width = work.probe->width;
        replacement.height = work.probe->height;
        replacement.audioSampleRate = work.probe->audioSampleRate;
        replacement.fingerprint = fingerprintForFile(file);
        try {
        project_.updateMediaAsset(std::move(replacement));
        } catch (const std::exception& exception) {
            setStatus(QStringLiteral("Relink rejected: ") + QString::fromUtf8(exception.what()));
            return;
        }
        frameTimestampsMs_.clear();
        mediaLibrary_.refresh();
        refreshMissingMedia();
        selectSource(project_.findMediaAsset(originalAsset.id));
        if (const auto* relinked = project_.findMediaAsset(originalAsset.id)) {
            startSourceAnalysis(*relinked);
        }
        setDirty(true);
        setStatus(QStringLiteral("Media relinked successfully."));
        emit projectChanged();
    });
    watcher->setFuture(QtConcurrent::run([path] {
        MediaWorkResult work;
        work.probe = MediaProbe::probe(path, &work.error);
        return work;
    }));
    return true;
}

void ProjectController::recoverAutosave() {
    if (!recoveryAvailable_) {
        return;
    }
    loadRecoveredProject(recoveryAutosavePath_, recoveryProjectPath_);
}

void ProjectController::discardRecovery() {
    if (!recoveryProjectPath_.isEmpty()) {
        removeAutosaves(recoveryProjectPath_);
    }
    recoveryAvailable_ = false;
    recoveryAutosavePath_.clear();
    recoveryProjectPath_.clear();
    emit recoveryAvailableChanged();
    setStatus(QStringLiteral("Recovery copies discarded."));
}

void ProjectController::reportPlaybackError(const QString& message) {
    if (!message.trimmed().isEmpty()) {
        setStatus(QStringLiteral("Playback error: ") + message.trimmed());
    }
}

void ProjectController::undo() {
    if (commands_.undo()) {
        clipLibrary_.refresh();
        setDirty(true);
        setStatus(QStringLiteral("Undid last edit."));
        emit projectChanged();
        emit historyChanged();
    }
}

void ProjectController::redo() {
    if (commands_.redo()) {
        clipLibrary_.refresh();
        setDirty(true);
        setStatus(QStringLiteral("Redid last edit."));
        emit projectChanged();
        emit historyChanged();
    }
}

bool ProjectController::generateProxy(const QString& mediaId) {
    const auto* media=project_.findMediaAsset(mediaId.toStdString());
    if(!media)return false;
    const auto id=mediaId;const auto path=QString::fromStdString(media->path);
    setStatus(QStringLiteral("Generating edit proxy in the background…"));
    auto* watcher=new QFutureWatcher<bool>(this);
    connect(watcher,&QFutureWatcher<bool>::finished,this,[this,watcher,id]{
        const bool ok=watcher->result();watcher->deleteLater();
        if(ok){if(const auto* current=project_.findMediaAsset(id.toStdString())){auto updated=*current;updated.proxyPath=MediaCache::proxyPath(id).toStdString();updated.proxyReady=true;project_.updateMediaAsset(std::move(updated));setDirty(true);mediaLibrary_.refresh();emit projectChanged();}setStatus(QStringLiteral("Proxy ready."));}
        else setStatus(QStringLiteral("Proxy generation failed."));
    });
    watcher->setFuture(QtConcurrent::run([path,id]{return MediaCache::generateProxy(path,id); }));
    return true;
}

void ProjectController::setDirty(const bool value) {
    if(value)journalTimer_.start();
    if (dirty_ == value) {
        return;
    }
    dirty_ = value;
    if(!value){journalTimer_.stop();if(QFile::exists(journalPath()))QFile::remove(journalPath());if(journalAvailable_){journalAvailable_=false;emit journalChanged();}}
    emit dirtyChanged();
}

void ProjectController::writeJournal(){if(!dirty_)return;QString error;if(SessionSerializer::save(project_,sessionState_,journalPath(),&error)){if(!journalAvailable_){journalAvailable_=true;emit journalChanged();}}else setStatus("Recovery journal failed: "+error);}
void ProjectController::flushRecoveryJournal(){journalTimer_.stop();writeJournal();}

void ProjectController::setBusy(const bool value) {
    if (busy_ == value) {
        return;
    }
    busy_ = value;
    emit busyChanged();
}

void ProjectController::setStatus(QString message) {
    if (statusMessage_ == message) {
        return;
    }
    statusMessage_ = std::move(message);
    emit statusMessageChanged();
}

void ProjectController::selectSource(const MediaAsset* asset) {
    sourceWaveformUrl_ = QUrl{};
    if (!asset) {
        currentMediaId_.clear();
        sourceUrl_ = QUrl{};
        sourceName_.clear();
        sourceDurationMs_ = 0;
        frameTimestampsMs_.clear();
    } else {
        currentMediaId_ = asset->id;
        sourceUrl_ = QUrl::fromLocalFile(QString::fromStdString(asset->path));
        sourceName_ = QString::fromStdString(asset->displayName);
        sourceDurationMs_ = toMilliseconds(asset->duration);
        const auto waveform = MediaCache::waveformPath(QString::fromStdString(asset->id));
        if (QFileInfo::exists(waveform)) {
            sourceWaveformUrl_ = QUrl::fromLocalFile(waveform);
        }
    }
    const auto selectedId = QString::fromStdString(currentMediaId_);
    if (sessionState_.value(QStringLiteral("currentMediaId")).toString() != selectedId) {
        sessionState_.insert(QStringLiteral("currentMediaId"), selectedId);
        sessionStateDirty_ = true;
        emit sessionStateChanged();
    }
    emit sourceChanged();
    emit sourceWaveformChanged();
}

void ProjectController::startSourceAnalysis(const MediaAsset& asset) {
    if (!QFileInfo::exists(QString::fromStdString(asset.path))) {
        return;
    }
    const auto path = QString::fromStdString(asset.path);
    const auto assetId = asset.id;
    auto* watcher = new QFutureWatcher<MediaWorkResult>(this);
    connect(watcher, &QFutureWatcher<MediaWorkResult>::finished, this,
            [this, watcher, assetId] {
        const auto work = watcher->result();
        watcher->deleteLater();
        if (currentMediaId_ != assetId) {
            return;
        }
        frameTimestampsMs_ = work.frameTimestamps;
        if (work.waveformGenerated) {
            sourceWaveformUrl_ = QUrl::fromLocalFile(
                MediaCache::waveformPath(QString::fromStdString(assetId)));
            emit sourceWaveformChanged();
            emit mediaCacheChanged();
        }
        setStatus(work.cacheWarning.isEmpty()
            ? QStringLiteral("Source waveform and frame index are ready.")
            : QStringLiteral("Source analysis completed with a warning: ") + work.cacheWarning);
    });
    watcher->setFuture(QtConcurrent::run([path, assetId] {
        return analyzeMedia(path, QString::fromStdString(assetId), true);
    }));
}

void ProjectController::generateMissingThumbnails(){
    struct Work{enum class Kind{Library,TimelineVideo,TimelineAudio};Kind kind;QString path;Rational time;TimeRange range;QString key;};std::vector<Work> missing;
    for(const auto&clip:project_.libraryClips()){
        const auto key=QString::fromStdString(clip.id);if(QFileInfo::exists(MediaCache::thumbnailPath(key)))continue;
        const auto*media=project_.findMediaAsset(clip.mediaAssetId);if(!media||!QFileInfo::exists(QString::fromStdString(media->path)))continue;
        missing.push_back({Work::Kind::Library,QString::fromStdString(media->path),clip.sourceRange.start(),clip.sourceRange,key});
    }
    for(const auto&sequence:project_.sequences())for(const auto&track:sequence.tracks)for(const auto&item:track.items){
        if(item.adjustmentClip||!item.nestedSequenceId.empty())continue;
        const auto*media=project_.findMediaAsset(item.mediaAssetId);if(!media||!QFileInfo::exists(QString::fromStdString(media->path)))continue;
        const auto key=MediaCache::timelineVisualKey(item);
        const bool video=track.kind==TrackKind::Video;
        const auto output=video?MediaCache::timelineThumbnailPath(key):MediaCache::timelineWaveformPath(key);
        if(QFileInfo::exists(output))continue;
        missing.push_back({video?Work::Kind::TimelineVideo:Work::Kind::TimelineAudio,
                           QString::fromStdString(media->path),item.sourceRange.start(),item.sourceRange,key});
    }
    if(missing.empty())return;const auto generation=importGeneration_;auto*watcher=new QFutureWatcher<int>(this);
    connect(watcher,&QFutureWatcher<int>::finished,this,[this,watcher,generation]{const int created=watcher->result();watcher->deleteLater();if(generation!=importGeneration_)return;if(created>0){clipLibrary_.refresh();mediaLibrary_.refresh();emit mediaCacheChanged();}});
    watcher->setFuture(QtConcurrent::run([missing=std::move(missing)]{int created=0;for(const auto&work:missing){QString error;bool ok=false;if(work.kind==Work::Kind::Library)ok=MediaCache::generateThumbnail(work.path,work.time,work.key,&error);else if(work.kind==Work::Kind::TimelineVideo)ok=MediaCache::generateTimelineThumbnail(work.path,work.range,work.key,&error);else ok=MediaCache::generateTimelineWaveform(work.path,work.range,work.key,&error);if(ok)++created;}return created;}));
}

void ProjectController::autosave() {
    if ((!dirty_ && !sessionStateDirty_) || projectFilePath_.isEmpty()) {
        return;
    }
    QString error;
    if (!SessionSerializer::save(project_, sessionState_, recoveryPath(projectFilePath_), &error)) {
        setStatus(QStringLiteral("Autosave failed: ") + error);
    } else {
        sessionStateDirty_ = false;
    }
}

void ProjectController::refreshMissingMedia() {
    missingMediaIds_.clear();
    missingMediaName_.clear();
    for (const auto& asset : project_.mediaAssets()) {
        if (!mediaMatchesFingerprint(asset)) {
            missingMediaIds_.push_back(asset.id);
        }
    }
    if (!missingMediaIds_.empty()) {
        if (const auto* asset = project_.findMediaAsset(missingMediaIds_.front())) {
            missingMediaName_ = QString::fromStdString(asset->displayName);
        }
    }
    mediaLibrary_.refresh();
    emit missingMediaChanged();
}

void ProjectController::checkRecoveryFor(const QString& projectPath) {
    const auto recovery = recoveryPath(projectPath);
    const QFileInfo recoveryInfo(recovery);
    const QFileInfo projectInfo(projectPath);
    const bool available = recoveryInfo.exists() &&
        (!projectInfo.exists() || recoveryInfo.lastModified() > projectInfo.lastModified());
    recoveryAvailable_ = available;
    recoveryAutosavePath_ = available ? recovery : QString{};
    recoveryProjectPath_ = available ? projectPath : QString{};
    emit recoveryAvailableChanged();
}

void ProjectController::loadRecoveredProject(const QString& recoveryPath,
                                             const QString& originalPath) {
    QString error;
    auto session = SessionSerializer::load(recoveryPath, &error);
    if (!session) {
        setStatus(QStringLiteral("Recovery failed: ") + error);
        return;
    }
    ++importGeneration_;
    commands_.clear();
    project_ = std::move(session->project);
    sessionState_ = std::move(session->workspace);
    projectFilePath_ = originalPath;
    clipLibrary_.setProject(&project_);
    mediaLibrary_.setProject(&project_);
    refreshMissingMedia();
    frameTimestampsMs_.clear();
    const auto firstAvailable = std::find_if(project_.mediaAssets().begin(), project_.mediaAssets().end(),
        [](const MediaAsset& asset) { return mediaMatchesFingerprint(asset); });
    if (firstAvailable != project_.mediaAssets().end()) {
        selectSource(&*firstAvailable);
        startSourceAnalysis(*firstAvailable);
    } else {
        selectSource(nullptr);
    }
    generateMissingThumbnails();
    recoveryAvailable_ = false;
    emit recoveryAvailableChanged();
    setDirty(true);
    setStatus(QStringLiteral("Recovered autosaved changes. Save to keep them."));
    emit projectChanged();
    emit historyChanged();
    emit sessionStateChanged();
    emit sessionRestoreRequested();
}

const MediaAsset* ProjectController::currentMedia() const {
    return project_.findMediaAsset(currentMediaId_);
}

bool ProjectController::transcribeCurrentMedia(const QUrl& modelFile,const QString& language){
    const auto* media=currentMedia();if(!media||transcribing_)return false;const auto id=media->id;const auto path=QString::fromStdString(media->path);const auto model=modelFile.toLocalFile();const auto generation=importGeneration_;
    transcribing_=true;emit transcriptChanged();setStatus(QStringLiteral("Transcribing locally with Whisper…"));auto* watcher=new QFutureWatcher<std::pair<std::vector<TranscriptWord>,QString>>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher,id,model,language,generation]{auto result=watcher->result();watcher->deleteLater();if(generation!=importGeneration_)return;transcribing_=false;if(result.first.empty()){setStatus(QStringLiteral("Transcription failed: ")+result.second);emit transcriptChanged();return;}if(const auto* current=project_.findMediaAsset(id)){auto updated=*current;updated.transcript=std::move(result.first);updated.transcriptionLanguage=language.toStdString();updated.transcriptionModel=QFileInfo(model).fileName().toStdString();project_.updateMediaAsset(std::move(updated));setDirty(true);searchTranscript(QString{});setStatus(QStringLiteral("Local transcript ready and searchable."));emit projectChanged();}emit transcriptChanged();});
    watcher->setFuture(QtConcurrent::run([path,model,language]{QString error;auto words=MediaAnalysis::transcribe(path,model,language,&error);return std::make_pair(std::move(words),error);}));return true;
}

void ProjectController::searchTranscript(const QString& query){transcriptResults_.clear();const auto needle=query.trimmed();for(const auto& media:project_.mediaAssets())for(const auto& word:media.transcript){const auto text=QString::fromStdString(word.text);if(!needle.isEmpty()&&!text.contains(needle,Qt::CaseInsensitive))continue;transcriptResults_.push_back(QVariantMap{{"mediaId",QString::fromStdString(media.id)},{"mediaName",QString::fromStdString(media.displayName)},{"text",text},{"startMs",toMilliseconds(word.start)},{"endMs",toMilliseconds(word.start+word.duration)},{"confidence",word.confidence}});if(transcriptResults_.size()>=500)break;}emit transcriptChanged();}

bool ProjectController::createClipFromTranscript(const QString& mediaId,qint64 startMs,qint64 endMs,const QString& text){const auto* media=project_.findMediaAsset(mediaId.toStdString());if(!media)return false;startMs=std::max<qint64>(0,startMs-80);endMs=std::min(toMilliseconds(media->duration),endMs+120);if(endMs<=startMs)return false;LibraryClip clip{.id=createId(),.mediaAssetId=media->id,.sourceRange=TimeRange{Rational{startMs,1000},Rational{endMs-startMs,1000}},.name=text.trimmed().left(60).toStdString(),.tags={"transcript"},.notes="Created from local transcript",.color="#5aa9e6",.bin="Transcript",.favorite=false,.thumbnailTime=Rational{startMs,1000},.createdAtMs=QDateTime::currentMSecsSinceEpoch(),.lastUsedAtMs=QDateTime::currentMSecsSinceEpoch()};commands_.execute(std::make_unique<AddLibraryClipCommand>(project_,clip));clipLibrary_.refresh();setDirty(true);setStatus(QStringLiteral("Transcript word saved as a reusable clip."));emit projectChanged();emit historyChanged();generateMissingThumbnails();return true;}

void ProjectController::searchWords(const QString&query,bool phonetic){wordResults_.clear();for(const auto&match:RemixToolkit::searchWords(project_,query.toStdString(),phonetic)){const auto*media=project_.findMediaAsset(match.mediaAssetId);wordResults_.push_back(QVariantMap{{"mediaId",QString::fromStdString(match.mediaAssetId)},{"mediaName",media?QString::fromStdString(media->displayName):QString{}},{"text",QString::fromStdString(match.text)},{"startMs",toMilliseconds(match.range.start())},{"endMs",toMilliseconds(match.range.end())},{"score",match.score},{"phonetic",match.score<1}});}emit transcriptChanged();}

void ProjectController::recoverJournal(){QString error;auto session=SessionSerializer::load(journalPath(),&error);if(!session){setStatus("Journal recovery failed: "+error);return;}++importGeneration_;commands_.clear();project_=std::move(session->project);sessionState_=std::move(session->workspace);clipLibrary_.setProject(&project_);mediaLibrary_.setProject(&project_);frameTimestampsMs_.clear();transcriptResults_.clear();wordResults_.clear();transcribing_=false;refreshMissingMedia();const auto requested=sessionState_.value(QStringLiteral("currentMediaId")).toString().toStdString();auto firstAvailable=std::find_if(project_.mediaAssets().begin(),project_.mediaAssets().end(),[&](const auto&asset){return asset.id==requested&&mediaMatchesFingerprint(asset);});if(firstAvailable==project_.mediaAssets().end())firstAvailable=std::find_if(project_.mediaAssets().begin(),project_.mediaAssets().end(),[](const auto&asset){return mediaMatchesFingerprint(asset);});if(firstAvailable!=project_.mediaAssets().end()){selectSource(&*firstAvailable);startSourceAnalysis(*firstAvailable);}else selectSource(nullptr);generateMissingThumbnails();setDirty(true);journalAvailable_=false;QFile::remove(journalPath());emit journalChanged();emit transcriptChanged();emit projectChanged();emit historyChanged();emit sessionStateChanged();emit sessionRestoreRequested();setStatus("Recovered every journaled edit and workspace position. Save the session to keep it.");}
void ProjectController::discardJournal(){journalTimer_.stop();QFile::remove(journalPath());journalAvailable_=false;emit journalChanged();setStatus("Recovery journal discarded.");}
bool ProjectController::archiveProject(const QUrl&destination){const auto output=destination.toLocalFile();if(output.isEmpty()||QFileInfo::exists(output)){setStatus("Choose a new archive destination.");return false;}QTemporaryDir temporary;if(!temporary.isValid())return false;QDir root(temporary.path());root.mkpath("media");root.mkpath("proxies");root.mkpath("fonts");auto archived=project_;for(const auto&asset:project_.mediaAssets()){auto copy=asset;const auto source=QString::fromStdString(asset.path);const auto mediaName=QString::fromStdString(asset.id).left(8)+"-"+QFileInfo(source).fileName();const auto relative="media/"+mediaName;const auto target=root.filePath(relative);if(!QFile::copy(source,target)){setStatus("Archive failed while copying "+QFileInfo(source).fileName());return false;}copy.path=relative.toStdString();copy.fingerprint.clear();if(asset.proxyReady&&QFileInfo::exists(QString::fromStdString(asset.proxyPath))){const auto proxyName=QString::fromStdString(asset.id).left(8)+"-proxy."+QFileInfo(QString::fromStdString(asset.proxyPath)).suffix();const auto proxyRelative="proxies/"+proxyName;if(QFile::copy(QString::fromStdString(asset.proxyPath),root.filePath(proxyRelative))){copy.proxyPath=proxyRelative.toStdString();copy.proxyReady=true;}}archived.updateMediaAsset(std::move(copy));}QString error;if(!SessionSerializer::save(archived,sessionState_,root.filePath("project.ytps"),&error)){setStatus("Archive session copy failed: "+error);return false;}QFile manifest(root.filePath("ARCHIVE_README.txt"));if(manifest.open(QIODevice::WriteOnly|QIODevice::Text)){manifest.write("YTP Editor collected session archive\r\nOpen project.ytps after extraction. Media and proxies use relative paths.\r\nThe fonts directory contains project-local fonts when applicable.\r\n");manifest.close();}const QString systemTar="C:/Windows/System32/tar.exe";auto tar=QFileInfo::exists(systemTar)?systemTar:QStandardPaths::findExecutable("tar");if(tar.isEmpty()){setStatus("Archive compression tool is unavailable.");return false;}QProcess process;process.start(tar,{"-a","-c","-f",output,"-C",temporary.path(),"."});if(!process.waitForStarted(5000)||!process.waitForFinished(60*60*1000)||process.exitCode()!=0){process.kill();QFile::remove(output);setStatus("Archive compression failed: "+QString::fromUtf8(process.readAllStandardError()));return false;}setStatus("Collected session archive created.");return true;}

} // namespace ytp
