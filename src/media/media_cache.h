#pragma once

#include "core/rational.h"
#include "model/timeline.h"

#include <QString>
#include <atomic>

namespace ytp {
struct CacheInfo final { qint64 bytes{0}; int files{0}; };

class MediaCache final {
public:
    [[nodiscard]] static QString cacheDirectory();
    [[nodiscard]] static QString thumbnailPath(const QString& cacheKey);
    [[nodiscard]] static QString timelineThumbnailPath(const QString& cacheKey);
    [[nodiscard]] static QString timelineFramePath(const QString& mediaId, qint64 sourceTimeMs);
    [[nodiscard]] static QString timelineWaveformPath(const QString& cacheKey);
    [[nodiscard]] static QString timelineVisualKey(const TimelineItem& item);
    [[nodiscard]] static QString waveformPath(const QString& cacheKey);
    [[nodiscard]] static QString proxyPath(const QString& cacheKey);
    [[nodiscard]] static QString effectPreviewPath(const QString& cacheKey);
    [[nodiscard]] static QString playbackPreviewPath(const QString& cacheKey);
    [[nodiscard]] static QString sequencePreviewPath(const QString& cacheKey);
    [[nodiscard]] static CacheInfo cacheInfo();
    static bool clearGenerated(QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateThumbnail(const QString& mediaPath,
                                                const Rational& time,
                                                const QString& cacheKey,
                                                QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateWaveform(const QString& mediaPath,
                                               const QString& cacheKey,
                                               QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateTimelineThumbnail(const QString& mediaPath,
                                                        const TimeRange& sourceRange,
                                                        const QString& cacheKey,
                                                        QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateTimelineFrame(const QString& mediaPath,
                                                    const Rational& sourceTime,
                                                    const QString& mediaId,
                                                    qint64 quantizedSourceTimeMs,
                                                    QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateTimelineWaveform(const QString& mediaPath,
                                                       const TimeRange& sourceRange,
                                                       const QString& cacheKey,
                                                       QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateProxy(const QString& mediaPath, const QString& cacheKey,
                                            QString* errorMessage = nullptr);
    [[nodiscard]] static bool generateEffectPreview(const QString& mediaPath,
                                                    const TimelineItem& item,
                                                    Rational sourceTime,
                                                    PreviewQuality quality,
                                                    QString* errorMessage = nullptr,
                                                    const std::atomic_bool* cancellationRequested = nullptr);
    [[nodiscard]] static bool generatePlaybackPreview(const QString& mediaPath,
                                                      const TimelineItem& videoItem,
                                                      const TimelineItem& audioItem,
                                                      const Track& videoTrack,
                                                      const Track& audioTrack,
                                                      const Sequence& sequence,
                                                      QString* errorMessage = nullptr);
};

} // namespace ytp
