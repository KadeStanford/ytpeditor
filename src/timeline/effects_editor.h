#pragma once
#include "model/project.h"

namespace ytp {

enum class EffectTarget { Item, Track, Master };

class EffectsEditor final {
public:
    static void setTransform(Sequence&, std::string_view itemId, TransformSettings);
    [[nodiscard]] static Id addTransformKeyframe(Sequence&, std::string_view itemId,
                                                  std::string_view property, Rational time,
                                                  double value, KeyframeInterpolation);
    static void setClipAudio(Sequence&, std::string_view itemId, AudioSettings);
    static void setTrackAudio(Sequence&, std::string_view trackId, AudioSettings);
    static void setMasterAudio(Sequence&, AudioSettings);
    [[nodiscard]] static Id addAudioKeyframe(Sequence&, EffectTarget, std::string_view targetId,
                                              std::string_view parameter, Rational time,
                                              double value, KeyframeInterpolation);
    [[nodiscard]] static Id addEffect(Sequence&, EffectTarget, std::string_view targetId,
                                      std::string_view effectType);
    static void removeEffect(Sequence&, EffectTarget, std::string_view targetId, std::string_view effectId);
    static void moveEffect(Sequence&, EffectTarget, std::string_view targetId, std::string_view effectId, int offset);
    static void setEffectEnabled(Sequence&, EffectTarget, std::string_view targetId, std::string_view effectId, bool);
    static void resetEffect(Sequence&, EffectTarget, std::string_view targetId, std::string_view effectId);
    static void setParameter(Sequence&, EffectTarget, std::string_view targetId, std::string_view effectId,
                             std::string_view parameter, double value);
    [[nodiscard]] static Id addKeyframe(Sequence&, EffectTarget, std::string_view targetId,
                                        std::string_view effectId, std::string_view parameter,
                                        Rational time, double value, KeyframeInterpolation);
    static void removeKeyframe(Sequence&, EffectTarget, std::string_view targetId,
                               std::string_view effectId, std::string_view parameter,
                               std::string_view keyframeId);
    static void pasteItemAttributes(Sequence&, std::string_view sourceItemId,
                                    const std::vector<Id>& targets, bool transform,
                                    bool timing, bool audio, bool effects);
};
}
