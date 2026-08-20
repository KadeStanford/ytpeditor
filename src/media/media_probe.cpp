#include "media/media_probe.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace ytp {
namespace {

void setError(QString* destination, const QString& message) {
    if (destination != nullptr) {
        *destination = message;
    }
}

std::optional<Rational> parseFraction(const QString& value) {
    const auto parts = value.split('/');
    if (parts.size() != 2) {
        return std::nullopt;
    }
    bool numeratorOk = false;
    bool denominatorOk = false;
    const auto numerator = parts[0].toLongLong(&numeratorOk);
    const auto denominator = parts[1].toLongLong(&denominatorOk);
    if (!numeratorOk || !denominatorOk || denominator == 0) {
        return std::nullopt;
    }
    return Rational{numerator, denominator};
}

std::optional<Rational> streamDuration(const QJsonObject& stream) {
    bool ticksOk = false;
    const auto ticks = stream.value("duration_ts").toVariant().toLongLong(&ticksOk);
    const auto timeBase = parseFraction(stream.value("time_base").toString());
    if (ticksOk && timeBase && ticks >= 0) {
        return Rational{ticks, 1} * *timeBase;
    }
    return std::nullopt;
}

Rational decimalSeconds(const QString& value) {
    bool ok = false;
    const long double seconds = value.toDouble(&ok);
    if (!ok || !std::isfinite(seconds) || seconds < 0.0L) {
        throw std::invalid_argument("media duration is invalid");
    }
    constexpr std::int64_t scale = 1'000'000;
    return {static_cast<std::int64_t>(std::llround(seconds * scale)), scale};
}

} // namespace

std::optional<MediaProbeResult> MediaProbe::probe(const QString& filePath,
                                                  QString* errorMessage) {
    QString executable = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (executable.isEmpty()) {
        executable = QStringLiteral("C:/msys64/ucrt64/bin/ffprobe.exe");
    }

    QProcess process;
    process.start(executable, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_streams"), QStringLiteral("-show_format"), filePath
    });
    if (!process.waitForStarted(5'000) || !process.waitForFinished(30'000)) {
        process.kill();
        setError(errorMessage, QStringLiteral("ffprobe did not finish while reading this file."));
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(errorMessage, QString::fromUtf8(process.readAllStandardError()).trimmed());
        return std::nullopt;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("ffprobe returned invalid metadata."));
        return std::nullopt;
    }

    try {
        const auto root = document.object();
        const auto streams = root.value("streams").toArray();
        QJsonObject video;
        QJsonObject audio;
        for (const auto& value : streams) {
            const auto stream = value.toObject();
            const auto type = stream.value("codec_type").toString();
            if (type == "video" && video.isEmpty()) {
                video = stream;
            } else if (type == "audio" && audio.isEmpty()) {
                audio = stream;
            }
        }
        if (video.isEmpty() && audio.isEmpty()) {
            throw std::invalid_argument("file contains no usable video or audio stream");
        }

        MediaProbeResult result;
        if (!video.isEmpty()) {
            result.width = video.value("width").toInt();
            result.height = video.value("height").toInt();
            const auto rate = parseFraction(video.value("avg_frame_rate").toString());
            if (rate && rate->numerator() > 0) {
                result.frameRateNumerator = rate->numerator();
                result.frameRateDenominator = rate->denominator();
            }
        }
        if (!audio.isEmpty()) {
            bool sampleRateOk = false;
            result.audioSampleRate = audio.value("sample_rate").toString().toInt(&sampleRateOk);
            if (!sampleRateOk) {
                result.audioSampleRate = 0;
            }
        }

        auto duration = !video.isEmpty() ? streamDuration(video) : std::nullopt;
        if (!duration && !audio.isEmpty()) {
            duration = streamDuration(audio);
        }
        if (duration) {
            result.duration = *duration;
        } else {
            result.duration = decimalSeconds(
                root.value("format").toObject().value("duration").toString());
        }
        return result;
    } catch (const std::exception& exception) {
        setError(errorMessage, QString::fromUtf8(exception.what()));
        return std::nullopt;
    }
}

QVector<qint64> MediaProbe::frameTimestampsMs(const QString& filePath,
                                              QString* errorMessage) {
    QString executable = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (executable.isEmpty()) {
        executable = QStringLiteral("C:/msys64/ucrt64/bin/ffprobe.exe");
    }
    QProcess process;
    process.start(executable, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"), QStringLiteral("frame=best_effort_timestamp_time"),
        QStringLiteral("-of"), QStringLiteral("csv=p=0"), filePath
    });
    if (!process.waitForStarted(5'000) || !process.waitForFinished(300'000)) {
        process.kill();
        setError(errorMessage, QStringLiteral("Frame indexing timed out."));
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(errorMessage, QString::fromUtf8(process.readAllStandardError()).trimmed());
        return {};
    }
    QVector<qint64> timestamps;
    const auto lines = process.readAllStandardOutput().split('\n');
    timestamps.reserve(lines.size());
    for (const auto& rawLine : lines) {
        const auto line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        bool ok = false;
        const double seconds = line.toDouble(&ok);
        if (ok) {
            timestamps.push_back(static_cast<qint64>(std::llround(seconds * 1'000.0)));
        }
    }
    std::sort(timestamps.begin(), timestamps.end());
    timestamps.erase(std::unique(timestamps.begin(), timestamps.end()), timestamps.end());
    return timestamps;
}

} // namespace ytp
