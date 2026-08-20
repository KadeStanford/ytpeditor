#include "media/native_media_player.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <combaseapi.h>
#include <propvarutil.h>
#endif

#include <algorithm>
#include <cmath>

namespace ytp {

NativeMediaPlayer::NativeMediaPlayer(QObject* parent) : QObject(parent) {
    positionTimer_.setInterval(50);
    connect(&positionTimer_, &QTimer::timeout, this, [this] {
        if (playbackState_ != PlayingState) return;
        const auto current = position();
        if (current != positionMs_) positionMs_ = current;
        emit positionChanged();
    });

#ifdef Q_OS_WIN
    if (QGuiApplication::platformName().compare(QStringLiteral("windows"), Qt::CaseInsensitive) == 0) {
        videoWindow_ = new QWindow;
        videoWindow_->setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
        videoWindow_->installEventFilter(this);
        const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized_ = SUCCEEDED(comResult);
        const auto hwnd = reinterpret_cast<HWND>(videoWindow_->winId());
        const auto result = MFPCreateMediaPlayer(nullptr, FALSE, MFP_OPTION_NONE, this, hwnd, &player_);
        if (FAILED(result)) backendError(QStringLiteral("create Windows Media Foundation player"), result);
    }
#endif
}

NativeMediaPlayer::~NativeMediaPlayer() {
    positionTimer_.stop();
#ifdef Q_OS_WIN
    if (player_) {
        player_->Shutdown();
        player_->Release();
        player_ = nullptr;
    }
    if (comInitialized_) CoUninitialize();
#endif
    delete videoWindow_;
}

qint64 NativeMediaPlayer::position() const {
#ifdef Q_OS_WIN
    // SetPosition is asynchronous in MFPlay. While it is pending, exposing the
    // decoder's old position makes QML conclude that the seek failed and wait
    // forever for another position notification. Use the requested position
    // (and its running clock) until POSITION_SET confirms the decoder caught up.
    if (player_ && backendReady_ && !seekPending_) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(player_->GetPosition(MFP_POSITIONTYPE_100NS, &value))) {
            LONGLONG ticks = 0;
            if (SUCCEEDED(PropVariantToInt64(value, &ticks))) {
                PropVariantClear(&value);
                return std::max<qint64>(0, ticks / 10'000);
            }
        }
        PropVariantClear(&value);
    }
#endif
    if (playbackState_ == PlayingState && playbackClock_.isValid())
        return std::max<qint64>(0, playbackClockStartMs_ +
            static_cast<qint64>(std::llround(playbackClock_.elapsed() * playbackRate_)));
    return positionMs_;
}

void NativeMediaPlayer::setSource(const QUrl& source) {
    if (source_ == source) return;
    ++sourceGeneration_;
    source_ = source;
    backendReady_ = false;
    seekPending_ = false;
    durationMs_ = 0;
    positionMs_ = 0;
    resetSimulationClock(0);
    setPlaybackState(StoppedState);
    emit sourceChanged();
    emit positionChanged();
    emit durationChanged();
    if (source_.isEmpty()) {
#ifdef Q_OS_WIN
        if (player_) player_->ClearMediaItem();
#endif
        setMediaStatus(NoMedia);
        return;
    }
    setMediaStatus(LoadingMedia);
#ifdef Q_OS_WIN
    if (player_) {
        const auto localPath = source_.isLocalFile() ? source_.toLocalFile() : source_.toString();
        const auto path = localPath.toStdWString();
        const auto result = player_->CreateMediaItemFromURL(path.c_str(), FALSE,
            static_cast<DWORD_PTR>(sourceGeneration_), nullptr);
        if (FAILED(result)) {
            setMediaStatus(InvalidMedia);
            backendError(QStringLiteral("open media"), result);
        }
        return;
    }
#endif
    // Offscreen tests have no native HWND. Preserve deterministic transport
    // semantics there without invoking a decoder.
    setMediaStatus(LoadedMedia);
}

void NativeMediaPlayer::setPosition(qint64 positionMs) {
    positionMs = std::max<qint64>(0, positionMs);
    if (durationMs_ > 0) positionMs = std::min(positionMs, durationMs_);
    positionMs_ = positionMs;
    resetSimulationClock(positionMs_);
#ifdef Q_OS_WIN
    if (player_ && backendReady_) {
        PROPVARIANT value;
        PropVariantInit(&value);
        InitPropVariantFromInt64(positionMs_ * 10'000, &value);
        const auto result = player_->SetPosition(MFP_POSITIONTYPE_100NS, &value);
        PropVariantClear(&value);
        seekPending_ = SUCCEEDED(result);
        if (FAILED(result)) backendError(QStringLiteral("seek media"), result);
    }
#endif
    emit positionChanged();
}

void NativeMediaPlayer::setPlaybackRate(double rate) {
    rate = std::clamp(rate, 0.01, 8.0);
    if (qFuzzyCompare(playbackRate_, rate)) return;
    positionMs_ = position();
    playbackRate_ = rate;
    resetSimulationClock(positionMs_);
#ifdef Q_OS_WIN
    if (player_ && backendReady_) player_->SetRate(static_cast<float>(playbackRate_));
#endif
    emit playbackRateChanged();
}

void NativeMediaPlayer::setMuted(bool muted) {
    if (muted_ == muted) return;
    muted_ = muted;
#ifdef Q_OS_WIN
    if (player_) player_->SetMute(muted_ ? TRUE : FALSE);
#endif
    emit mutedChanged();
}

void NativeMediaPlayer::play() {
    if (source_.isEmpty()) return;
    positionMs_ = position();
    resetSimulationClock(positionMs_);
    setPlaybackState(PlayingState);
    positionTimer_.start();
#ifdef Q_OS_WIN
    if (player_ && backendReady_) {
        const auto result = player_->Play();
        if (FAILED(result)) backendError(QStringLiteral("play media"), result);
    }
#endif
}

void NativeMediaPlayer::pause() {
    positionMs_ = position();
    resetSimulationClock(positionMs_);
    positionTimer_.stop();
    if (playbackState_ != StoppedState) setPlaybackState(PausedState);
#ifdef Q_OS_WIN
    if (player_ && backendReady_) player_->Pause();
#endif
    emit positionChanged();
}

void NativeMediaPlayer::stop() {
    positionTimer_.stop();
    positionMs_ = 0;
    resetSimulationClock(0);
    setPlaybackState(StoppedState);
#ifdef Q_OS_WIN
    if (player_ && backendReady_) player_->Stop();
#endif
    emit positionChanged();
}

bool NativeMediaPlayer::eventFilter(QObject* watched, QEvent* event) {
#ifdef Q_OS_WIN
    if (watched == videoWindow_ && player_ &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Expose))
        player_->UpdateVideo();
#else
    Q_UNUSED(watched)
    Q_UNUSED(event)
#endif
    return false;
}

void NativeMediaPlayer::setPlaybackState(int state) {
    if (playbackState_ == state) return;
    playbackState_ = state;
    emit playbackStateChanged();
}

void NativeMediaPlayer::setMediaStatus(int status) {
    if (mediaStatus_ == status) return;
    mediaStatus_ = status;
    emit mediaStatusChanged();
}

void NativeMediaPlayer::resetSimulationClock(qint64 positionMs) {
    playbackClockStartMs_ = positionMs;
    playbackClock_.restart();
}

void NativeMediaPlayer::backendError(const QString& operation, long result) {
    emit errorOccurred(QStringLiteral("Could not %1 (Media Foundation error 0x%2).")
        .arg(operation, QString::number(static_cast<unsigned long>(result), 16)));
}

#ifdef Q_OS_WIN
HRESULT STDMETHODCALLTYPE NativeMediaPlayer::QueryInterface(REFIID iid, void** object) {
    if (!object) return E_POINTER;
    static constexpr GUID callbackIid{0x766c8ffb, 0x5fdb, 0x4fea,
        {0xa2, 0x8d, 0xb9, 0x12, 0x99, 0x6f, 0x51, 0xbd}};
    if (iid == IID_IUnknown || iid == callbackIid) {
        *object = static_cast<IMFPMediaPlayerCallback*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE NativeMediaPlayer::AddRef() { return ++callbackReferences_; }
ULONG STDMETHODCALLTYPE NativeMediaPlayer::Release() { return --callbackReferences_; }

void STDMETHODCALLTYPE NativeMediaPlayer::OnMediaPlayerEvent(MFP_EVENT_HEADER* event) {
    if (!event) return;
    if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED) {
        const auto* created = MFP_GET_MEDIAITEM_CREATED_EVENT(event);
        auto* item = created ? created->pMediaItem : nullptr;
        const auto generation = created ? static_cast<quint64>(created->dwUserData) : 0;
        if (item) item->AddRef();
        const auto result = event->hrEvent;
        QMetaObject::invokeMethod(this, [this, item, generation, result] {
            handleMediaItemCreated(item, generation, result);
            if (item) item->Release();
        }, Qt::QueuedConnection);
    } else if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
        const auto result = event->hrEvent;
        QMetaObject::invokeMethod(this, [this, result] { handleMediaItemSet(result); }, Qt::QueuedConnection);
    } else if (event->eEventType == MFP_EVENT_TYPE_POSITION_SET) {
        const auto result = event->hrEvent;
        QMetaObject::invokeMethod(this, [this, result] { handlePositionSet(result); }, Qt::QueuedConnection);
    } else if (event->eEventType == MFP_EVENT_TYPE_ERROR) {
        const auto result = event->hrEvent;
        QMetaObject::invokeMethod(this, [this, result] {
            setMediaStatus(InvalidMedia);
            backendError(QStringLiteral("decode media"), result);
        }, Qt::QueuedConnection);
    }
}

void NativeMediaPlayer::handleMediaItemCreated(IMFPMediaItem* item, quint64 generation, long result) {
    if (generation != sourceGeneration_) return;
    if (FAILED(result) || !item || !player_) {
        setMediaStatus(InvalidMedia);
        backendError(QStringLiteral("open media"), result);
        return;
    }
    const auto setResult = player_->SetMediaItem(item);
    if (FAILED(setResult)) {
        setMediaStatus(InvalidMedia);
        backendError(QStringLiteral("select media"), setResult);
    }
}

void NativeMediaPlayer::handleMediaItemSet(long result) {
    if (FAILED(result) || !player_) {
        setMediaStatus(InvalidMedia);
        backendError(QStringLiteral("prepare media"), result);
        return;
    }
    backendReady_ = true;
    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(player_->GetDuration(MFP_POSITIONTYPE_100NS, &duration))) {
        LONGLONG ticks = 0;
        if (SUCCEEDED(PropVariantToInt64(duration, &ticks))) durationMs_ = ticks / 10'000;
    }
    PropVariantClear(&duration);
    emit durationChanged();
    setMediaStatus(LoadedMedia);
    applyPendingState();
}

void NativeMediaPlayer::handlePositionSet(long result) {
    seekPending_ = false;
    if (FAILED(result)) {
        backendError(QStringLiteral("seek media"), result);
        return;
    }
    // Rebase the fallback clock at the settled decoder position. This keeps
    // position monotonic if playback is already running during the seek.
    positionMs_ = position();
    resetSimulationClock(positionMs_);
    emit positionChanged();
}

void NativeMediaPlayer::applyPendingState() {
    if (!player_ || !backendReady_) return;
    player_->SetMute(muted_ ? TRUE : FALSE);
    player_->SetRate(static_cast<float>(playbackRate_));
    PROPVARIANT value;
    PropVariantInit(&value);
    InitPropVariantFromInt64(positionMs_ * 10'000, &value);
    player_->SetPosition(MFP_POSITIONTYPE_100NS, &value);
    PropVariantClear(&value);
    if (playbackState_ == PlayingState) player_->Play();
    else if (playbackState_ == PausedState) player_->Pause();
}
#endif

} // namespace ytp
