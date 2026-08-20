#include "commands/command_stack.h"

#include <stdexcept>

namespace ytp {

void CommandStack::execute(std::unique_ptr<Command> command) {
    if (!command) {
        throw std::invalid_argument("cannot execute an empty command");
    }
    command->redo();
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(cursor_), commands_.end());
    commands_.push_back(std::move(command));
    if (commands_.size() > maximumHistorySize_)
        commands_.erase(commands_.begin(),
                        commands_.begin() + static_cast<std::ptrdiff_t>(commands_.size() - maximumHistorySize_));
    cursor_ = commands_.size();
}

bool CommandStack::undo() {
    if (!canUndo()) {
        return false;
    }
    commands_[cursor_ - 1]->undo();
    --cursor_;
    return true;
}

bool CommandStack::redo() {
    if (!canRedo()) {
        return false;
    }
    commands_[cursor_]->redo();
    ++cursor_;
    return true;
}

void CommandStack::clear() noexcept {
    commands_.clear();
    cursor_ = 0;
}

std::string_view CommandStack::undoDescription() const noexcept {
    return canUndo() ? commands_[cursor_ - 1]->description() : std::string_view{};
}

std::string_view CommandStack::redoDescription() const noexcept {
    return canRedo() ? commands_[cursor_]->description() : std::string_view{};
}

} // namespace ytp
