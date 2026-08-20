#pragma once

#include "model/project.h"
#include <QString>
#include <QByteArray>
#include <vector>

namespace ytp {
struct TrackingPoint final { Rational time; double x{0}; double y{0}; double confidence{0}; friend bool operator==(const TrackingPoint&,const TrackingPoint&)=default; };

class MediaAnalysis final {
public:
    static std::vector<TranscriptWord> parseWhisperJson(const QByteArray& json, QString* error = nullptr);
    static std::vector<TranscriptWord> transcribe(const QString& mediaPath, const QString& modelPath,
                                                  const QString& language, QString* error = nullptr);
    static std::vector<Rational> detectOnsets(const QString& mediaPath, QString* error = nullptr);
    static std::vector<Rational> detectOnsetsFromPcm(const std::vector<float>& samples, int sampleRate);
    static std::vector<TrackingPoint> trackRegion(const QString& mediaPath,Rational start,Rational duration,
                                                  int sourceWidth,int sourceHeight,double x,double y,double width,double height,
                                                  QString* error = nullptr);
    static std::vector<TrackingPoint> trackGrayFrames(const std::vector<QByteArray>& frames,int width,int height,
                                                      double x,double y,double regionWidth,double regionHeight,double frameSeconds=.1);
};

} // namespace ytp
