#include "export/export_settings.h"

#include <algorithm>

namespace ytp {

const std::vector<ExportPreset>& exportPresets() {
    static const std::vector<ExportPreset> presets{
        {"youtube_1080p", "YouTube 1080p", ExportContainer::Mp4, 1920, 1080, "libx264", "aac", 12000, 320, 48000, false},
        {"youtube_720p", "YouTube 720p", ExportContainer::Mp4, 1280, 720, "libx264", "aac", 7500, 256, 48000, false},
        {"youtube_4k", "YouTube 4K", ExportContainer::Mp4, 3840, 2160, "libx264", "aac", 45000, 320, 48000, false},
        {"webm_1080p", "WebM 1080p", ExportContainer::WebM, 1920, 1080, "libvpx-vp9", "libopus", 10000, 256, 48000, false},
        {"archive_mov", "Editing Master", ExportContainer::Mov, 1920, 1080, "prores_ks", "pcm_s24le", 50000, 0, 48000, false},
        {"audio_wav", "Audio WAV", ExportContainer::Wav, 0, 0, "", "pcm_s24le", 0, 0, 48000, true},
    };
    return presets;
}

const ExportPreset* findExportPreset(const std::string& id) noexcept {
    const auto& presets = exportPresets();
    const auto found = std::find_if(presets.begin(), presets.end(), [&](const auto& value) { return value.id == id; });
    return found == presets.end() ? nullptr : &*found;
}

std::string exportExtension(ExportContainer container) {
    switch (container) {
    case ExportContainer::Mp4: return "mp4";
    case ExportContainer::WebM: return "webm";
    case ExportContainer::Mov: return "mov";
    case ExportContainer::Wav: return "wav";
    }
    return "mp4";
}

std::optional<std::string> ExportSettings::validate(const Rational& sequenceDuration) const {
    if (outputPath.empty()) return "Choose an output file.";
    if (sequenceDuration <= Rational{}) return "The sequence is empty.";
    if (preset.id.empty() || preset.name.empty()) return "The export preset is incomplete.";
    const auto containerValue = static_cast<int>(preset.container);
    const auto rangeValue = static_cast<int>(range);
    if (containerValue < static_cast<int>(ExportContainer::Mp4) ||
        containerValue > static_cast<int>(ExportContainer::Wav) ||
        rangeValue < static_cast<int>(ExportRange::EntireSequence) ||
        rangeValue > static_cast<int>(ExportRange::MarkedRegion))
        return "The export mode is invalid.";
    if (!preset.audioOnly && (preset.width < 16 || preset.height < 16 || preset.width % 2 || preset.height % 2))
        return "Video dimensions must be even and at least 16 pixels.";
    if (!preset.audioOnly && (preset.videoCodec.empty() || preset.videoBitrateKbps <= 0))
        return "A video codec and positive bitrate are required.";
    if (preset.audioCodec.empty() || preset.audioSampleRate < 8000) return "Audio settings are invalid.";
    if (range == ExportRange::MarkedRegion &&
        (rangeStart < Rational{} || rangeEnd <= rangeStart || rangeEnd > sequenceDuration))
        return "The marked render region is outside the sequence.";
    return std::nullopt;
}

} // namespace ytp
