#pragma once

#include "commands/command.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace ytp {

class CommandStack final {
public:
    void execute(std::unique_ptr<Command> command);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void clear() noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < commands_.size(); }
    [[nodiscard]] std::string_view undoDescription() const noexcept;
    [[nodiscard]] std::string_view redoDescription() const noexcept;

private:
    static constexpr std::size_t maximumHistorySize_ = 100;
    std::vector<std::unique_ptr<Command>> commands_;
    std::size_t cursor_{0};
};

} // namespace ytp
