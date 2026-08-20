#pragma once

#include "commands/command_stack.h"
#include "model/project.h"
#include "ui/clip_library_model.h"
#include "ui/media_library_model.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QVariantList>

namespace ytp {

class ProjectController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl NOTIFY sourceChanged)
    Q_PROPERTY(QString currentMediaId READ currentMediaId NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(qint64 sourceDurationMs READ sourceDurationMs NOTIFY sourceChanged)
    Q_PROPERTY(QUrl sourceWaveformUrl READ sourceWaveformUrl NOTIFY sourceWaveformChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryAvailableChanged)
    Q_PROPERTY(bool hasMissingMedia READ hasMissingMedia NOTIFY missingMediaChanged)
    Q_PROPERTY(QString missingMediaName READ missingMediaName NOTIFY missingMediaChanged)
    Q_PROPERTY(bool firstRunTutorialVisible READ firstRunTutorialVisible NOTIFY firstRunTutorialChanged)
    Q_PROPERTY(QVariantList transcriptResults READ transcriptResults NOTIFY transcriptChanged)
    Q_PROPERTY(bool transcribing READ transcribing NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantList wordResults READ wordResults NOTIFY transcriptChanged)
    Q_PROPERTY(bool journalAvailable READ journalAvailable NOTIFY journalChanged)
    Q_PROPERTY(QVariantMap sessionState READ sessionState NOTIFY sessionStateChanged)
    Q_PROPERTY(ytp::ClipLibraryModel* clipLibrary READ clipLibrary CONSTANT)
    Q_PROPERTY(ytp::MediaLibraryModel* mediaLibrary READ mediaLibrary CONSTANT)

public:
    explicit ProjectController(QObject* parent = nullptr);

    [[nodiscard]] QString projectName() const;
    [[nodiscard]] QUrl sourceUrl() const { return sourceUrl_; }
    [[nodiscard]] QString currentMediaId() const { return QString::fromStdString(currentMediaId_); }
    [[nodiscard]] QString sourceName() const { return sourceName_; }
    [[nodiscard]] qint64 sourceDurationMs() const { return sourceDurationMs_; }
    [[nodiscard]] QUrl sourceWaveformUrl() const { return sourceWaveformUrl_; }
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] bool canUndo() const noexcept { return commands_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return commands_.canRedo(); }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool recoveryAvailable() const noexcept { return recoveryAvailable_; }
    [[nodiscard]] bool hasMissingMedia() const noexcept { return !missingMediaIds_.empty(); }
    [[nodiscard]] QString missingMediaName() const { return missingMediaName_; }
    [[nodiscard]] bool firstRunTutorialVisible() const noexcept { return firstRunTutorialVisible_; }
    [[nodiscard]] ClipLibraryModel* clipLibrary() noexcept { return &clipLibrary_; }
    [[nodiscard]] MediaLibraryModel* mediaLibrary() noexcept { return &mediaLibrary_; }
    [[nodiscard]] const Project& project() const noexcept { return project_; }
    bool applySequenceEdit(Sequence before, Sequence after, std::string description);
    bool applySequencesEdit(std::vector<Sequence> before, std::vector<Sequence> after, std::string description);
    bool applyProjectEdit(Project before,Project after,std::string description);
    void reportTimelineStatus(const QString& message) { setStatus(message); }
    [[nodiscard]] QVariantList transcriptResults() const { return transcriptResults_; }
    [[nodiscard]] bool transcribing() const noexcept { return transcribing_; }
    [[nodiscard]] QVariantList wordResults() const { return wordResults_; }
    [[nodiscard]] bool journalAvailable() const noexcept { return journalAvailable_; }
    [[nodiscard]] QVariantMap sessionState() const { return sessionState_; }

    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool importMedia(const QUrl& fileUrl);
    Q_INVOKABLE bool addCurrentSourceToTimeline();
    Q_INVOKABLE bool createLibraryClip(const QString& name, qint64 inMs, qint64 outMs);
    bool createLibraryClipForMedia(const QString& mediaId, const QString& name,
                                   qint64 inMs, qint64 outMs);
    Q_INVOKABLE bool saveProject(const QUrl& fileUrl = {});
    Q_INVOKABLE void flushRecoveryJournal();
    Q_INVOKABLE bool openProject(const QUrl& fileUrl);
    Q_INVOKABLE bool updateLibraryClip(const QString& clipId, const QString& name,
                                       const QString& tags, const QString& notes,
                                       const QString& color, const QString& bin,
                                       bool favorite, qint64 inMs, qint64 outMs);
    Q_INVOKABLE bool deleteLibraryClip(const QString& clipId);
    Q_INVOKABLE bool activateLibraryClip(const QString& clipId);
    Q_INVOKABLE bool activateMedia(const QString& mediaId);
    Q_INVOKABLE bool setMediaBin(const QString& mediaId, const QString& bin);
    Q_INVOKABLE qint64 stepFrame(qint64 currentPositionMs, int direction) const;
    Q_INVOKABLE bool relinkMissingMedia(const QUrl& fileUrl);
    Q_INVOKABLE void recoverAutosave();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void reportPlaybackError(const QString& message);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool generateProxy(const QString& mediaId);
    Q_INVOKABLE void dismissFirstRunTutorial();
    Q_INVOKABLE void showFirstRunTutorial();
    Q_INVOKABLE bool transcribeCurrentMedia(const QUrl& modelFile, const QString& language = QStringLiteral("auto"));
    Q_INVOKABLE void searchTranscript(const QString& query);
    Q_INVOKABLE bool createClipFromTranscript(const QString& mediaId, qint64 startMs, qint64 endMs, const QString& text);
    Q_INVOKABLE void searchWords(const QString& query,bool phonetic);
    Q_INVOKABLE void recoverJournal();
    Q_INVOKABLE void discardJournal();
    Q_INVOKABLE bool archiveProject(const QUrl& destination);
    Q_INVOKABLE void updateSessionState(const QVariantMap& values);

signals:
    void projectChanged();
    void sourceChanged();
    void sourceWaveformChanged();
    void statusMessageChanged();
    void dirtyChanged();
    void historyChanged();
    void busyChanged();
    void recoveryAvailableChanged();
    void missingMediaChanged();
    void seekSourceRequested(qint64 positionMs);
    void seekSourceRangeRequested(qint64 inMs, qint64 outMs);
    void firstRunTutorialChanged();
    void transcriptChanged();
    void journalChanged();
    void mediaCacheChanged();
    void sessionStateChanged();
    void sessionRestoreRequested();

private:
    void setDirty(bool value);
    void setStatus(QString message);
    void selectSource(const MediaAsset* asset);
    void autosave();
    void writeJournal();
    void refreshMissingMedia();
    void setBusy(bool value);
    void checkRecoveryFor(const QString& projectPath);
    void loadRecoveredProject(const QString& autosavePath, const QString& originalPath);
    void startSourceAnalysis(const MediaAsset& asset);
    void generateMissingThumbnails();
    [[nodiscard]] const MediaAsset* currentMedia() const;

    Project project_;
    CommandStack commands_;
    ClipLibraryModel clipLibrary_;
    MediaLibraryModel mediaLibrary_;
    Id currentMediaId_;
    QString projectFilePath_;
    QUrl sourceUrl_;
    QString sourceName_;
    qint64 sourceDurationMs_{0};
    QUrl sourceWaveformUrl_;
    QVector<qint64> frameTimestampsMs_;
    QString statusMessage_{"Ready"};
    bool dirty_{false};
    bool busy_{false};
    bool recoveryAvailable_{false};
    QString recoveryAutosavePath_;
    QString recoveryProjectPath_;
    std::vector<Id> missingMediaIds_;
    QString missingMediaName_;
    int importGeneration_{0};
    QTimer autosaveTimer_;
    QTimer journalTimer_;
    QTimer thumbnailDebounce_;
    bool firstRunTutorialVisible_{true};
    QVariantList transcriptResults_;
    QVariantList wordResults_;
    bool transcribing_{false};
    bool journalAvailable_{false};
    QVariantMap sessionState_;
    bool sessionStateDirty_{false};
};

} // namespace ytp
