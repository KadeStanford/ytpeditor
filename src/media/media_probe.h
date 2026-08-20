#pragma once

#include "core/rational.h"

#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

namespace ytp {

struct MediaProbeResult final {
    Rational duration;
    std::int64_t frameRateNumerator{0};
    std::int64_t frameRateDenominator{1};
    int width{0};
    int height{0};
    int audioSampleRate{0};
};

class MediaProbe final {
public:
    [[nodiscard]] static std::optional<MediaProbeResult> probe(
        const QString& filePath, QString* errorMessage = nullptr);
    [[nodiscard]] static QVector<qint64> frameTimestampsMs(
        const QString& filePath, QString* errorMessage = nullptr);
};

} // namespace ytp
