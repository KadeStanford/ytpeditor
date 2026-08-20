#pragma once

#include "core/rational.h"

#include <optional>
#include <string>
#include <vector>

namespace ytp {

enum class ExportContainer { Mp4, WebM, Mov, Wav };
enum class ExportRange { EntireSequence, MarkedRegion };

struct ExportPreset final {
    std::string id;
    std::string name;
    ExportContainer container{ExportContainer::Mp4};
    int width{1920};
    int height{1080};
    std::string videoCodec{"libx264"};
    std::string audioCodec{"aac"};
    int videoBitrateKbps{12000};
    int audioBitrateKbps{320};
    int audioSampleRate{48000};
    bool audioOnly{false};

    friend bool operator==(const ExportPreset&, const ExportPreset&) = default;
};

struct ExportSettings final {
    ExportPreset preset;
    ExportRange range{ExportRange::EntireSequence};
    Rational rangeStart;
    Rational rangeEnd;
    std::string outputPath;
    bool useHardwareEncoder{false};
    bool overwrite{false};

    [[nodiscard]] std::optional<std::string> validate(const Rational& sequenceDuration) const;
    friend bool operator==(const ExportSettings&, const ExportSettings&) = default;
};

[[nodiscard]] const std::vector<ExportPreset>& exportPresets();
[[nodiscard]] const ExportPreset* findExportPreset(const std::string& id) noexcept;
[[nodiscard]] std::string exportExtension(ExportContainer container);

} // namespace ytp
