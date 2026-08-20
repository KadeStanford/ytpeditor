#include "model/timeline.h"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <cmath>

namespace ytp {

Rational Sequence::duration() const {
    Rational result;
    for (const auto& track : tracks) {
        for (const auto& item : track.items) {
            result = std::max(result, item.timelineEnd());
        }
    }
    return result;
}

Track* Sequence::findTrack(const std::string_view soughtId) noexcept {
    const auto found = std::find_if(tracks.begin(), tracks.end(),
        [soughtId](const Track& track) { return track.id == soughtId; });
    return found == tracks.end() ? nullptr : &*found;
}

const Track* Sequence::findTrack(const std::string_view soughtId) const noexcept {
    const auto found = std::find_if(tracks.begin(), tracks.end(),
        [soughtId](const Track& track) { return track.id == soughtId; });
    return found == tracks.end() ? nullptr : &*found;
}

TimelineItem* Sequence::findItem(const std::string_view soughtId) noexcept {
    for (auto& track : tracks) {
        const auto found = std::find_if(track.items.begin(), track.items.end(),
            [soughtId](const TimelineItem& item) { return item.id == soughtId; });
        if (found != track.items.end()) return &*found;
    }
    return nullptr;
}

const TimelineItem* Sequence::findItem(const std::string_view soughtId) const noexcept {
    for (const auto& track : tracks) {
        const auto found = std::find_if(track.items.begin(), track.items.end(),
            [soughtId](const TimelineItem& item) { return item.id == soughtId; });
        if (found != track.items.end()) return &*found;
    }
    return nullptr;
}

std::optional<std::string> Sequence::validate() const {
    if (!isValidId(id) || name.empty()) return "Sequence identity is invalid.";
    const auto quality=static_cast<int>(previewQuality);if(quality<0||quality>4)return "Sequence preview quality is invalid.";
    if(!std::isfinite(beatGrid.bpm)||beatGrid.bpm<20||beatGrid.bpm>400||beatGrid.offset<Rational{}||beatGrid.division<1||beatGrid.division>64)return "Sequence beat grid is invalid.";
    std::unordered_set<std::string> ids;
    std::unordered_set<int> orders;
    for (const auto& track : tracks) {
        if (!isValidId(track.id) || track.name.empty() || track.color.empty() ||
            track.height < 24 || track.order < 0 ||
            track.order >= static_cast<int>(tracks.size()) || !orders.insert(track.order).second ||
            !ids.insert(track.id).second) {
            return "A timeline track is invalid.";
        }
        for (const auto& item : track.items) {
            if (!isValidId(item.id) || item.trackId != track.id ||
                item.timelineStart < Rational{} || item.duration <= Rational{} ||
                ((!item.adjustmentClip && item.nestedSequenceId.empty()) && item.sourceRange.isEmpty()) ||
                item.fadeIn < Rational{} || item.fadeOut < Rational{} ||
                item.fadeIn + item.fadeOut > item.duration || item.captionSize < 12 ||
                item.captionSize > 200 || !std::isfinite(item.captionSize) ||
                item.captionColor.empty() || !ids.insert(item.id).second) {
                return "A timeline item is invalid.";
            }
            if (!std::isfinite(item.speed) || item.speed < 0.01 || item.speed > 100.0)
                return "Timeline item speed is invalid.";
            const auto speedRatio=Rational{static_cast<std::int64_t>(std::llround(item.speed*1'000'000.0)),1'000'000};
            if (validateTransform(item.transform) || validateAudio(item.audio) ||
                !std::isfinite(item.pitchSemitones) || item.pitchSemitones < -48 || item.pitchSemitones > 48 ||
                (!item.freezeFrame&&item.sourceRange.duration()!=item.duration*speedRatio) ||
                (item.freezeFrame&&!item.sourceRange.contains(item.freezeSourceTime))) return "Timeline item effects are invalid.";
            const auto keysInRange=[&](const auto&keys){return std::all_of(keys.begin(),keys.end(),[&](const auto&key){return key.time<=item.duration;});};
            if(!keysInRange(item.audio.gainEnvelope)||!keysInRange(item.audio.panEnvelope))return "Timeline item audio keyframe is outside the clip.";
            for(const auto&channel:item.transform.animation)if(!keysInRange(channel.keyframes))return "Timeline transform keyframe is outside the clip.";
            for (const auto& effect : item.effects) {if (const auto error=validateEffect(effect)) return error;const auto*descriptor=findEffectDescriptor(effect.type);if(!descriptor||descriptor->audio!=(track.kind==TrackKind::Audio))return "Timeline effect type does not match its track.";for(const auto&parameter:effect.parameters)if(!keysInRange(parameter.keyframes))return "Timeline effect keyframe is outside the clip.";}
            for (const auto& mask : item.masks) {
                const auto shape=static_cast<int>(mask.shape);
                if (!isValidId(mask.id) || shape<0||shape>1 || !std::isfinite(mask.x) || !std::isfinite(mask.y) ||
                    !std::isfinite(mask.width) || !std::isfinite(mask.height) || !std::isfinite(mask.feather) ||
                    !std::isfinite(mask.opacity) || mask.x < 0 || mask.y < 0 || mask.width <= 0 ||
                    mask.height <= 0 || mask.x + mask.width > 1.0 || mask.y + mask.height > 1.0 ||
                    mask.feather < 0 || mask.feather > 1 || mask.opacity < 0 || mask.opacity > 1)
                    return "Timeline mask is invalid.";
                for (const auto& channel : mask.animation) {
                    const bool supported = channel.name == "x" || channel.name == "y" ||
                                           channel.name == "width" || channel.name == "height";
                    if (!supported || !std::isfinite(channel.value) ||
                        !std::isfinite(channel.minimum) || !std::isfinite(channel.maximum) ||
                        channel.minimum > channel.maximum || channel.value < channel.minimum || channel.value > channel.maximum)
                        return "Timeline mask animation is invalid.";
                    Rational previous;bool first=true;for(const auto&key:channel.keyframes){const auto interpolation=static_cast<int>(key.interpolation);if(!isValidId(key.id)||key.time<Rational{}||!std::isfinite(key.value)||key.value<channel.minimum||key.value>channel.maximum||interpolation<0||interpolation>2||(!first&&key.time<=previous))return "Timeline mask keyframe is invalid.";previous=key.time;first=false;}
                    if(!keysInRange(channel.keyframes))return "Timeline mask keyframe is outside the clip.";
                }
            }
        }
        if (validateAudio(track.audio)) return "Track audio settings are invalid.";
        for (const auto& effect : track.effects) {if (const auto error=validateEffect(effect)) return error;const auto*descriptor=findEffectDescriptor(effect.type);if(!descriptor||descriptor->audio!=(track.kind==TrackKind::Audio))return "Track effect type does not match its track.";}
    }
    if (validateAudio(masterAudio)) return "Master audio settings are invalid.";
    for (const auto& effect : masterEffects) {if (const auto error=validateEffect(effect)) return error;const auto*descriptor=findEffectDescriptor(effect.type);if(!descriptor||!descriptor->audio)return "Master effects must be audio effects.";}
    for (const auto& marker : markers) {
        if (!isValidId(marker.id) || marker.time < Rational{} || !ids.insert(marker.id).second) {
            return "A timeline marker is invalid.";
        }
    }
    return std::nullopt;
}

Sequence createDefaultSequence() {
    Sequence sequence;
    sequence.id = createId();
    sequence.tracks = {
        Track{.id = createId(), .name = "V2", .kind = TrackKind::Video, .order = 0, .color = "#46688a", .audio={},.effects={}, .items = {}},
        Track{.id = createId(), .name = "V1", .kind = TrackKind::Video, .order = 1, .color = "#3b6f96", .audio={},.effects={}, .items = {}},
        Track{.id = createId(), .name = "A1", .kind = TrackKind::Audio, .order = 2, .color = "#39756f", .audio={},.effects={}, .items = {}},
        Track{.id = createId(), .name = "A2", .kind = TrackKind::Audio, .order = 3, .color = "#32645f", .audio={},.effects={}, .items = {}}
    };
    return sequence;
}

} // namespace ytp
