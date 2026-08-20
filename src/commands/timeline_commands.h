#pragma once

#include "commands/command.h"
#include "model/project.h"

#include <string>

namespace ytp {

class UpdateSequenceCommand final : public Command {
public:
    UpdateSequenceCommand(Project& project, Sequence before, Sequence after,
                          std::string description);
    void redo() override;
    void undo() override;
    [[nodiscard]] std::string_view description() const noexcept override;
private:
    Project& project_;
    Sequence before_;
    Sequence after_;
    std::string description_;
};

class UpdateSequencesCommand final : public Command {
public:
    UpdateSequencesCommand(Project& project, std::vector<Sequence> before, std::vector<Sequence> after,
                           std::string description);
    void redo() override; void undo() override;
    [[nodiscard]] std::string_view description() const noexcept override;
private:
    Project& project_; std::vector<Sequence> before_; std::vector<Sequence> after_; std::string description_;
};
class UpdateProjectCommand final : public Command {
public:
    UpdateProjectCommand(Project& project,Project before,Project after,std::string description);
    void redo() override;void undo() override;[[nodiscard]] std::string_view description() const noexcept override;
private:Project& project_;Project before_;Project after_;std::string description_;
};

} // namespace ytp
