#pragma once

#include "core/rational.h"
#include "model/id.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ytp {

enum class KeyframeInterpolation { Hold, Linear, Smooth };
enum class PreviewQuality { Automatic, Full, Half, Quarter, Proxy };

struct Keyframe final {
    Id id;
    Rational time;
    double value{0.0};
    KeyframeInterpolation interpolation{KeyframeInterpolation::Linear};
    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

struct EffectParameter final {
    std::string name;
    double value{0.0};
    double minimum{0.0};
    double maximum{1.0};
    std::string unit;
    std::vector<Keyframe> keyframes;
    friend bool operator==(const EffectParameter&, const EffectParameter&) = default;
};

struct EffectInstance final {
    Id id;
    std::string type;
    bool enabled{true};
    std::vector<EffectParameter> parameters;
    friend bool operator==(const EffectInstance&, const EffectInstance&) = default;
};

struct TransformSettings final {
    double positionX{0.0};
    double positionY{0.0};
    double scaleX{1.0};
    double scaleY{1.0};
    double rotation{0.0};
    double anchorX{0.5};
    double anchorY{0.5};
    double opacity{1.0};
    double cropLeft{0.0};
    double cropTop{0.0};
    double cropRight{0.0};
    double cropBottom{0.0};
    bool fit{true};
    bool flipHorizontal{false};
    bool flipVertical{false};
    std::vector<EffectParameter> animation;
    friend bool operator==(const TransformSettings&, const TransformSettings&) = default;
};

struct AudioSettings final {
    double gainDb{0.0};
    double pan{0.0};
    bool bypass{false};
    std::vector<Keyframe> gainEnvelope;
    std::vector<Keyframe> panEnvelope;
    friend bool operator==(const AudioSettings&, const AudioSettings&) = default;
};

struct EffectDescriptor final {
    std::string type;
    std::string name;
    bool audio{false};
    std::vector<EffectParameter> parameters;
};

[[nodiscard]] const std::vector<EffectDescriptor>& effectCatalog();
[[nodiscard]] const EffectDescriptor* findEffectDescriptor(std::string_view type);
[[nodiscard]] EffectInstance createEffect(std::string_view type);
[[nodiscard]] EffectParameter* findParameter(EffectInstance& effect, std::string_view name);
[[nodiscard]] const EffectParameter* findParameter(const EffectInstance& effect, std::string_view name);
[[nodiscard]] double evaluateParameter(const EffectParameter& parameter, Rational time);
[[nodiscard]] std::optional<std::string> validateEffect(const EffectInstance& effect);
[[nodiscard]] std::optional<std::string> validateTransform(const TransformSettings& transform);
[[nodiscard]] std::optional<std::string> validateAudio(const AudioSettings& audio);

} // namespace ytp
