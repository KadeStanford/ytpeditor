#include "core/rational.h"
#include "core/time_range.h"
#include "commands/command_stack.h"
#include "commands/library_commands.h"
#include "model/project.h"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Exception, typename Function>
void checkThrows(Function&& function, const std::string_view message) {
    try {
        function();
        check(false, message);
    } catch (const Exception&) {
        check(true, message);
    } catch (...) {
        check(false, message);
    }
}

void rationalTests() {
    check(ytp::Rational{2, 4} == ytp::Rational{1, 2}, "fractions normalize");
    check(ytp::Rational{1, -2} == ytp::Rational{-1, 2}, "sign normalizes");
    check(ytp::Rational{1, 3} + ytp::Rational{1, 6} == ytp::Rational{1, 2},
          "addition is exact");
    check(ytp::Rational{5, 6} - ytp::Rational{1, 3} == ytp::Rational{1, 2},
          "subtraction is exact");
    check(ytp::Rational{2, 3} * ytp::Rational{9, 4} == ytp::Rational{3, 2},
          "multiplication cross-reduces");
    check(ytp::Rational{2, 3} / ytp::Rational{4, 9} == ytp::Rational{3, 2},
          "division is exact");
    check(ytp::Rational{1, 3} < ytp::Rational{1, 2}, "comparison works");
    checkThrows<std::invalid_argument>([] { (void)ytp::Rational{1, 0}; },
                                       "zero denominator is rejected");
    checkThrows<std::overflow_error>(
        [] {
            (void)(ytp::Rational{std::numeric_limits<std::int64_t>::max(), 1} *
                   ytp::Rational{2, 1});
        },
        "multiplication overflow is rejected before evaluation");
}

void frameRateTests() {
    check(ytp::frameDuration(30'000, 1'001) == ytp::Rational{1'001, 30'000},
          "29.97 frame duration is exact");
    check(ytp::timeAtFrame(30'000, 30'000, 1'001) == ytp::Rational{1'001, 1},
          "30000 frames at 29.97 is exactly 1001 seconds");
    check(ytp::timeAtFrame(24, 24) == ytp::Rational{1, 1},
          "24 frames at 24 fps is one second");
    checkThrows<std::invalid_argument>([] { (void)ytp::frameDuration(0); },
                                       "invalid frame rates are rejected");
}

void timeRangeTests() {
    const ytp::TimeRange range{ytp::Rational{1, 1}, ytp::Rational{2, 1}};
    check(range.contains(ytp::Rational{1, 1}), "range contains its start");
    check(range.contains(ytp::Rational{5, 2}), "range contains an interior time");
    check(!range.contains(ytp::Rational{3, 1}), "range excludes its end");
    check(range.intersects(ytp::TimeRange{ytp::Rational{2, 1}, ytp::Rational{2, 1}}),
          "overlapping ranges intersect");
    check(!range.intersects(ytp::TimeRange{ytp::Rational{3, 1}, ytp::Rational{1, 1}}),
          "touching ranges do not intersect");
    checkThrows<std::invalid_argument>(
        [] { (void)ytp::TimeRange{ytp::Rational{}, ytp::Rational{-1, 1}}; },
        "negative range duration is rejected");
}

void commandTests() {
    ytp::Project project{"Undo test"};
    ytp::MediaAsset media{
        .id = ytp::createId(),
        .path = "source.mp4",
        .displayName = "source.mp4",
        .duration = ytp::Rational{10, 1}
    };
    const auto mediaId = media.id;
    project.addMediaAsset(std::move(media));

    ytp::CommandStack commands;
    commands.execute(std::make_unique<ytp::AddLibraryClipCommand>(
        project,
        ytp::LibraryClip{
            .id = ytp::createId(),
            .mediaAssetId = mediaId,
            .sourceRange = ytp::TimeRange{ytp::Rational{1, 1}, ytp::Rational{2, 1}},
            .name = "Reusable fragment",
            .thumbnailTime = ytp::Rational{2, 1}
        }));
    check(project.libraryClips().size() == 1, "command creates a library clip");
    check(commands.canUndo() && !commands.canRedo(), "new command is undoable");
    check(commands.undo(), "library command undoes");
    check(project.libraryClips().empty(), "undo removes the created clip");
    check(commands.redo(), "library command redoes");
    check(project.libraryClips().size() == 1, "redo restores the exact clip");
}

} // namespace

int main() {
    rationalTests();
    frameRateTests();
    timeRangeTests();
    commandTests();

    if (failures == 0) {
        std::cout << "All core tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
