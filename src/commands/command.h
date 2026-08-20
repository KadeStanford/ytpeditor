#pragma once

#include <string_view>

namespace ytp {

class Command {
public:
    virtual ~Command() = default;
    virtual void redo() = 0;
    virtual void undo() = 0;
    [[nodiscard]] virtual std::string_view description() const noexcept = 0;
};

} // namespace ytp

