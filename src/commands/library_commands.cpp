#include "commands/library_commands.h"

#include <stdexcept>
#include <utility>

namespace ytp {

AddLibraryClipCommand::AddLibraryClipCommand(Project& project, LibraryClip clip)
    : project_(project), clip_(std::move(clip)) {}

void AddLibraryClipCommand::redo() {
    if (applied_) {
        throw std::logic_error("add-library-clip command is already applied");
    }
    project_.addLibraryClip(clip_);
    applied_ = true;
}

void AddLibraryClipCommand::undo() {
    if (!applied_ || !project_.removeLibraryClip(clip_.id)) {
        throw std::logic_error("cannot undo add-library-clip command");
    }
    applied_ = false;
}

std::string_view AddLibraryClipCommand::description() const noexcept {
    return "Create library clip";
}

UpdateLibraryClipCommand::UpdateLibraryClipCommand(Project& project,
                                                   LibraryClip before,
                                                   LibraryClip after)
    : project_(project), before_(std::move(before)), after_(std::move(after)) {
    if (before_.id != after_.id) {
        throw std::invalid_argument("updated library clip must retain its ID");
    }
}

void UpdateLibraryClipCommand::redo() {
    project_.updateLibraryClip(after_);
}

void UpdateLibraryClipCommand::undo() {
    project_.updateLibraryClip(before_);
}

std::string_view UpdateLibraryClipCommand::description() const noexcept {
    return "Edit library clip";
}

RemoveLibraryClipCommand::RemoveLibraryClipCommand(Project& project,
                                                   LibraryClip clip,
                                                   const std::size_t index)
    : project_(project), clip_(std::move(clip)), index_(index) {
    for (const auto& sequence : project_.sequences()) for (const auto& track : sequence.tracks)
        for (const auto& item : track.items) if (item.libraryClipId == clip_.id)
            referencedItemIds_.push_back(item.id);
}

void RemoveLibraryClipCommand::redo() {
    if (applied_ || !project_.removeLibraryClip(clip_.id)) {
        throw std::logic_error("cannot remove library clip");
    }
    applied_ = true;
}

void RemoveLibraryClipCommand::undo() {
    if (!applied_) {
        throw std::logic_error("remove-library-clip command is not applied");
    }
    project_.insertLibraryClip(index_, clip_);
    for (const auto& sequenceValue : project_.sequences()) {
        auto* sequence = project_.findSequence(sequenceValue.id);
        for (const auto& itemId : referencedItemIds_) if (auto* item = sequence->findItem(itemId))
            item->libraryClipId = clip_.id;
    }
    applied_ = false;
}

std::string_view RemoveLibraryClipCommand::description() const noexcept {
    return "Delete library clip";
}

} // namespace ytp
