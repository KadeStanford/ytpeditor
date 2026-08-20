#pragma once

#include "commands/command.h"
#include "model/project.h"

namespace ytp {

class AddLibraryClipCommand final : public Command {
public:
    AddLibraryClipCommand(Project& project, LibraryClip clip);

    void redo() override;
    void undo() override;
    [[nodiscard]] std::string_view description() const noexcept override;

private:
    Project& project_;
    LibraryClip clip_;
    bool applied_{false};
};

class UpdateLibraryClipCommand final : public Command {
public:
    UpdateLibraryClipCommand(Project& project, LibraryClip before, LibraryClip after);
    void redo() override;
    void undo() override;
    [[nodiscard]] std::string_view description() const noexcept override;
private:
    Project& project_;
    LibraryClip before_;
    LibraryClip after_;
};

class RemoveLibraryClipCommand final : public Command {
public:
    RemoveLibraryClipCommand(Project& project, LibraryClip clip, std::size_t index);
    void redo() override;
    void undo() override;
    [[nodiscard]] std::string_view description() const noexcept override;
private:
    Project& project_;
    LibraryClip clip_;
    std::size_t index_;
    std::vector<Id> referencedItemIds_;
    bool applied_{false};
};

} // namespace ytp
