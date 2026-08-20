#pragma once

#include "export/export_settings.h"
#include "export/render_engine.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <vector>

namespace ytp {

class ProjectController;
class TimelineController;

class ExportController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY jobsChanged)
public:
    explicit ExportController(ProjectController* projectController,
                              TimelineController* timelineController,
                              QObject* parent = nullptr);

    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] QVariantList jobs() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }

    Q_INVOKABLE bool enqueue(const QUrl& outputUrl, const QString& presetId,
                             qint64 rangeStartMs, qint64 rangeEndMs,
                             bool markedRegion, const QVariantMap& customSettings = {});
    Q_INVOKABLE bool cancel(const QString& jobId);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE bool saveCustomPreset(const QString& name, const QVariantMap& settings);
    Q_INVOKABLE bool removeCustomPreset(const QString& presetId);
    Q_INVOKABLE bool saveSnapshot(const QUrl& outputUrl, qint64 timeMs);
    Q_INVOKABLE QUrl logUrl(const QString& jobId) const;

signals:
    void presetsChanged();
    void jobsChanged();

private:
    enum class State { Queued, Rendering, Complete, Failed, Cancelled };
    struct Job final {
        QString id;
        QString outputPath;
        QString presetName;
        ExportSettings settings;
        State state{State::Queued};
        double progress{0};
        QString message{"Queued"};
        QString logPath;
        Project projectSnapshot;
        std::string sequenceId;
    };
    [[nodiscard]] std::vector<ExportPreset> allPresets() const;
    void startNext();
    void persistHistory() const;
    void restoreHistory();
    static QString stateName(State state);

    ProjectController* projectController_;
    TimelineController* timelineController_;
    std::vector<Job> jobs_;
    QString activeJobId_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    QString statusMessage_{"Export queue ready"};
};

} // namespace ytp
