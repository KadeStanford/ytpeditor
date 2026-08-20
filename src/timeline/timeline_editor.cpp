#include "timeline/timeline_editor.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace ytp {
namespace {

void sortItems(Track& track) {
    std::stable_sort(track.items.begin(), track.items.end(),
        [](const TimelineItem& left, const TimelineItem& right) {
            return left.timelineStart < right.timelineStart;
        });
}

Rational absolute(const Rational& value) {
    return value < Rational{} ? -value : value;
}

void clampFades(TimelineItem& item) {
    item.fadeIn = std::min(item.fadeIn, item.duration);
    item.fadeOut = std::min(item.fadeOut, item.duration - item.fadeIn);
}

Rational speedRatio(const TimelineItem& item) {
    return Rational{static_cast<std::int64_t>(std::llround(item.speed * 1'000'000.0)),
                    1'000'000};
}

TimeRange sourceSlice(const TimelineItem& item, const Rational& timelineOffset,
                      const Rational& timelineDuration) {
    const auto ratio = speedRatio(item);
    const auto sourceOffset = timelineOffset * ratio;
    const auto sourceDuration = timelineDuration * ratio;
    const auto sourceStart = item.reverse
        ? item.sourceRange.end() - sourceOffset - sourceDuration
        : item.sourceRange.start() + sourceOffset;
    return {sourceStart, sourceDuration};
}

TimelineItem timelineSlice(const TimelineItem& item, const Rational& timelineOffset,
                           const Rational& timelineDuration) {
    auto result = item;
    result.timelineStart = item.timelineStart + timelineOffset;
    result.duration = timelineDuration;
    result.sourceRange = sourceSlice(item, timelineOffset, timelineDuration);
    clampFades(result);
    return result;
}

void requireEditable(const Sequence& sequence, const std::vector<Id>& ids) {
    for (const auto& id : ids) {
        const auto* item = sequence.findItem(id);
        const auto* track = item ? sequence.findTrack(item->trackId) : nullptr;
        if (!item || !track || track->locked) {
            throw std::invalid_argument("timeline selection contains a missing or locked item");
        }
    }
}

void removeSingletonLinks(Sequence& sequence) {
    std::unordered_map<std::string, std::size_t> counts;
    for (const auto& track : sequence.tracks) for (const auto& item : track.items)
        if (!item.linkedGroupId.empty()) ++counts[item.linkedGroupId];
    for (auto& track : sequence.tracks) for (auto& item : track.items)
        if (!item.linkedGroupId.empty() && counts[item.linkedGroupId] < 2)
            item.linkedGroupId.clear();
}

std::unordered_set<std::string> trackSetForItems(const Sequence& sequence,
                                                 const std::vector<Id>& ids) {
    std::unordered_set<std::string> result;
    for (const auto& track : sequence.tracks) {
        if (track.locked) continue;
        for (const auto& item : track.items) {
            if (std::find(ids.begin(), ids.end(), item.id) != ids.end()) result.insert(track.id);
        }
    }
    return result;
}

void shiftForRipple(Sequence& sequence, const Rational& pivot, const Rational& delta,
                    const std::unordered_set<std::string>& affectedTracks,
                    const bool shiftMarkers = true, const bool force = false) {
    if (delta == Rational{} || (!force && sequence.rippleMode == RippleMode::Off)) return;
    for (auto& track : sequence.tracks) {
        const bool included = sequence.rippleMode == RippleMode::AllTracks ||
            affectedTracks.contains(track.id);
        if (!included || track.locked) continue;
        for (auto& item : track.items) {
            if (item.timelineStart >= pivot) item.timelineStart = item.timelineStart + delta;
        }
        sortItems(track);
    }
    if (shiftMarkers && sequence.rippleMarkers) {
        for (auto& marker : sequence.markers) {
            if (marker.time >= pivot) marker.time = std::max(Rational{}, marker.time + delta);
        }
    }
}

void overwriteRange(Track& track, const TimeRange& range,
                    std::unordered_map<std::string, std::string>& rightLinks) {
    std::vector<TimelineItem> result;
    for (auto item : track.items) {
        const TimeRange itemRange{item.timelineStart, item.duration};
        if (!itemRange.intersects(range)) {
            result.push_back(std::move(item));
            continue;
        }
        const bool keepLeft = item.timelineStart < range.start();
        const bool keepRight = item.timelineEnd() > range.end();
        if (keepLeft) {
            auto left = timelineSlice(item, Rational{}, range.start() - item.timelineStart);
            left.fadeOut = Rational{};
            result.push_back(std::move(left));
        }
        if (keepRight) {
            const auto offset = range.end() - item.timelineStart;
            auto right = timelineSlice(item, offset, item.timelineEnd() - range.end());
            right.id = keepLeft ? createId() : item.id;
            right.fadeIn = Rational{};
            if (keepLeft && !right.linkedGroupId.empty()) {
                auto& mapped = rightLinks[right.linkedGroupId];
                if (mapped.empty()) mapped = createId();
                right.linkedGroupId = mapped;
            }
            result.push_back(std::move(right));
        }
    }
    track.items = std::move(result);
    sortItems(track);
}

TimelineItem makeItem(const LibraryClip& clip, const Track& track, const Rational& start,
                      const Id& linkedGroupId) {
    return TimelineItem{
        .id = createId(),
        .libraryClipId = clip.id,
        .mediaAssetId = clip.mediaAssetId,
        .nestedSequenceId = {},
        .adjustmentClip = false,
        .trackId = track.id,
        .timelineStart = start,
        .sourceRange = clip.sourceRange,
        .duration = clip.sourceRange.duration(),
        .linkedGroupId = linkedGroupId,
        .groupId = {},
        .fadeIn = Rational{},
        .fadeOut = Rational{},
        .transform = {},
        .audio = {},
        .speed = 1.0,
        .pitchSemitones = 0.0,
        .preservePitch = true,
        .reverse = false,
        .freezeFrame = false,
        .freezeSourceTime = {},
        .effects = {},
        .masks = {},
        .captionEnabled = false,
        .captionText = {},
        .captionSize = 54.0,
        .captionColor = "white",
        .name = {}
    };
}

std::vector<TimeRange> mergeRanges(std::vector<TimeRange> ranges) {
    std::sort(ranges.begin(), ranges.end(), [](const TimeRange& a, const TimeRange& b) {
        return a.start() < b.start();
    });
    std::vector<TimeRange> merged;
    for (const auto& range : ranges) {
        if (merged.empty() || range.start() > merged.back().end()) {
            merged.push_back(range);
        } else {
            const auto end = std::max(merged.back().end(), range.end());
            merged.back() = TimeRange{merged.back().start(), end - merged.back().start()};
        }
    }
    return merged;
}

} // namespace

std::vector<Id> TimelineEditor::pasteItems(Sequence& sequence,
                                           const std::vector<TimelineItem>& items,
                                           Rational at) {
    if (items.empty()) return {};
    Rational earliest = items.front().timelineStart;
    Rational latest = items.front().timelineEnd();
    std::unordered_set<std::string> affected;
    for (const auto& item : items) {
        earliest = std::min(earliest, item.timelineStart);
        latest = std::max(latest, item.timelineEnd());
        const auto* track = sequence.findTrack(item.trackId);
        if (!track || track->locked)
            throw std::invalid_argument("paste target track is missing or locked");
        affected.insert(track->id);
    }
    at = std::max(Rational{}, at);
    // Paste is always an insert operation: even with general ripple disabled it opens
    // room on the destination tracks instead of laying the copied segment over media.
    shiftForRipple(sequence, at, latest - earliest, affected, true, true);
    std::unordered_map<std::string, std::string> links, groups;
    std::vector<Id> result;
    for (auto item : items) {
        auto* track = sequence.findTrack(item.trackId);
        if (!track || track->locked) throw std::logic_error("paste target changed during edit");
        item.id = createId();
        item.timelineStart = at + (item.timelineStart - earliest);
        if (!item.linkedGroupId.empty()) { auto& id=links[item.linkedGroupId]; if(id.empty())id=createId(); item.linkedGroupId=id; }
        if (!item.groupId.empty()) { auto& id=groups[item.groupId]; if(id.empty())id=createId(); item.groupId=id; }
        result.push_back(item.id); track->items.push_back(std::move(item)); sortItems(*track);
    }
    return result;
}

InsertResult TimelineEditor::insertLibraryClip(
    const Project& project, Sequence& sequence, const std::string_view libraryClipId,
    const std::string_view targetTrackId, Rational timelineStart, const EditMode mode,
    const std::optional<std::string_view> replaceItemId) {
    const auto* clip = project.findLibraryClip(libraryClipId);
    const auto* media = clip ? project.findMediaAsset(clip->mediaAssetId) : nullptr;
    auto* target = sequence.findTrack(targetTrackId);
    if (!clip || !media || !target || target->locked || timelineStart < Rational{}) {
        throw std::invalid_argument("cannot insert library clip on the requested track");
    }

    if (mode == EditMode::Replace) {
        if (!replaceItemId) throw std::invalid_argument("replace edit needs a target item");
        auto* replaced = sequence.findItem(*replaceItemId);
        if (!replaced) throw std::invalid_argument("replace target does not exist");
        const auto* replacedTrack = sequence.findTrack(replaced->trackId);
        if (!replacedTrack || replacedTrack->locked) throw std::invalid_argument("replace track is locked");
        const auto linked = replaced->linkedGroupId;
        const auto slotDuration = replaced->duration;
        std::vector<Id> ids;
        for (auto& track : sequence.tracks) for (auto& item : track.items) {
            if (item.id != replaced->id && (linked.empty() || item.linkedGroupId != linked)) continue;
            if (track.locked) throw std::invalid_argument("replace target is linked to a locked track");
            if ((track.kind == TrackKind::Video && (media->width <= 0 || media->height <= 0)) ||
                (track.kind == TrackKind::Audio && media->audioSampleRate <= 0))
                throw std::invalid_argument("replacement media is incompatible with a linked track");
            const auto ratio = speedRatio(item);
            const auto timelineAvailable = clip->sourceRange.duration() / ratio;
            item.libraryClipId = clip->id; item.mediaAssetId = clip->mediaAssetId;
            item.duration = std::min(item.duration, timelineAvailable);
            item.sourceRange = TimeRange{clip->sourceRange.start(), item.duration * ratio};
            clampFades(item); ids.push_back(item.id);
        }
        return {ids, slotDuration};
    }

    const bool hasVideo = media->width > 0 && media->height > 0;
    const bool hasAudio = media->audioSampleRate > 0;
    if ((target->kind == TrackKind::Video && !hasVideo) ||
        (target->kind == TrackKind::Audio && !hasAudio)) {
        throw std::invalid_argument("media has no stream compatible with the target track");
    }

    std::vector<Track*> insertionTracks{target};
    if (target->kind == TrackKind::Video && hasAudio) {
        const auto audio = std::find_if(sequence.tracks.begin(), sequence.tracks.end(),
            [](const Track& track) { return track.kind == TrackKind::Audio && !track.locked; });
        if (audio != sequence.tracks.end()) insertionTracks.push_back(&*audio);
    }
    std::unordered_set<std::string> affected;
    for (const auto* track : insertionTracks) affected.insert(track->id);
    const auto duration = clip->sourceRange.duration();
    if (mode == EditMode::Insert) {
        shiftForRipple(sequence, timelineStart, duration, affected);
    } else {
        std::unordered_map<std::string, std::string> rightLinks;
        for (auto* track : insertionTracks)
            overwriteRange(*track, TimeRange{timelineStart, duration}, rightLinks);
        removeSingletonLinks(sequence);
    }

    const Id linkedId = insertionTracks.size() > 1 ? createId() : Id{};
    InsertResult result;
    result.duration = duration;
    for (auto* track : insertionTracks) {
        auto item = makeItem(*clip, *track, timelineStart, linkedId);
        result.itemIds.push_back(item.id);
        track->items.push_back(std::move(item));
        sortItems(*track);
    }
    if (sequence.automaticAudioFades) {
        const Rational shortFade{1, 100};
        for (auto* track : insertionTracks) if (track->kind == TrackKind::Audio) {
            auto* inserted = sequence.findItem(result.itemIds.back());
            if (!inserted || inserted->trackId != track->id) {
                for (const auto& id : result.itemIds) if (auto* candidate=sequence.findItem(id); candidate && candidate->trackId==track->id) inserted=candidate;
            }
            if (!inserted) continue;
            for (auto& neighbor : track->items) {
                if (neighbor.id == inserted->id) continue;
                if (neighbor.timelineEnd() == inserted->timelineStart) {
                    neighbor.fadeOut = std::min(shortFade, neighbor.duration);
                    inserted->fadeIn = std::min(shortFade, inserted->duration);
                }
                if (inserted->timelineEnd() == neighbor.timelineStart) {
                    inserted->fadeOut = std::min(shortFade, inserted->duration - inserted->fadeIn);
                    neighbor.fadeIn = std::min(shortFade, neighbor.duration);
                }
            }
        }
    }
    return result;
}

std::vector<Id> TimelineEditor::expandedSelection(const Sequence& sequence,
                                                  const std::vector<Id>& itemIds) {
    std::unordered_set<std::string> selected(itemIds.begin(), itemIds.end());
    std::vector<Id> ordered;
    ordered.reserve(itemIds.size());
    for (const auto& id : itemIds) {
        if (sequence.findItem(id) && std::find(ordered.begin(), ordered.end(), id) == ordered.end())
            ordered.push_back(id);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_set<std::string> links;
        std::unordered_set<std::string> groups;
        for (const auto& id : selected) {
            if (const auto* item = sequence.findItem(id)) {
                if (!item->linkedGroupId.empty()) links.insert(item->linkedGroupId);
                if (!item->groupId.empty()) groups.insert(item->groupId);
            }
        }
        for (const auto& track : sequence.tracks) {
            for (const auto& item : track.items) {
                if ((!item.linkedGroupId.empty() && links.contains(item.linkedGroupId)) ||
                    (!item.groupId.empty() && groups.contains(item.groupId))) {
                    if (selected.insert(item.id).second) {
                        ordered.push_back(item.id);
                        changed = true;
                    }
                }
            }
        }
    }
    return ordered;
}

std::vector<Id> TimelineEditor::splitItems(Sequence& sequence,
                                           const std::vector<Id>& itemIds,
                                           const Rational time) {
    const auto selected = expandedSelection(sequence, itemIds);
    requireEditable(sequence, selected);
    std::vector<Id> rightIds;
    std::unordered_map<std::string, std::string> rightLinks;
    for (auto& track : sequence.tracks) {
        if (track.locked) continue;
        const auto originalCount = track.items.size();
        for (std::size_t index = 0; index < originalCount; ++index) {
            auto& item = track.items[index];
            if (std::find(selected.begin(), selected.end(), item.id) == selected.end() ||
                time <= item.timelineStart || time >= item.timelineEnd()) continue;
            const auto leftDuration = time - item.timelineStart;
            const auto rightDuration = item.timelineEnd() - time;
            TimelineItem original = item;
            TimelineItem right = timelineSlice(original, leftDuration, rightDuration);
            right.id = createId();
            if (!right.linkedGroupId.empty()) {
                auto& mapped = rightLinks[right.linkedGroupId];
                if (mapped.empty()) mapped = createId();
                right.linkedGroupId = mapped;
            }
            right.fadeIn = Rational{};
            item = timelineSlice(original, Rational{}, leftDuration);
            item.fadeOut = Rational{};
            clampFades(item);
            clampFades(right);
            rightIds.push_back(right.id);
            track.items.push_back(std::move(right));
        }
        sortItems(track);
    }
    if (rightIds.empty()) throw std::invalid_argument("split point is outside selected items");
    return rightIds;
}

void TimelineEditor::deleteItems(Sequence& sequence, const std::vector<Id>& itemIds) {
    const auto selected = expandedSelection(sequence, itemIds);
    if (selected.empty()) return;
    requireEditable(sequence, selected);
    std::unordered_set<std::string> selectedSet(selected.begin(), selected.end());
    std::map<std::string, std::vector<TimeRange>> rangesByTrack;
    std::vector<TimeRange> allRanges;
    for (const auto& track : sequence.tracks) {
        if (track.locked) continue;
        for (const auto& item : track.items) {
            if (selectedSet.contains(item.id)) {
                rangesByTrack[track.id].emplace_back(item.timelineStart, item.duration);
                allRanges.emplace_back(item.timelineStart, item.duration);
            }
        }
    }
    for (auto& track : sequence.tracks) {
        if (track.locked) continue;
        std::erase_if(track.items, [&selectedSet](const TimelineItem& item) {
            return selectedSet.contains(item.id);
        });
    }
    if (sequence.rippleMode == RippleMode::Off) return;

    if (sequence.rippleMode == RippleMode::AllTracks) {
        auto ranges = mergeRanges(std::move(allRanges));
        for (auto iterator = ranges.rbegin(); iterator != ranges.rend(); ++iterator) {
            shiftForRipple(sequence, iterator->end(), -iterator->duration(), {}, true);
        }
    } else {
        for (auto& [trackId, ranges] : rangesByTrack) {
            const std::unordered_set<std::string> affected{trackId};
            auto merged = mergeRanges(std::move(ranges));
            for (auto iterator = merged.rbegin(); iterator != merged.rend(); ++iterator) {
                shiftForRipple(sequence, iterator->end(), -iterator->duration(), affected, false);
            }
        }
        if (sequence.rippleMarkers) {
            auto ranges = mergeRanges(std::move(allRanges));
            for (auto iterator = ranges.rbegin(); iterator != ranges.rend(); ++iterator)
                for (auto& marker : sequence.markers) if (marker.time >= iterator->end())
                    marker.time = std::max(Rational{}, marker.time - iterator->duration());
        }
    }
}

std::vector<Id> TimelineEditor::duplicateItems(Sequence& sequence,
                                               const std::vector<Id>& itemIds) {
    const auto selected = expandedSelection(sequence, itemIds);
    if (selected.empty()) return {};
    requireEditable(sequence, selected);
    Rational earliest;
    Rational latest;
    bool initialized = false;
    for (const auto& id : selected) {
        if (const auto* item = sequence.findItem(id)) {
            if (!initialized) {
                earliest = item->timelineStart;
                latest = item->timelineEnd();
                initialized = true;
            } else {
                earliest = std::min(earliest, item->timelineStart);
                latest = std::max(latest, item->timelineEnd());
            }
        }
    }
    const auto span = latest - earliest;
    const auto affected = trackSetForItems(sequence, selected);
    // Duplicate is also an insert operation and must never overlap the following edit.
    shiftForRipple(sequence, latest, span, affected, true, true);

    std::unordered_map<std::string, std::string> linkedIds;
    std::unordered_map<std::string, std::string> groupIds;
    std::vector<Id> copies;
    for (auto& track : sequence.tracks) {
        if (track.locked) continue;
        const auto original = track.items;
        for (const auto& item : original) {
            if (std::find(selected.begin(), selected.end(), item.id) == selected.end()) continue;
            auto copy = item;
            copy.id = createId();
            copy.timelineStart = item.timelineStart + span;
            if (!copy.linkedGroupId.empty()) {
                auto& mapped = linkedIds[copy.linkedGroupId];
                if (mapped.empty()) mapped = createId();
                copy.linkedGroupId = mapped;
            }
            if (!copy.groupId.empty()) {
                auto& mapped = groupIds[copy.groupId];
                if (mapped.empty()) mapped = createId();
                copy.groupId = mapped;
            }
            copies.push_back(copy.id);
            track.items.push_back(std::move(copy));
        }
        sortItems(track);
    }
    return copies;
}

void TimelineEditor::moveItems(Sequence& sequence, const std::vector<Id>& itemIds,
                               Rational newStart,
                               const std::optional<std::string_view> targetTrackId) {
    auto selected = expandedSelection(sequence, itemIds);
    if (selected.empty()) return;
    requireEditable(sequence, selected);
    Rational earliest;
    Rational latest;
    bool initialized = false;
    for (const auto& id : selected) {
        if (const auto* item = sequence.findItem(id)) {
            earliest = initialized ? std::min(earliest, item->timelineStart) : item->timelineStart;
            latest = initialized ? std::max(latest, item->timelineEnd()) : item->timelineEnd();
            initialized = true;
        }
    }
    newStart = std::max(Rational{}, newStart);
    auto delta = newStart - earliest;
    const std::unordered_set<std::string> selectedSet(selected.begin(),selected.end());
    const auto affected=trackSetForItems(sequence,selected);
    const bool rippleAfter=sequence.rippleMode!=RippleMode::Off&&delta!=Rational{};
    if (targetTrackId && selected.size() == 1 &&
        sequence.findItem(selected.front())->trackId != *targetTrackId) {
        auto* item = sequence.findItem(selected.front());
        auto* oldTrack = item ? sequence.findTrack(item->trackId) : nullptr;
        auto* newTrack = sequence.findTrack(*targetTrackId);
        if (!item || !oldTrack || !newTrack || newTrack->locked || oldTrack->locked ||
            oldTrack->kind != newTrack->kind) {
            throw std::invalid_argument("item cannot move to the requested track");
        }
        auto moved = *item;
        moved.trackId = newTrack->id;
        moved.timelineStart = moved.timelineStart + delta;
        std::erase_if(oldTrack->items, [&moved](const TimelineItem& candidate) { return candidate.id == moved.id; });
        newTrack->items.push_back(std::move(moved));
        sortItems(*oldTrack);
        sortItems(*newTrack);
        if(sequence.rippleMarkers&&rippleAfter)
            for(auto&marker:sequence.markers)if(marker.time>=latest)marker.time=std::max(Rational{},marker.time+delta);
        for(auto&track:sequence.tracks){
            const bool rippleTrack=sequence.rippleMode==RippleMode::AllTracks||affected.contains(track.id);
            if(track.locked||!rippleTrack)continue;
            for(auto&candidate:track.items)
                if(candidate.id!=selected.front()&&candidate.timelineStart>=latest)candidate.timelineStart=candidate.timelineStart+delta;
            sortItems(track);
        }
        return;
    }
    for (auto& track : sequence.tracks) {
        if (track.locked) continue;
        const bool rippleTrack=sequence.rippleMode==RippleMode::AllTracks||affected.contains(track.id);
        for (auto& item : track.items) {
            const bool selectedItem=selectedSet.contains(item.id);
            const bool follower=rippleAfter&&rippleTrack&&!selectedItem&&item.timelineStart>=latest;
            if (selectedItem||follower) {
                item.timelineStart = item.timelineStart + delta;
            }
        }
        sortItems(track);
    }
    if(sequence.rippleMarkers&&rippleAfter)
        for(auto&marker:sequence.markers)if(marker.time>=latest)marker.time=std::max(Rational{},marker.time+delta);
}

void TimelineEditor::trimItemEnd(Sequence& sequence, const std::string_view itemId,
                                 const Rational newEnd) {
    const auto* original = sequence.findItem(itemId);
    if (!original || newEnd <= original->timelineStart) throw std::invalid_argument("invalid end trim");
    const auto* originalTrack = sequence.findTrack(original->trackId);
    if (!originalTrack || originalTrack->locked) throw std::invalid_argument("track is locked");
    const auto oldEnd = original->timelineEnd();
    const auto delta = newEnd - oldEnd;
    std::vector<Id> targets{original->id};
    if (!original->linkedGroupId.empty()) {
        for (const auto& track : sequence.tracks) for (const auto& item : track.items)
            if (item.linkedGroupId == original->linkedGroupId) targets.push_back(item.id);
    }
    requireEditable(sequence, targets);
    std::unordered_set<std::string> affected;
    for (auto& track : sequence.tracks) {
        if (track.locked) continue;
        for (auto& item : track.items) {
            if (std::find(targets.begin(), targets.end(), item.id) == targets.end()) continue;
            const auto newDuration = item.duration + delta;
            if (newDuration <= Rational{}) throw std::invalid_argument("trim removes entire linked item");
            const auto sourceDelta = delta * speedRatio(item);
            const auto newSourceDuration = item.sourceRange.duration() + sourceDelta;
            const auto newSourceStart = item.reverse
                ? item.sourceRange.start() - sourceDelta
                : item.sourceRange.start();
            if (newSourceStart < Rational{} || newSourceDuration <= Rational{})
                throw std::invalid_argument("trim exceeds source bounds");
            item.duration = newDuration;
            item.sourceRange = TimeRange{newSourceStart, newSourceDuration};
            clampFades(item);
            affected.insert(track.id);
        }
    }
    shiftForRipple(sequence, oldEnd, delta, affected);
}

void TimelineEditor::trimItemStart(Sequence& sequence, const std::string_view itemId,
                                   const Rational newStart) {
    const auto* original = sequence.findItem(itemId);
    if (!original || newStart < Rational{} || newStart >= original->timelineEnd()) {
        throw std::invalid_argument("invalid start trim");
    }
    const auto* originalTrack = sequence.findTrack(original->trackId);
    if (!originalTrack || originalTrack->locked) throw std::invalid_argument("track is locked");
    const auto delta = newStart - original->timelineStart;
    const auto linkedId = original->linkedGroupId;
    const auto oldEnd = original->timelineEnd();
    std::unordered_set<std::string> affected;
    std::vector<Id> targets{original->id};
    if (!linkedId.empty()) for (const auto& track : sequence.tracks) for (const auto& item : track.items)
        if (item.id != original->id && item.linkedGroupId == linkedId) targets.push_back(item.id);
    requireEditable(sequence, targets);
    for (auto& track : sequence.tracks) {
        if (track.locked) continue;
        for (auto& item : track.items) {
            if (item.id != itemId && (linkedId.empty() || item.linkedGroupId != linkedId)) continue;
            const auto newDuration = item.duration - delta;
            if (newDuration <= Rational{}) throw std::invalid_argument("trim removes entire linked item");
            const auto sourceDelta = delta * speedRatio(item);
            const auto newSourceStart = item.reverse
                ? item.sourceRange.start()
                : item.sourceRange.start() + sourceDelta;
            const auto newSourceDuration = item.sourceRange.duration() - sourceDelta;
            if (newSourceStart < Rational{} || newSourceDuration <= Rational{})
                throw std::invalid_argument("trim exceeds source bounds");
            if (sequence.rippleMode == RippleMode::Off) item.timelineStart = item.timelineStart + delta;
            item.sourceRange = TimeRange{newSourceStart, newSourceDuration};
            item.duration = newDuration;
            clampFades(item);
            affected.insert(track.id);
        }
        sortItems(track);
    }
    if (sequence.rippleMode != RippleMode::Off)
        shiftForRipple(sequence, oldEnd, -delta, affected);
}

void TimelineEditor::slipItem(const Project& project, Sequence& sequence,
                              const std::string_view itemId, const Rational newSourceStart) {
    auto* item = sequence.findItem(itemId);
    if (!item) throw std::invalid_argument("slip target does not exist");
    const auto* track = sequence.findTrack(item->trackId);
    if (!track || track->locked) throw std::invalid_argument("track is locked");
    const auto* media = project.findMediaAsset(item->mediaAssetId);
    if (!media || newSourceStart < Rational{} ||
        newSourceStart + item->sourceRange.duration() > media->duration) {
        throw std::invalid_argument("slip exceeds source media bounds");
    }
    item->sourceRange = TimeRange{newSourceStart, item->sourceRange.duration()};
}

void TimelineEditor::rollEdit(Sequence& sequence, const std::string_view leftItemId,
                              const std::string_view rightItemId, const Rational newBoundary) {
    auto* left = sequence.findItem(leftItemId);
    auto* right = sequence.findItem(rightItemId);
    if (!left || !right || left->trackId != right->trackId || left->timelineEnd() != right->timelineStart ||
        newBoundary <= left->timelineStart || newBoundary >= right->timelineEnd()) {
        throw std::invalid_argument("items do not form a valid roll-edit boundary");
    }
    const auto* track = sequence.findTrack(left->trackId);
    if (!track || track->locked) throw std::invalid_argument("track is locked");
    const auto delta = newBoundary - left->timelineEnd();
    const auto leftSourceDelta = delta * speedRatio(*left);
    const auto rightSourceDelta = delta * speedRatio(*right);
    const auto leftSourceStart = left->reverse
        ? left->sourceRange.start() - leftSourceDelta
        : left->sourceRange.start();
    const auto rightSourceStart = right->reverse
        ? right->sourceRange.start()
        : right->sourceRange.start() + rightSourceDelta;
    const auto leftSourceDuration = left->sourceRange.duration() + leftSourceDelta;
    const auto rightSourceDuration = right->sourceRange.duration() - rightSourceDelta;
    if (leftSourceStart < Rational{} || rightSourceStart < Rational{} ||
        leftSourceDuration <= Rational{} || rightSourceDuration <= Rational{})
        throw std::invalid_argument("roll edit exceeds source bounds");
    left->duration = left->duration + delta;
    left->sourceRange = TimeRange{leftSourceStart, leftSourceDuration};
    right->timelineStart = newBoundary;
    right->duration = right->duration - delta;
    right->sourceRange = TimeRange{rightSourceStart, rightSourceDuration};
    clampFades(*left);
    clampFades(*right);
}

void TimelineEditor::setItemFades(Sequence& sequence, const std::string_view itemId,
                                  const Rational fadeIn, const Rational fadeOut) {
    auto* item = sequence.findItem(itemId);
    if (!item || fadeIn < Rational{} || fadeOut < Rational{} || fadeIn + fadeOut > item->duration) {
        throw std::invalid_argument("invalid item fades");
    }
    const auto* track = sequence.findTrack(item->trackId);
    if (!track || track->locked) throw std::invalid_argument("track is locked");
    item->fadeIn = fadeIn;
    item->fadeOut = fadeOut;
}

void TimelineEditor::setItemSpeed(Sequence& sequence, const std::string_view itemId,
                                  const double speed, const bool preservePitch) {
    if (!std::isfinite(speed) || speed < 0.01 || speed > 100.0) throw std::invalid_argument("invalid speed");
    const auto* original=sequence.findItem(itemId); if(!original) throw std::invalid_argument("speed target missing");
    const auto linked=original->linkedGroupId; const auto oldEnd=original->timelineEnd();
    const auto ratio=Rational{static_cast<std::int64_t>(std::llround(speed*1'000'000.0)),1'000'000};
    const auto selected = expandedSelection(sequence, {original->id});
    requireEditable(sequence, selected);
    std::unordered_set<std::string> affected; Rational primaryDelta;
    for(auto& track:sequence.tracks){if(track.locked)continue;for(auto& item:track.items){if(item.id!=itemId&&(linked.empty()||item.linkedGroupId!=linked))continue;const auto old=item.duration;item.speed=speed;item.preservePitch=preservePitch;item.duration=item.sourceRange.duration()/ratio;clampFades(item);affected.insert(track.id);if(item.id==itemId)primaryDelta=item.duration-old;}}
    shiftForRipple(sequence,oldEnd,primaryDelta,affected);
}
void TimelineEditor::setItemReverse(Sequence& sequence,const std::string_view itemId,const bool reverse){const auto ids=expandedSelection(sequence,{Id{itemId.begin(),itemId.end()}});requireEditable(sequence,ids);for(const auto&id:ids)if(auto* item=sequence.findItem(id))item->reverse=reverse;}
void TimelineEditor::setFreezeFrame(Sequence& sequence,const std::string_view itemId,const bool enabled,const Rational sourceTime){auto*item=sequence.findItem(itemId);const auto*track=item?sequence.findTrack(item->trackId):nullptr;if(!item||!track||track->locked||(enabled&&!item->sourceRange.contains(sourceTime)))throw std::invalid_argument("invalid freeze frame");item->freezeFrame=enabled;if(enabled)item->freezeSourceTime=sourceTime;}

void TimelineEditor::groupItems(Sequence& sequence, const std::vector<Id>& itemIds) {
    if (itemIds.size() < 2) throw std::invalid_argument("group needs at least two items");
    requireEditable(sequence, itemIds);
    const auto groupId = createId();
    for (const auto& id : itemIds) {
        auto* item = sequence.findItem(id);
        if (!item) throw std::invalid_argument("group item does not exist");
        item->groupId = groupId;
    }
}

void TimelineEditor::ungroupItems(Sequence& sequence, const std::vector<Id>& itemIds) {
    requireEditable(sequence, itemIds);
    std::unordered_set<std::string> groups;
    for (const auto& id : itemIds) if (const auto* item = sequence.findItem(id))
        if (!item->groupId.empty()) groups.insert(item->groupId);
    for (auto& track : sequence.tracks) for (auto& item : track.items)
        if (groups.contains(item.groupId)) item.groupId.clear();
}

void TimelineEditor::unlinkItems(Sequence& sequence, const std::vector<Id>& itemIds) {
    requireEditable(sequence, expandedSelection(sequence, itemIds));
    std::unordered_set<std::string> links;
    for (const auto& id : itemIds) if (const auto* item = sequence.findItem(id))
        if (!item->linkedGroupId.empty()) links.insert(item->linkedGroupId);
    for (auto& track : sequence.tracks) for (auto& item : track.items)
        if (links.contains(item.linkedGroupId)) item.linkedGroupId.clear();
}

void TimelineEditor::linkItems(Sequence& sequence, const std::vector<Id>& itemIds) {
    if (itemIds.size() < 2) throw std::invalid_argument("link needs at least two items");
    requireEditable(sequence, itemIds);
    const auto linkId = createId();
    for (const auto& id : itemIds) {
        auto* item = sequence.findItem(id);
        if (!item) throw std::invalid_argument("link item does not exist");
        item->linkedGroupId = linkId;
    }
}

Id TimelineEditor::addTrack(Sequence& sequence, const TrackKind kind, std::string name) {
    if (name.empty()) throw std::invalid_argument("track name cannot be empty");
    Track track;
    track.id = createId();
    track.name = std::move(name);
    track.kind = kind;
    track.order = static_cast<int>(sequence.tracks.size());
    track.color = kind == TrackKind::Video ? "#3b6f96" : "#39756f";
    const auto id = track.id;
    sequence.tracks.push_back(std::move(track));
    return id;
}

void TimelineEditor::removeTrack(Sequence& sequence, const std::string_view trackId) {
    const auto found = std::find_if(sequence.tracks.begin(), sequence.tracks.end(),
        [trackId](const Track& track) { return track.id == trackId; });
    if (found == sequence.tracks.end() || !found->items.empty()) {
        throw std::invalid_argument("only empty tracks can be removed");
    }
    sequence.tracks.erase(found);
    for (std::size_t index = 0; index < sequence.tracks.size(); ++index)
        sequence.tracks[index].order = static_cast<int>(index);
}

Id TimelineEditor::addMarker(Sequence& sequence, const Rational time,
                             std::string label, std::string color) {
    if (time < Rational{}) throw std::invalid_argument("marker time cannot be negative");
    TimelineMarker marker{.id = createId(), .time = time, .label = std::move(label), .color = std::move(color)};
    const auto id = marker.id;
    sequence.markers.push_back(std::move(marker));
    std::stable_sort(sequence.markers.begin(), sequence.markers.end(),
        [](const TimelineMarker& a, const TimelineMarker& b) { return a.time < b.time; });
    return id;
}

void TimelineEditor::removeMarker(Sequence& sequence, const std::string_view markerId) {
    const auto oldSize = sequence.markers.size();
    std::erase_if(sequence.markers, [markerId](const TimelineMarker& marker) { return marker.id == markerId; });
    if (sequence.markers.size() == oldSize) throw std::invalid_argument("marker does not exist");
}

Rational TimelineEditor::snapTime(const Sequence& sequence, const Rational proposed,
                                  const Rational playhead, const Rational tolerance) {
    Rational best = proposed;
    auto bestDistance = tolerance + Rational{1, 1};
    const auto consider = [&](const Rational candidate, Rational& current, Rational& distance) {
        const auto candidateDistance = absolute(candidate - proposed);
        if (candidateDistance <= tolerance && candidateDistance < distance) {
            current = candidate;
            distance = candidateDistance;
        }
    };
    consider(playhead, best, bestDistance);
    consider(Rational{}, best, bestDistance);
    for (const auto& marker : sequence.markers) consider(marker.time, best, bestDistance);
    for (const auto& track : sequence.tracks) for (const auto& item : track.items) {
        consider(item.timelineStart, best, bestDistance);
        consider(item.timelineEnd(), best, bestDistance);
    }
    return best;
}

} // namespace ytp
