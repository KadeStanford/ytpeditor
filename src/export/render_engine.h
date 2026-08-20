#pragma once

#include "export/export_settings.h"
#include "model/project.h"

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace ytp {

struct RenderResult final {
    bool success{false};
    bool cancelled{false};
    QString error;
    QString logPath;
};

class RenderEngine final {
public:
    using ProgressCallback = std::function<void(double, const QString&)>;

    [[nodiscard]] static RenderResult render(const Project& project, const Sequence& sequence,
                                             const ExportSettings& settings,
                                             const std::atomic_bool& cancellationRequested,
                                             ProgressCallback progress = {});
    [[nodiscard]] static bool snapshot(const Project& project, const Sequence& sequence,
                                       const Rational& time, const QString& outputPath,
                                       QString* errorMessage = nullptr);
    [[nodiscard]] static RenderResult renderPreviewCache(const Project& project,const Sequence& sequence,
                                                         const QString& outputPath,int width,int height,
                                                         const std::atomic_bool& cancellationRequested,
                                                         ProgressCallback progress = {});
    [[nodiscard]] static RenderResult renderPreviewWindow(const Project& project,const Sequence& sequence,
                                                          const QString& outputPath,int width,int height,
                                                          Rational start,Rational duration,
                                                          const std::atomic_bool& cancellationRequested,
                                                          ProgressCallback progress = {});
    [[nodiscard]] static QStringList previewStreamArguments(const Project& project,const Sequence& sequence,
                                                            int width,int height,Rational start,
                                                            const QString& outputUrl,
                                                            Rational outputTimestampOffset = {});
    [[nodiscard]] static bool encoderAvailable(const QString& encoder);
    [[nodiscard]] static QString ffmpegExecutable();
};

} // namespace ytp
