#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#ifdef Q_OS_WIN
#include <atomic>
#include <mfplay.h>
#endif

namespace ytp {

// Windows Media Foundation playback surface used by the interactive viewers.
// Rendering/export remains a separate concern; this class deliberately avoids
// Qt Multimedia so opening or playing a clip cannot load Qt's FFmpeg plugin.
class NativeMediaPlayer : public QObject
#ifdef Q_OS_WIN
    , public IMFPMediaPlayerCallback
#endif
{
    Q_OBJECT
    Q_PROPERTY(QWindow* videoWindow READ videoWindow CONSTANT)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY mediaStatusChanged)
    Q_PROPERTY(int playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(int mediaStatus READ mediaStatus NOTIFY mediaStatusChanged)

public:
    enum PlaybackState { StoppedState = 0, PlayingState = 1, PausedState = 2 };
    Q_ENUM(PlaybackState)
    enum MediaStatus {
        NoMedia = 0, LoadingMedia = 1, LoadedMedia = 2, BufferedMedia = 3,
        BufferingMedia = 4, EndOfMedia = 5, InvalidMedia = 6
    };
    Q_ENUM(MediaStatus)

    explicit NativeMediaPlayer(QObject* parent = nullptr);
    ~NativeMediaPlayer() override;

    QWindow* videoWindow() const noexcept { return videoWindow_; }
    QUrl source() const { return source_; }
    qint64 position() const;
    qint64 duration() const noexcept { return durationMs_; }
    double playbackRate() const noexcept { return playbackRate_; }
    bool muted() const noexcept { return muted_; }
    bool seekable() const noexcept { return mediaStatus_ == LoadedMedia || mediaStatus_ == BufferedMedia; }
    int playbackState() const noexcept { return playbackState_; }
    int mediaStatus() const noexcept { return mediaStatus_; }

    void setSource(const QUrl& source);
    void setPosition(qint64 positionMs);
    void setPlaybackRate(double rate);
    void setMuted(bool muted);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();

signals:
    void sourceChanged();
    void positionChanged();
    void durationChanged();
    void playbackRateChanged();
    void mutedChanged();
    void playbackStateChanged();
    void mediaStatusChanged();
    void errorOccurred(const QString& errorString);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

#ifdef Q_OS_WIN
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override;
#endif

private:
    void setPlaybackState(int state);
    void setMediaStatus(int status);
    void resetSimulationClock(qint64 positionMs);
    void backendError(const QString& operation, long result);
#ifdef Q_OS_WIN
    void applyPendingState();
    void handleMediaItemCreated(IMFPMediaItem* item, quint64 generation, long result);
    void handleMediaItemSet(long result);
    void handlePositionSet(long result);
#endif

    QWindow* videoWindow_ = nullptr;
    QUrl source_;
    mutable qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    double playbackRate_ = 1.0;
    bool muted_ = false;
    int playbackState_ = StoppedState;
    int mediaStatus_ = NoMedia;
    QElapsedTimer playbackClock_;
    qint64 playbackClockStartMs_ = 0;
    QTimer positionTimer_;
    quint64 sourceGeneration_ = 0;

#ifdef Q_OS_WIN
    std::atomic_ulong callbackReferences_{1};
    IMFPMediaPlayer* player_ = nullptr;
    bool comInitialized_ = false;
    bool backendReady_ = false;
    bool seekPending_ = false;
#endif
};

} // namespace ytp
