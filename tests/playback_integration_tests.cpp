#include <QAudioOutput>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QVideoSink>

#include <functional>
#include <iostream>

namespace {

bool waitUntil(const std::function<bool()>& condition, const int timeoutMs = 20'000) {
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
    if (ffmpeg.isEmpty()) ffmpeg = QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe");
    QProcess process;
    process.start(ffmpeg, {
        "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=3",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=3",
        "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-shortest", path
    });
    return process.waitForStarted(5'000) && process.waitForFinished(30'000) && process.exitCode() == 0;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    const auto mediaPath = directory.filePath(QStringLiteral("playback.mp4"));
    if (!directory.isValid() || !createFixture(mediaPath)) {
        std::cerr << "Could not create playback fixture.\n";
        return 1;
    }

    QMediaPlayer player;
    QAudioOutput audio;
    QVideoSink video;
    audio.setMuted(true);
    player.setAudioOutput(&audio);
    player.setVideoSink(&video);
    int decodedFrames = 0;
    QObject::connect(&video, &QVideoSink::videoFrameChanged,
                     [&decodedFrames] { ++decodedFrames; });

    player.setSource(QUrl::fromLocalFile(mediaPath));
    const bool loaded = waitUntil([&] {
        return player.mediaStatus() == QMediaPlayer::LoadedMedia ||
               player.mediaStatus() == QMediaPlayer::BufferedMedia;
    });
    if (!loaded || player.duration() < 2'900 || player.duration() > 3'100) {
        std::cerr << "Qt Multimedia did not load the three-second A/V fixture: "
                  << player.errorString().toStdString() << '\n';
        return 1;
    }

    player.setPosition(1'000);
    if (!waitUntil([&] { return player.position() >= 950 && player.position() <= 1'100; })) {
        std::cerr << "Source seek did not settle near one second.\n";
        return 1;
    }
    player.play();
    if (!waitUntil([&] { return player.position() > 1'100 && decodedFrames > 0; })) {
        std::cerr << "Playback did not advance and decode video frames: "
                  << player.errorString().toStdString() << '\n';
        return 1;
    }
    player.pause();
    player.stop();

    // The Program monitor consumes one continuous FFmpeg transport stream for rendered
    // effects. This verifies that Qt's FFmpeg backend can decode directly from a running
    // QProcess/QIODevice without closing and reopening media files at effect boundaries.
    auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) ffmpeg = QStringLiteral("C:/msys64/ucrt64/bin/ffmpeg.exe");
    QProcess stream;
    stream.setProcessChannelMode(QProcess::SeparateChannels);
    const auto streamUrl=QStringLiteral("http://127.0.0.1:%1/program.ts")
        .arg(30'000+static_cast<int>(QCoreApplication::applicationPid()%20'000));
    stream.start(ffmpeg, {
        "-hide_banner", "-loglevel", "error", "-i", mediaPath,
        "-vf", "hue=h=90,eq=contrast=1.4", "-c:v", "libx264", "-preset", "ultrafast",
        "-tune", "zerolatency", "-g", "15", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-f", "mpegts", "-muxdelay", "0", "-flush_packets", "1",
        "-listen", "1", streamUrl
    });
    if (!stream.waitForStarted(5'000)) {
        std::cerr << "Could not start continuous preview stream.\n";
        return 1;
    }
    const auto framesBeforeStream = decodedFrames;
    player.setSource(QUrl(streamUrl));
    player.play();
    if (!waitUntil([&] { return decodedFrames > framesBeforeStream + 2 && player.position() > 100; }, 20'000)) {
        std::cerr << "Qt Multimedia did not consume the continuous processed stream: "
                  << player.errorString().toStdString() << " / "
                  << stream.readAllStandardError().toStdString() << '\n';
        stream.kill();
        stream.waitForFinished(5'000);
        return 1;
    }
    player.stop();
    if (stream.state() != QProcess::NotRunning) stream.kill();
    stream.waitForFinished(5'000);
    std::cout << "Qt Multimedia source playback, seek, and continuous processed-stream decode passed.\n";
    return 0;
}
