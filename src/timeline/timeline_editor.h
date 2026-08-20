#pragma once

#include "model/project.h"

#include <optional>
#include <string_view>
#include <vector>

namespace ytp {

struct InsertResult final {
    std::vector<Id> itemIds;
    Rational duration;
};

class TimelineEditor final {
public:
    [[nodiscard]] static InsertResult insertLibraryClip(
        const Project& project, Sequence& sequence, std::string_view libraryClipId,
        std::string_view targetTrackId, Rational timelineStart, EditMode mode,
        std::optional<std::string_view> replaceItemId = std::nullopt);

    [[nodiscard]] static std::vector<Id> splitItems(
        Sequence& sequence, const std::vector<Id>& itemIds, Rational time);
    static void deleteItems(Sequence& sequence, const std::vector<Id>& itemIds);
    [[nodiscard]] static std::vector<Id> duplicateItems(
        Sequence& sequence, const std::vector<Id>& itemIds);
    [[nodiscard]] static std::vector<Id> pasteItems(
        Sequence& sequence, const std::vector<TimelineItem>& items, Rational at);
    static void moveItems(Sequence& sequence, const std::vector<Id>& itemIds,
                          Rational newStart, std::optional<std::string_view> targetTrackId = std::nullopt);
    static void trimItemEnd(Sequence& sequence, std::string_view itemId, Rational newEnd);
    static void trimItemStart(Sequence& sequence, std::string_view itemId, Rational newStart);
    static void slipItem(const Project& project, Sequence& sequence,
                         std::string_view itemId, Rational newSourceStart);
    static void rollEdit(Sequence& sequence, std::string_view leftItemId,
                         std::string_view rightItemId, Rational newBoundary);
    static void setItemFades(Sequence& sequence, std::string_view itemId,
                             Rational fadeIn, Rational fadeOut);
    static void setItemSpeed(Sequence& sequence, std::string_view itemId, double speed,
                             bool preservePitch);
    static void setItemReverse(Sequence& sequence, std::string_view itemId, bool reverse);
    static void setFreezeFrame(Sequence& sequence, std::string_view itemId, bool enabled,
                               Rational sourceTime);

    static void groupItems(Sequence& sequence, const std::vector<Id>& itemIds);
    static void ungroupItems(Sequence& sequence, const std::vector<Id>& itemIds);
    static void unlinkItems(Sequence& sequence, const std::vector<Id>& itemIds);
    static void linkItems(Sequence& sequence, const std::vector<Id>& itemIds);

    [[nodiscard]] static Id addTrack(Sequence& sequence, TrackKind kind, std::string name);
    static void removeTrack(Sequence& sequence, std::string_view trackId);
    [[nodiscard]] static Id addMarker(Sequence& sequence, Rational time,
                                      std::string label, std::string color = "#ffd166");
    static void removeMarker(Sequence& sequence, std::string_view markerId);

    [[nodiscard]] static Rational snapTime(const Sequence& sequence, Rational proposed,
                                           Rational playhead, Rational tolerance);
    [[nodiscard]] static std::vector<Id> expandedSelection(
        const Sequence& sequence, const std::vector<Id>& itemIds);
};

} // namespace ytp
