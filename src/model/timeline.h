#pragma once

#include "core/rational.h"
#include "core/time_range.h"
#include "model/id.h"
#include "model/effects.h"

#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace ytp {

enum class TrackKind { Video, Audio };
enum class RippleMode { Off, AffectedTracks, AllTracks };
enum class EditMode { Insert, Overwrite, Replace };
enum class MaskShape { Rectangle, Ellipse };

struct MaskSettings final {
    Id id;
    MaskShape shape{MaskShape::Rectangle};
    double x{0.1};
    double y{0.1};
    double width{0.8};
    double height{0.8};
    double feather{0.0};
    double opacity{1.0};
    bool inverted{false};
    std::vector<EffectParameter> animation;
    friend bool operator==(const MaskSettings&, const MaskSettings&) = default;
};

struct TimelineItem final {
    Id id;
    Id libraryClipId;
    Id mediaAssetId;
    Id nestedSequenceId;
    bool adjustmentClip{false};
    Id trackId;
    Rational timelineStart;
    TimeRange sourceRange;
    Rational duration;
    Id linkedGroupId;
    Id groupId;
    Rational fadeIn;
    Rational fadeOut;
    TransformSettings transform;
    AudioSettings audio;
    double speed{1.0};
    double pitchSemitones{0.0};
    bool preservePitch{true};
    bool reverse{false};
    bool freezeFrame{false};
    Rational freezeSourceTime;
    std::vector<EffectInstance> effects;
    std::vector<MaskSettings> masks;
    bool captionEnabled{false};
    std::string captionText;
    double captionSize{54.0};
    std::string captionColor{"white"};
    std::string name;

    [[nodiscard]] Rational timelineEnd() const { return timelineStart + duration; }
    friend bool operator==(const TimelineItem&, const TimelineItem&) = default;
};

struct Track final {
    Id id;
    std::string name;
    TrackKind kind{TrackKind::Video};
    int order{0};
    bool locked{false};
    bool muted{false};
    bool solo{false};
    bool visible{true};
    int height{60};
    std::string color{"#3b6f96"};
    AudioSettings audio;
    std::vector<EffectInstance> effects;
    std::vector<TimelineItem> items;

    friend bool operator==(const Track&, const Track&) = default;
};

struct TimelineMarker final {
    Id id;
    Rational time;
    std::string label;
    std::string color{"#ffd166"};

    friend bool operator==(const TimelineMarker&, const TimelineMarker&) = default;
};

struct BeatGrid final {
    bool enabled{false};
    double bpm{120.0};
    Rational offset;
    int division{4};
    friend bool operator==(const BeatGrid&, const BeatGrid&) = default;
};

struct Sequence final {
    Id id;
    std::string name{"Main"};
    RippleMode rippleMode{RippleMode::AllTracks};
    bool rippleMarkers{true};
    bool automaticAudioFades{true};
    PreviewQuality previewQuality{PreviewQuality::Automatic};
    AudioSettings masterAudio;
    std::vector<EffectInstance> masterEffects;
    bool masterLimiter{true};
    BeatGrid beatGrid;
    std::vector<Track> tracks;
    std::vector<TimelineMarker> markers;

    [[nodiscard]] Rational duration() const;
    [[nodiscard]] Track* findTrack(std::string_view id) noexcept;
    [[nodiscard]] const Track* findTrack(std::string_view id) const noexcept;
    [[nodiscard]] TimelineItem* findItem(std::string_view id) noexcept;
    [[nodiscard]] const TimelineItem* findItem(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::string> validate() const;

    friend bool operator==(const Sequence&, const Sequence&) = default;
};

[[nodiscard]] Sequence createDefaultSequence();

} // namespace ytp
