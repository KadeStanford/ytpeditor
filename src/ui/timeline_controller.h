#pragma once

#include "model/timeline.h"
#include "ytp/ytp_toolkit.h"

#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QTimer>
#include <QQueue>
#include <QSet>
#include <QByteArray>
#include <QPointer>
#include <functional>
#include <atomic>
#include <memory>

class QProcess;
class QIODevice;

namespace ytp {

class ProjectController;

class TimelineController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList timelineRows READ timelineRows NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY timelineChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(qint64 playheadMs READ playheadMs WRITE setPlayheadMs NOTIFY playheadChanged)
    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond WRITE setPixelsPerSecond NOTIFY zoomChanged)
    Q_PROPERTY(bool snapping READ snapping WRITE setSnapping NOTIFY snappingChanged)
    Q_PROPERTY(int rippleMode READ rippleMode WRITE setRippleMode NOTIFY timelineChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
    Q_PROPERTY(QUrl programImageUrl READ programImageUrl NOTIFY programPreviewChanged)
    Q_PROPERTY(QString programLabel READ programLabel NOTIFY programPreviewChanged)
    Q_PROPERTY(QVariantMap instantPreview READ instantPreview NOTIFY programPreviewChanged)
    Q_PROPERTY(QVariantMap inspector READ inspector NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList availableEffects READ availableEffects NOTIFY effectsBrowserChanged)
    Q_PROPERTY(QVariantList mixerTracks READ mixerTracks NOTIFY timelineChanged)
    Q_PROPERTY(QStringList effectPresets READ effectPresets NOTIFY presetsChanged)
    Q_PROPERTY(QUrl playbackPreviewUrl READ playbackPreviewUrl NOTIFY playbackPreviewChanged)
    Q_PROPERTY(qint64 playbackPreviewStartMs READ playbackPreviewStartMs NOTIFY playbackPreviewChanged)
    Q_PROPERTY(qint64 playbackPreviewDurationMs READ playbackPreviewDurationMs NOTIFY playbackPreviewChanged)
    Q_PROPERTY(double projectFrameDurationMs READ projectFrameDurationMs NOTIFY timelineChanged)
    Q_PROPERTY(bool previewRendering READ previewRendering NOTIFY playbackPreviewChanged)
    Q_PROPERTY(QUrl livePreviewUrl READ livePreviewUrl NOTIFY livePreviewChanged)
    Q_PROPERTY(qint64 livePreviewStartMs READ livePreviewStartMs NOTIFY livePreviewChanged)
    Q_PROPERTY(bool livePreviewStarting READ livePreviewStarting NOTIFY livePreviewChanged)
    Q_PROPERTY(qint64 presentedFrameTimestampUs READ presentedFrameTimestampUs NOTIFY presentedFrameChanged)
    Q_PROPERTY(QVariantList ytpVisualPresets READ ytpVisualPresets CONSTANT)
    Q_PROPERTY(QVariantList ytpAudioPresets READ ytpAudioPresets CONSTANT)
    Q_PROPERTY(QVariantList ytpCombinedPresets READ ytpCombinedPresets CONSTANT)
    Q_PROPERTY(QStringList ytpMacros READ ytpMacros NOTIFY toolkitChanged)
    Q_PROPERTY(bool macroRecording READ macroRecording NOTIFY toolkitChanged)
    Q_PROPERTY(int recordedMacroSteps READ recordedMacroSteps NOTIFY toolkitChanged)
    Q_PROPERTY(QVariantMap randomizerPreview READ randomizerPreview NOTIFY toolkitChanged)
    Q_PROPERTY(QVariantList sequences READ sequences NOTIFY timelineChanged)
    Q_PROPERTY(QString activeSequenceId READ activeSequenceId NOTIFY timelineChanged)
    Q_PROPERTY(qint64 visibleStartMs READ visibleStartMs NOTIFY timelineChanged)
    Q_PROPERTY(qint64 visibleEndMs READ visibleEndMs NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList backgroundTasks READ backgroundTasks NOTIFY backgroundTasksChanged)
    Q_PROPERTY(bool programCacheStale READ programCacheStale NOTIFY playbackPreviewChanged)
    Q_PROPERTY(double programCacheProgress READ programCacheProgress NOTIFY playbackPreviewChanged)
    Q_PROPERTY(QVariantMap cacheStats READ cacheStats NOTIFY backgroundTasksChanged)
    Q_PROPERTY(QVariantList compoundClips READ compoundClips NOTIFY timelineChanged)
    Q_PROPERTY(QVariantMap beatGrid READ beatGrid NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList macroEditorSteps READ macroEditorSteps NOTIFY toolkitChanged)
    Q_PROPERTY(QStringList macroVariationPreviews READ macroVariationPreviews NOTIFY toolkitChanged)
    Q_PROPERTY(int timelineThumbnailGeneration READ timelineThumbnailGeneration NOTIFY timelineThumbnailCacheChanged)
public:
    explicit TimelineController(ProjectController* projectController, QObject* parent = nullptr);
    ~TimelineController() override;
    [[nodiscard]] QVariantList tracks() const;
    [[nodiscard]] QVariantList items() const;
    [[nodiscard]] QVariantList timelineRows() const;
    [[nodiscard]] QVariantList markers() const;
    [[nodiscard]] QStringList selectedIds() const { return selectedIds_; }
    Q_INVOKABLE bool isSelected(const QString& itemId) const { return selectedIdSet_.contains(itemId); }
    [[nodiscard]] qint64 playheadMs() const { return playheadMs_; }
    [[nodiscard]] double pixelsPerSecond() const { return pixelsPerSecond_; }
    [[nodiscard]] bool snapping() const { return snapping_; }
    [[nodiscard]] int rippleMode() const;
    [[nodiscard]] qint64 durationMs() const;
    [[nodiscard]] QUrl programImageUrl() const;
    [[nodiscard]] QString programLabel() const;
    [[nodiscard]] QVariantMap instantPreview() const;
    [[nodiscard]] QVariantMap inspector() const;
    [[nodiscard]] QVariantList availableEffects() const;
    [[nodiscard]] QVariantList mixerTracks() const;
    [[nodiscard]] QStringList effectPresets() const;
    [[nodiscard]] QUrl playbackPreviewUrl() const { return playbackPreviewUrl_; }
    [[nodiscard]] qint64 playbackPreviewStartMs() const noexcept { return playbackPreviewStartMs_; }
    [[nodiscard]] qint64 playbackPreviewDurationMs() const noexcept { return playbackPreviewDurationMs_; }
    [[nodiscard]] double projectFrameDurationMs() const;
    [[nodiscard]] bool previewRendering() const { return previewRendering_; }
    [[nodiscard]] QUrl livePreviewUrl() const { return livePreviewUrl_; }
    [[nodiscard]] qint64 livePreviewStartMs() const noexcept { return livePreviewStartMs_; }
    [[nodiscard]] bool livePreviewStarting() const noexcept { return livePreviewStarting_; }
    [[nodiscard]] qint64 presentedFrameTimestampUs() const noexcept { return presentedFrameTimestampUs_; }
    [[nodiscard]] QVariantList ytpVisualPresets() const;
    [[nodiscard]] QVariantList ytpAudioPresets() const;
    [[nodiscard]] QVariantList ytpCombinedPresets() const;
    [[nodiscard]] QStringList ytpMacros() const;
    [[nodiscard]] bool macroRecording() const noexcept { return macroRecording_; }
    [[nodiscard]] int recordedMacroSteps() const noexcept { return static_cast<int>(recordedMacroSteps_.size()); }
    [[nodiscard]] QVariantMap randomizerPreview() const;
    [[nodiscard]] QVariantList sequences() const;
    [[nodiscard]] QString activeSequenceId() const { return activeSequenceId_; }
    [[nodiscard]] qint64 visibleStartMs() const { return visibleStartMs_; }
    [[nodiscard]] qint64 visibleEndMs() const { return visibleEndMs_; }
    [[nodiscard]] QVariantList backgroundTasks() const { return backgroundTasks_; }
    [[nodiscard]] bool programCacheStale() const noexcept { return programCacheStale_; }
    [[nodiscard]] double programCacheProgress() const noexcept { return programCacheProgress_; }
    [[nodiscard]] QVariantMap cacheStats() const;
    [[nodiscard]] QVariantList compoundClips() const;
    [[nodiscard]] QVariantMap beatGrid() const;
    [[nodiscard]] QVariantList macroEditorSteps() const;
    [[nodiscard]] QStringList macroVariationPreviews() const { return macroVariationPreviews_; }
    [[nodiscard]] int timelineThumbnailGeneration() const noexcept { return timelineThumbnailGeneration_; }
    void setPlayheadMs(qint64 value);
    Q_INVOKABLE void setPlaybackPlayheadMs(qint64 value);
    void setPixelsPerSecond(double value);
    void setSnapping(bool value);
    void setRippleMode(int value);

    Q_INVOKABLE bool insertClip(const QString& clipId, const QString& trackId, qint64 atMs,
                                int editMode = 0, const QString& replaceId = {});
    Q_INVOKABLE void select(const QString& itemId, bool additive = false);
    Q_INVOKABLE void selectBox(qint64 startMs, qint64 endMs, int firstTrack, int lastTrack);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool moveSelected(qint64 atMs, const QString& targetTrackId = {});
    Q_INVOKABLE bool isRippleMoveFollower(const QString& itemId) const;
    Q_INVOKABLE bool renameItem(const QString& itemId, const QString& name);
    Q_INVOKABLE bool splitSelected(qint64 atMs = -1);
    Q_INVOKABLE bool createLibraryClipFromSelection(const QString& name = {});
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool duplicateSelected();
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE bool paste(qint64 atMs = -1);
    Q_INVOKABLE bool trimStart(const QString& itemId, qint64 atMs);
    Q_INVOKABLE bool trimEnd(const QString& itemId, qint64 atMs);
    Q_INVOKABLE bool slip(const QString& itemId, qint64 sourceStartMs);
    Q_INVOKABLE bool roll(const QString& leftId, const QString& rightId, qint64 boundaryMs);
    Q_INVOKABLE bool setFades(const QString& itemId, qint64 fadeInMs, qint64 fadeOutMs);
    Q_INVOKABLE bool groupSelected();
    Q_INVOKABLE bool ungroupSelected();
    Q_INVOKABLE bool linkSelected();
    Q_INVOKABLE bool unlinkSelected();
    Q_INVOKABLE bool addTrack(int kind, const QString& name = {});
    Q_INVOKABLE bool removeTrack(const QString& trackId);
    Q_INVOKABLE bool moveTrack(const QString& trackId, int direction);
    Q_INVOKABLE bool setTrackState(const QString& trackId, const QString& property,
                                   const QVariant& value);
    Q_INVOKABLE bool addMarker(qint64 atMs, const QString& label = {});
    Q_INVOKABLE bool removeMarker(const QString& markerId);
    Q_INVOKABLE qint64 snap(qint64 proposedMs) const;
    Q_INVOKABLE qint64 snapMove(qint64 proposedStartMs) const;
    Q_INVOKABLE QString shortcut(const QString& action) const;
    Q_INVOKABLE bool configureShortcuts(const QVariantMap& values);
    Q_INVOKABLE void useVegasShortcuts();
    Q_INVOKABLE void selectFollowing(const QString& itemId);
    Q_INVOKABLE void selectSameSource(const QString& itemId);
    Q_INVOKABLE bool setTransformValue(const QString& property, double value);
    Q_INVOKABLE bool setTransformFlag(const QString& property, bool value);
    Q_INVOKABLE bool addTransformKeyframe(const QString& property, qint64 timeMs,
                                           double value, int interpolation);
    Q_INVOKABLE bool setClipAudio(double gainDb, double pan);
    Q_INVOKABLE bool setTrackAudio(const QString& trackId, double gainDb, double pan);
    Q_INVOKABLE bool setMasterAudio(double gainDb, double pan);
    Q_INVOKABLE bool setSpeed(double speed, bool preservePitch);
    Q_INVOKABLE bool setPitch(double semitones);
    Q_INVOKABLE bool setReverse(bool enabled);
    Q_INVOKABLE bool setFreeze(bool enabled, qint64 sourceMs);
    Q_INVOKABLE bool addEffect(int target, const QString& targetId, const QString& type);
    Q_INVOKABLE bool removeEffect(int target, const QString& targetId, const QString& effectId);
    Q_INVOKABLE bool moveEffect(int target, const QString& targetId, const QString& effectId, int offset);
    Q_INVOKABLE bool bypassEffect(int target, const QString& targetId, const QString& effectId, bool bypass);
    Q_INVOKABLE bool resetEffect(int target, const QString& targetId, const QString& effectId);
    Q_INVOKABLE bool setEffectParameter(int target, const QString& targetId, const QString& effectId,
                                        const QString& parameter, double value);
    Q_INVOKABLE bool addKeyframe(int target, const QString& targetId, const QString& effectId,
                                 const QString& parameter, qint64 timeMs, double value, int interpolation);
    Q_INVOKABLE bool removeKeyframe(int target, const QString& targetId, const QString& effectId,
                                    const QString& parameter, const QString& keyframeId);
    Q_INVOKABLE void copyAttributes();
    Q_INVOKABLE bool pasteAttributes(bool transform, bool timing, bool audio, bool effects);
    Q_INVOKABLE bool saveEffectPreset(const QString& name);
    Q_INVOKABLE bool applyEffectPreset(const QString& name);
    Q_INVOKABLE bool removeEffectPreset(const QString& name);
    Q_INVOKABLE void setPreviewQuality(int quality);
    Q_INVOKABLE bool addAudioKeyframe(int target, const QString& targetId,
                                      const QString& parameter, qint64 timeMs,
                                      double value, int interpolation);
    Q_INVOKABLE bool setMasterLimiter(bool enabled);
    Q_INVOKABLE bool renderPlaybackPreview();
    Q_INVOKABLE void cancelPlaybackPreviewRendering();
    Q_INVOKABLE bool startLivePreview(qint64 atMs);
    Q_INVOKABLE void stopLivePreview();
    Q_INVOKABLE qint64 stepFrame(qint64 currentPositionMs, int direction) const;
    Q_INVOKABLE bool buildStutter(int repeats, qint64 sliceMs, bool alternateReverse);
    Q_INVOKABLE bool buildRapidReverse(int segments, qint64 segmentMs);
    Q_INVOKABLE bool buildFrameRepeat(int sourceFrames, int repeatsPerFrame);
    Q_INVOKABLE bool buildRhythmRepeat(qint64 atMs, double bpm, int beats, qint64 gateMs, bool useMarkers);
    Q_INVOKABLE bool buildSpeedLadder(int steps, double startSpeed, double endSpeed,
                                      double pitchStep, bool preservePitch);
    Q_INVOKABLE bool applySafeEarrape(double intensity);
    Q_INVOKABLE bool applyYtpVisualPreset(const QString& presetId);
    Q_INVOKABLE bool applyYtpAudioPreset(const QString& presetId);
    Q_INVOKABLE bool applyYtpCombinedPreset(const QString& presetId);
    Q_INVOKABLE bool buildSentenceMixer(const QString& order);
    Q_INVOKABLE void startMacroRecording();
    Q_INVOKABLE void cancelMacroRecording();
    Q_INVOKABLE bool saveRecordedMacro(const QString& name);
    Q_INVOKABLE bool applyYtpMacro(const QString& name);
    Q_INVOKABLE bool removeYtpMacro(const QString& name);
    Q_INVOKABLE bool previewRandomizer(qulonglong seed, double reverseProbability,
                                       double effectProbability, double minSpeed,
                                       double maxSpeed, double minPitch, double maxPitch,
                                       bool shuffle);
    Q_INVOKABLE bool commitRandomizer();
    Q_INVOKABLE void cancelRandomizer();
    Q_INVOKABLE bool createSequence(const QString& name);
    Q_INVOKABLE bool removeSequence(const QString& sequenceId);
    Q_INVOKABLE bool switchSequence(const QString& sequenceId);
    Q_INVOKABLE bool insertNestedSequence(const QString& sequenceId,const QString& trackId,qint64 atMs);
    Q_INVOKABLE bool createAdjustmentClip(const QString& trackId,qint64 atMs,qint64 durationMs);
    Q_INVOKABLE bool addMask(int shape);
    Q_INVOKABLE bool updateMask(const QString& maskId,double x,double y,double width,double height,double feather,double opacity,bool inverted);
    Q_INVOKABLE bool removeMask(const QString& maskId);
    Q_INVOKABLE bool detectBeats();
    Q_INVOKABLE void setVisibleRange(qint64 startMs,qint64 endMs);
    Q_INVOKABLE bool renderContinuousProgramCache();
    Q_INVOKABLE void setContinuousCaching(bool enabled) { continuousCacheEnabled_=enabled; }
    Q_INVOKABLE void cancelBackgroundTask(const QString& taskId);
    Q_INVOKABLE bool clearMediaCache();
    Q_INVOKABLE bool buildSentenceV2(const QVariantList& words,const QString& name,qint64 paddingMs,qint64 crossfadeMs,const QString& trackId,qint64 atMs);
    Q_INVOKABLE bool createCompoundFromSelection(const QString& name);
    Q_INVOKABLE bool insertCompound(const QString& compoundId,const QString& trackId,qint64 atMs,bool independentCopy=false);
    Q_INVOKABLE bool removeCompound(const QString& compoundId);
    Q_INVOKABLE bool estimateBeatGrid(int division=4);
    Q_INVOKABLE bool configureBeatGrid(double bpm,qint64 offsetMs,int division,bool enabled=true);
    Q_INVOKABLE bool applyBeatTool(int mode,const QString& effectType=QStringLiteral("screen_shake"),const QString& parameter=QStringLiteral("amount"));
    Q_INVOKABLE bool trackMask(const QString& maskId);
    Q_INVOKABLE bool applyTrackedMotion(const QString& maskId,int mode);
    Q_INVOKABLE bool setCaption(bool enabled,const QString& text,double size,const QString& color);
    Q_INVOKABLE bool loadMacroEditor(const QString& name);
    Q_INVOKABLE void addMacroEditorStep(int tool);
    Q_INVOKABLE bool moveMacroEditorStep(int index,int offset);
    Q_INVOKABLE bool removeMacroEditorStep(int index);
    Q_INVOKABLE bool saveVisualMacro(const QString& name);
    Q_INVOKABLE bool applyMacroScope(const QString& name,int scope,double probability,qulonglong seed);
    Q_INVOKABLE bool previewMacroVariations(const QString& name,int count,qulonglong seed);
    Q_INVOKABLE QUrl sourceThumbnailUrl(const QString& mediaId, qint64 sourceTimeMs);
    Q_INVOKABLE bool addEffectToSelection(const QString& type);
    Q_INVOKABLE bool toggleEffectFavorite(const QString& type);
signals:
    void effectsBrowserChanged();
    void timelineChanged();
    void selectionChanged();
    void playheadChanged();
    void zoomChanged();
    void snappingChanged();
    void shortcutsChanged();
    void presetsChanged();
    void programPreviewChanged();
    void playbackPreviewChanged();
    void livePreviewChanged();
    void presentedFrameChanged();
    void toolkitChanged();
    void backgroundTasksChanged();
    void timelineThumbnailCacheChanged();
    void timelineCompacted(qint64 removedBeforePlayheadMs);
private:
    struct TimelineThumbnailRequest {
        QString mediaPath;
        QString mediaId;
        qint64 sourceTimeMs{0};
        QString cachePath;
    };
    struct PlaybackWindowCacheEntry {
        qint64 startMs{0};
        qint64 durationMs{0};
        QString path;
        quint64 lastUsed{0};
    };
    [[nodiscard]] const Sequence* sequence() const;
    bool mutate(const QString& description, const std::function<void(Sequence&)>& operation);
    void sanitizeSelection();
    bool runToolkit(const QString& description, const MacroStep& step,
                    const std::function<ToolkitResult(Sequence&)>& operation);
    [[nodiscard]] std::vector<Id> selectedItemIds() const;
    [[nodiscard]] std::optional<YtpMacro> loadYtpMacro(const QString& name) const;
    void saveYtpMacro(const YtpMacro& macro);
    void invalidatePlaybackPreview();
    bool renderPlaybackWindow(qint64 requestedMs, bool foreground);
    bool activatePlaybackWindow(qint64 startMs);
    void precacheUpcomingPlayback();
    void prunePlaybackWindowCache();
    void disposeLivePreviewProcess();
    void resetPresentedFrame();
    [[nodiscard]] bool livePreviewActive() const { return livePreviewStarting_ || !livePreviewUrl_.isEmpty() || livePreviewProcess_; }
    void publishLivePreviewIfBuffered(quint64 generation, const QUrl& streamUrl);
    bool startLivePreviewChunk(quint64 generation, qint64 chunkStartMs, int width, int height);
    void startNextTimelineThumbnail();
    ProjectController* projectController_;
    QStringList selectedIds_;
    QSet<QString> selectedIdSet_;
    mutable QVariantMap inspectorCache_;
    mutable bool inspectorCacheDirty_{true};
    std::vector<TimelineItem> clipboard_;
    qint64 clipboardOriginMs_{0};
    qint64 playheadMs_{0};
    double pixelsPerSecond_{80.0};
    bool snapping_{true};
    std::optional<TimelineItem> attributeClipboard_;
    QTimer previewDebounce_;
    QTimer programCacheDebounce_;
    QUrl renderedPreviewUrl_;
    QString renderedPreviewItemId_;
    int previewGeneration_{0};
    bool effectPreviewRendering_{false};
    bool effectPreviewPending_{false};
    std::shared_ptr<std::atomic_bool> effectPreviewCancellation_;
    QUrl playbackPreviewUrl_;
    qint64 playbackPreviewStartMs_{0};
    qint64 playbackPreviewDurationMs_{0};
    int playbackRequestGeneration_{0};
    bool previewRendering_{false};
    std::vector<PlaybackWindowCacheEntry> playbackWindowCache_;
    qint64 playbackRenderStartMs_{-1};
    bool playbackRenderForeground_{false};
    quint64 playbackWindowUseCounter_{0};
    QProcess* livePreviewProcess_{nullptr};
    QIODevice* livePreviewDevice_{nullptr};
    QByteArray livePreviewProgressBuffer_;
    QByteArray livePreviewDiagnostics_;
    qint64 livePreviewEncodedUs_{0};
    qint64 livePreviewPublishThresholdUs_{0};
    bool livePreviewProducerFinished_{false};
    QUrl livePreviewUrl_;
    qint64 livePreviewStartMs_{0};
    bool livePreviewStarting_{false};
    quint64 livePreviewGeneration_{0};
    qint64 presentedFrameTimestampUs_{-1};
    bool macroRecording_{false};
    std::vector<MacroStep> recordedMacroSteps_;
    std::optional<RandomizationPlan> randomizerPlan_;
    QString activeSequenceId_;
    qint64 visibleStartMs_{-1};
    qint64 visibleEndMs_{-1};
    QVariantList backgroundTasks_;
    bool programCacheStale_{true};
    double programCacheProgress_{0};
    QString programTaskId_;
    std::shared_ptr<std::atomic_bool> programCancellation_;
    bool continuousCacheEnabled_{false};
    std::vector<MacroStep> macroEditorSteps_;
    QStringList macroVariationPreviews_;
    QQueue<TimelineThumbnailRequest> timelineThumbnailQueue_;
    QSet<QString> timelineThumbnailRequests_;
    bool timelineThumbnailActive_{false};
    int timelineThumbnailGeneration_{0};
};

} // namespace ytp
