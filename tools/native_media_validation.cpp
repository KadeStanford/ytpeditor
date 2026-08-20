#include "media/native_media_player.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QThread>
#include <functional>
#include <iostream>

namespace {
bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(10);
    }
    return condition();
}
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    if (argc != 2 || !QFileInfo::exists(QString::fromLocal8Bit(argv[1]))) {
        std::cerr << "Usage: ytp_native_media_validation <media-file>\n";
        return 2;
    }
    ytp::NativeMediaPlayer player;
    QString playbackError;
    QObject::connect(&player, &ytp::NativeMediaPlayer::errorOccurred,
                     &application, [&](const QString& error) { playbackError = error; });
    player.setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (!waitUntil([&] { return player.mediaStatus() == ytp::NativeMediaPlayer::LoadedMedia ||
                               player.mediaStatus() == ytp::NativeMediaPlayer::InvalidMedia; }, 15'000) ||
            player.mediaStatus() == ytp::NativeMediaPlayer::InvalidMedia) {
        std::cerr << "Media Foundation could not load the file: " << playbackError.toStdString() << '\n';
        return 1;
    }
    player.setPosition(1'000);
    player.play();
    if (!waitUntil([&] { return player.position() >= 2'500 || !playbackError.isEmpty(); }, 8'000)) {
        std::cerr << "Media Foundation playback did not advance.\n";
        return 1;
    }
    if (!playbackError.isEmpty()) {
        std::cerr << playbackError.toStdString() << '\n';
        return 1;
    }
    player.pause();
    std::cout << "Native Windows Media Foundation playback passed; position="
              << player.position() << " ms, duration=" << player.duration() << " ms.\n";
    return 0;
}
