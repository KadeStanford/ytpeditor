#include "ui/export_controller.h"

#include "model/id.h"
#include "ui/project_controller.h"
#include "ui/timeline_controller.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStorageInfo>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ytp {
namespace {
Rational milliseconds(qint64 value) { return Rational{value, 1000}; }

ExportContainer containerFromString(const QString& value) {
    if (value == "webm") return ExportContainer::WebM;
    if (value == "mov") return ExportContainer::Mov;
    if (value == "wav") return ExportContainer::Wav;
    return ExportContainer::Mp4;
}

QString containerName(ExportContainer value) { return QString::fromStdString(exportExtension(value)); }

ExportPreset customPreset(const QString& id, const QString& name, const QVariantMap& map) {
    ExportPreset preset;
    preset.id = id.toStdString(); preset.name = name.toStdString();
    preset.container = containerFromString(map.value("container", "mp4").toString());
    preset.width = map.value("width", 1920).toInt(); preset.height = map.value("height", 1080).toInt();
    preset.videoCodec = map.value("videoCodec", "libx264").toString().toStdString();
    preset.audioCodec = map.value("audioCodec", "aac").toString().toStdString();
    preset.videoBitrateKbps = map.value("videoBitrateKbps", 12000).toInt();
    preset.audioBitrateKbps = map.value("audioBitrateKbps", 320).toInt();
    preset.audioSampleRate = map.value("audioSampleRate", 48000).toInt();
    preset.audioOnly = map.value("audioOnly", preset.container == ExportContainer::Wav).toBool();
    return preset;
}
} // namespace

ExportController::ExportController(ProjectController* projectController, TimelineController* timelineController, QObject* parent)
    : QObject(parent), projectController_(projectController), timelineController_(timelineController) { restoreHistory(); }

std::vector<ExportPreset> ExportController::allPresets() const {
    auto values = exportPresets();
    QSettings settings; settings.beginGroup(QStringLiteral("exportPresets"));
    for (const auto& key : settings.childKeys()) {
        const auto object = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
        values.push_back(customPreset(key, object.value("name").toString(key), object.toVariantMap()));
    }
    return values;
}

QVariantList ExportController::presets() const {
    QVariantList result;
    for (const auto& preset : allPresets()) result.push_back(QVariantMap{
        {"id", QString::fromStdString(preset.id)}, {"name", QString::fromStdString(preset.name)},
        {"container", containerName(preset.container)}, {"width", preset.width}, {"height", preset.height},
        {"videoCodec", QString::fromStdString(preset.videoCodec)}, {"audioCodec", QString::fromStdString(preset.audioCodec)},
        {"videoBitrateKbps", preset.videoBitrateKbps}, {"audioBitrateKbps", preset.audioBitrateKbps},
        {"audioSampleRate", preset.audioSampleRate}, {"audioOnly", preset.audioOnly},
        {"custom", QString::fromStdString(preset.id).startsWith("custom_")}});
    return result;
}

QString ExportController::stateName(State state) {
    switch (state) { case State::Queued: return "Queued"; case State::Rendering: return "Rendering";
    case State::Complete: return "Complete"; case State::Failed: return "Failed"; case State::Cancelled: return "Cancelled"; }
    return "Unknown";
}

QVariantList ExportController::jobs() const {
    QVariantList result;
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) result.push_back(QVariantMap{
        {"jobId", it->id}, {"outputPath", it->outputPath}, {"fileName", QFileInfo(it->outputPath).fileName()},
        {"preset", it->presetName}, {"state", stateName(it->state)}, {"progress", it->progress},
        {"message", it->message}, {"logPath", it->logPath},
        {"active", it->state == State::Rendering}, {"finished", it->state == State::Complete || it->state == State::Failed || it->state == State::Cancelled}});
    return result;
}

bool ExportController::busy() const { return !activeJobId_.isEmpty(); }

bool ExportController::enqueue(const QUrl& outputUrl, const QString& presetId, qint64 rangeStartMs,
                               qint64 rangeEndMs, bool markedRegion, const QVariantMap& customSettings) {
    if (!outputUrl.isLocalFile() || projectController_->project().sequences().empty()) { statusMessage_ = "Choose a local output file."; emit jobsChanged(); return false; }
    auto presets = allPresets();
    auto found = std::find_if(presets.begin(), presets.end(), [&](const auto& value) { return QString::fromStdString(value.id) == presetId; });
    if (found == presets.end()) { statusMessage_ = "The selected export preset no longer exists."; emit jobsChanged(); return false; }
    auto preset = *found;
    if (!customSettings.isEmpty()) preset = customPreset(presetId, QString::fromStdString(preset.name), customSettings);
    QString outputPath = outputUrl.toLocalFile();
    const auto suffix = QString::fromStdString(exportExtension(preset.container));
    if (!outputPath.endsWith('.' + suffix, Qt::CaseInsensitive)) outputPath += '.' + suffix;
    if (QFileInfo::exists(outputPath)) { statusMessage_ = "Output already exists. Choose another filename."; emit jobsChanged(); return false; }
    ExportSettings settings{preset, markedRegion ? ExportRange::MarkedRegion : ExportRange::EntireSequence,
                            milliseconds(rangeStartMs), milliseconds(rangeEndMs), outputPath.toStdString(), false, false};
    const auto& project = projectController_->project();
    const auto* sequence = project.findSequence(timelineController_->activeSequenceId().toStdString());
    if (!sequence) sequence = &project.sequences().front();
    if (const auto error = settings.validate(sequence->duration())) { statusMessage_ = QString::fromStdString(*error); emit jobsChanged(); return false; }
    std::unordered_set<std::string> requiredMedia;
    std::unordered_set<std::string> visitedSequences;
    std::function<void(const Sequence&)> collectMedia = [&](const Sequence& current) {
        if (!visitedSequences.insert(current.id).second) return;
        for (const auto& track : current.tracks) for (const auto& item : track.items) {
            if (!item.mediaAssetId.empty()) requiredMedia.insert(item.mediaAssetId);
            if (const auto* nested = project.findSequence(item.nestedSequenceId)) collectMedia(*nested);
        }
    };
    collectMedia(*sequence);
    for (const auto& mediaId : requiredMedia) {
        const auto* media = project.findMediaAsset(mediaId);
        if (!media || !QFileInfo::exists(QString::fromStdString(media->path))) {
            statusMessage_ = QStringLiteral("Missing source media: %1").arg(media ? QString::fromStdString(media->displayName) : QString::fromStdString(mediaId)); emit jobsChanged(); return false;
        }
    }
    const auto durationSeconds = static_cast<double>((markedRegion ? settings.rangeEnd - settings.rangeStart : sequence->duration()).asLongDouble());
    const auto expected = static_cast<qint64>(std::max(8.0, (preset.videoBitrateKbps + preset.audioBitrateKbps) * durationSeconds / 8.0 * 1.25) * 1024.0);
    QStorageInfo storage(QFileInfo(outputPath).absolutePath());
    if (storage.isValid() && storage.bytesAvailable() < expected) { statusMessage_ = "Not enough free disk space for this render."; emit jobsChanged(); return false; }
    jobs_.push_back(Job{QString::fromStdString(createId()), outputPath, QString::fromStdString(preset.name), settings});
    jobs_.back().projectSnapshot = project;
    jobs_.back().sequenceId = sequence->id;
    statusMessage_ = QStringLiteral("Queued %1").arg(QFileInfo(outputPath).fileName());
    persistHistory(); emit jobsChanged(); startNext(); return true;
}

void ExportController::startNext() {
    if (!activeJobId_.isEmpty()) return;
    auto found = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& job) { return job.state == State::Queued; });
    if (found == jobs_.end()) return;
    found->state = State::Rendering; found->message = "Starting FFmpeg"; activeJobId_ = found->id;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto jobId = found->id; const auto settings = found->settings;
    const auto project = found->projectSnapshot; const auto* queuedSequence = project.findSequence(found->sequenceId);
    if (!queuedSequence) {
        found->state = State::Failed; found->message = "The queued sequence no longer exists.";
        activeJobId_.clear(); cancellation_.reset(); persistHistory(); emit jobsChanged(); startNext(); return;
    }
    const auto sequence = *queuedSequence;
    statusMessage_ = QStringLiteral("Rendering %1").arg(QFileInfo(found->outputPath).fileName()); emit jobsChanged();
    auto* watcher = new QFutureWatcher<RenderResult>(this);
    connect(watcher, &QFutureWatcher<RenderResult>::finished, this, [this, watcher, jobId] {
        const auto result = watcher->result();
        if (auto foundJob = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& job) { return job.id == jobId; }); foundJob != jobs_.end()) {
            foundJob->logPath = result.logPath;
            foundJob->progress = result.success ? 1.0 : foundJob->progress;
            foundJob->state = result.success ? State::Complete : result.cancelled ? State::Cancelled : State::Failed;
            foundJob->message = result.success ? QStringLiteral("Render complete") : result.error;
            statusMessage_ = result.success ? QStringLiteral("Export complete: %1").arg(QFileInfo(foundJob->outputPath).fileName()) : foundJob->message;
        }
        activeJobId_.clear(); cancellation_.reset(); watcher->deleteLater(); persistHistory(); emit jobsChanged(); startNext();
    });
    const auto cancel = cancellation_;
    watcher->setFuture(QtConcurrent::run([project, sequence, settings, cancel, this, jobId] {
        return RenderEngine::render(project, sequence, settings, *cancel, [this, jobId](double progress, const QString& message) {
            QMetaObject::invokeMethod(this, [this, jobId, progress, message] {
                const auto job = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& value) { return value.id == jobId; });
                if (job != jobs_.end()) { job->progress = progress; job->message = message; emit jobsChanged(); }
            }, Qt::QueuedConnection);
        });
    }));
}

bool ExportController::cancel(const QString& jobId) {
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& job) { return job.id == jobId; });
    if (found == jobs_.end() || (found->state != State::Queued && found->state != State::Rendering)) return false;
    if (found->state == State::Rendering && cancellation_) cancellation_->store(true);
    else { found->state = State::Cancelled; found->message = "Cancelled before rendering"; persistHistory(); emit jobsChanged(); }
    return true;
}

void ExportController::clearFinished() {
    std::erase_if(jobs_, [](const auto& job) { return job.state == State::Complete || job.state == State::Failed || job.state == State::Cancelled; });
    persistHistory(); emit jobsChanged();
}

bool ExportController::saveCustomPreset(const QString& name, const QVariantMap& settings) {
    if (name.trimmed().isEmpty()) return false;
    const auto id = QStringLiteral("custom_%1").arg(QString::fromStdString(createId()));
    auto object = QJsonObject::fromVariantMap(settings); object.insert("name", name.trimmed());
    QSettings store; store.beginGroup(QStringLiteral("exportPresets"));
    store.setValue(id, QJsonDocument(object).toJson(QJsonDocument::Compact));
    store.endGroup(); store.sync();
    emit presetsChanged(); return true;
}

bool ExportController::removeCustomPreset(const QString& presetId) {
    if (!presetId.startsWith("custom_")) return false;
    QSettings store; store.beginGroup(QStringLiteral("exportPresets"));
    if (!store.contains(presetId)) { store.endGroup(); return false; }
    store.remove(presetId); store.endGroup(); store.sync(); emit presetsChanged(); return true;
}

bool ExportController::saveSnapshot(const QUrl& outputUrl, qint64 timeMs) {
    if (!outputUrl.isLocalFile() || projectController_->project().sequences().empty()) return false;
    auto path = outputUrl.toLocalFile(); if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    QString error;
    const auto& project = projectController_->project();
    const auto* sequence = project.findSequence(timelineController_->activeSequenceId().toStdString());
    if (!sequence) sequence = &project.sequences().front();
    const auto success = RenderEngine::snapshot(project, *sequence, milliseconds(timeMs), path, &error);
    statusMessage_ = success ? QStringLiteral("Snapshot saved: %1").arg(QFileInfo(path).fileName()) : error; emit jobsChanged(); return success;
}

QUrl ExportController::logUrl(const QString& jobId) const {
    const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto& job) { return job.id == jobId; });
    return found == jobs_.end() || found->logPath.isEmpty() ? QUrl{} : QUrl::fromLocalFile(found->logPath);
}

void ExportController::persistHistory() const {
    QJsonArray array;
    for (const auto& job : jobs_) if (job.state != State::Queued && job.state != State::Rendering)
        array.append(QJsonObject{{"id", job.id}, {"output", job.outputPath}, {"preset", job.presetName}, {"state", static_cast<int>(job.state)}, {"message", job.message}, {"log", job.logPath}});
    QSettings{}.setValue("export/history", QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void ExportController::restoreHistory() {
    const auto array = QJsonDocument::fromJson(QSettings{}.value("export/history").toByteArray()).array();
    for (const auto& value : array) { const auto object = value.toObject(); Job job; job.id=object.value("id").toString(); job.outputPath=object.value("output").toString(); job.presetName=object.value("preset").toString(); const auto state=object.value("state").toInt(static_cast<int>(State::Failed)); job.state=state>=static_cast<int>(State::Complete)&&state<=static_cast<int>(State::Cancelled)?static_cast<State>(state):State::Failed; job.message=object.value("message").toString(); job.logPath=object.value("log").toString(); job.progress=job.state==State::Complete?1.0:0.0; jobs_.push_back(std::move(job)); }
}

} // namespace ytp
