#include "commands/timeline_commands.h"

#include <utility>

namespace ytp {

UpdateSequenceCommand::UpdateSequenceCommand(Project& project, Sequence before,
                                             Sequence after, std::string description)
    : project_(project), before_(std::move(before)), after_(std::move(after)),
      description_(std::move(description)) {}

void UpdateSequenceCommand::redo() { project_.updateSequence(after_); }
void UpdateSequenceCommand::undo() { project_.updateSequence(before_); }
std::string_view UpdateSequenceCommand::description() const noexcept { return description_; }

UpdateSequencesCommand::UpdateSequencesCommand(Project& project,std::vector<Sequence> before,std::vector<Sequence> after,std::string description)
    :project_(project),before_(std::move(before)),after_(std::move(after)),description_(std::move(description)){}
void UpdateSequencesCommand::redo(){project_.setSequences(after_);} void UpdateSequencesCommand::undo(){project_.setSequences(before_);}
std::string_view UpdateSequencesCommand::description() const noexcept{return description_;}
UpdateProjectCommand::UpdateProjectCommand(Project&project,Project before,Project after,std::string description):project_(project),before_(std::move(before)),after_(std::move(after)),description_(std::move(description)){}
void UpdateProjectCommand::redo(){project_=after_;}void UpdateProjectCommand::undo(){project_=before_;}std::string_view UpdateProjectCommand::description()const noexcept{return description_;}

} // namespace ytp
