#pragma once

#include "model/timeline.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ytp {

enum class YtpTool {
    Stutter,
    RapidReverse,
    FrameRepeat,
    RhythmRepeat,
    SpeedLadder,
    SafeEarrape,
    VisualPreset,
    SentenceMixer,
    AudioPreset
    ,CombinedPreset
};

struct ToolkitResult final {
    std::vector<Id> itemIds;
    Rational start;
    Rational duration;
    std::string summary;
};

struct MacroStep final {
    YtpTool tool{YtpTool::Stutter};
    std::map<std::string, double> numbers;
    std::map<std::string, std::string> strings;
    friend bool operator==(const MacroStep&, const MacroStep&) = default;
};

struct YtpMacro final {
    std::string name;
    std::vector<MacroStep> steps;
    friend bool operator==(const YtpMacro&, const YtpMacro&) = default;
};

struct RandomizerOptions final {
    std::uint64_t seed{1};
    double reverseProbability{0.35};
    double effectProbability{0.30};
    double minSpeed{0.5};
    double maxSpeed{2.5};
    double minPitch{-12.0};
    double maxPitch{12.0};
    bool shuffle{true};
};

struct RandomizationPlan final {
    Sequence before;
    Sequence after;
    std::vector<Id> selectedIds;
    std::uint64_t seed{0};
    std::vector<std::string> changes;
};

struct YtpPresetDescriptor final {
    std::string id;
    std::string name;
    std::string description;
};

class YtpToolkit final {
public:
    [[nodiscard]] static ToolkitResult stutter(Sequence& sequence, const std::vector<Id>& itemIds,
                                               int repeats, Rational sliceDuration,
                                               bool alternateReverse = false);
    [[nodiscard]] static ToolkitResult rapidReverse(Sequence& sequence, const std::vector<Id>& itemIds,
                                                    int segments, Rational segmentDuration);
    [[nodiscard]] static ToolkitResult frameRepeat(Sequence& sequence, const std::vector<Id>& itemIds,
                                                   Rational frameDuration, int sourceFrames,
                                                   int repeatsPerFrame);
    [[nodiscard]] static ToolkitResult rhythmRepeat(Sequence& sequence, const std::vector<Id>& itemIds,
                                                    Rational at, double bpm, int beats,
                                                    Rational gateDuration, bool useMarkers);
    [[nodiscard]] static ToolkitResult speedLadder(Sequence& sequence, const std::vector<Id>& itemIds,
                                                   int steps, double startSpeed, double endSpeed,
                                                   double pitchStep, bool preservePitch);
    [[nodiscard]] static ToolkitResult safeEarrape(Sequence& sequence, const std::vector<Id>& itemIds,
                                                   double intensity = 0.65);
    [[nodiscard]] static ToolkitResult applyVisualPreset(Sequence& sequence,
                                                         const std::vector<Id>& itemIds,
                                                         const std::string& presetId);
    [[nodiscard]] static ToolkitResult applyAudioPreset(Sequence& sequence,
                                                        const std::vector<Id>& itemIds,
                                                        const std::string& presetId);
    [[nodiscard]] static ToolkitResult applyCombinedPreset(Sequence& sequence,
                                                           const std::vector<Id>& itemIds,
                                                           const std::string& presetId);
    [[nodiscard]] static ToolkitResult sentenceMixer(Sequence& sequence,
                                                     const std::vector<Id>& itemIds,
                                                     const std::vector<int>& order);
    [[nodiscard]] static ToolkitResult applyMacro(Sequence& sequence, std::vector<Id> itemIds,
                                                  const YtpMacro& macro);
    [[nodiscard]] static RandomizationPlan previewRandomizer(const Sequence& sequence,
                                                             const std::vector<Id>& itemIds,
                                                             const RandomizerOptions& options);
    static void commitRandomizer(Sequence& sequence, const RandomizationPlan& plan);

    [[nodiscard]] static const std::vector<YtpPresetDescriptor>& visualPresets();
    [[nodiscard]] static const std::vector<YtpPresetDescriptor>& audioPresets();
    [[nodiscard]] static const std::vector<YtpPresetDescriptor>& combinedPresets();
};

} // namespace ytp
