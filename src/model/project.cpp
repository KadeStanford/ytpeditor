#include "model/project.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <utility>

namespace ytp {

Project::Project() : id_(createId()), sequences_{createDefaultSequence()} {}

Project::Project(std::string name) : id_(createId()), name_(std::move(name)), sequences_{createDefaultSequence()} {
    if (name_.empty()) {
        name_ = "Untitled YTP";
    }
}

void Project::setIdentity(Id id, const int formatVersion) {
    if (!isValidId(id)) {
        throw std::invalid_argument("project ID is invalid");
    }
    if (formatVersion <= 0 || formatVersion > currentProjectFormatVersion) {
        throw std::invalid_argument("project format version is unsupported");
    }
    id_ = std::move(id);
    formatVersion_ = formatVersion;
}

void Project::setName(std::string name) {
    if (name.empty()) {
        throw std::invalid_argument("project name cannot be empty");
    }
    name_ = std::move(name);
}

void Project::setSettings(ProjectSettings settings) {
    if (settings.width <= 0 || settings.height <= 0 ||
        settings.frameRateNumerator <= 0 || settings.frameRateDenominator <= 0 ||
        settings.audioSampleRate <= 0) {
        throw std::invalid_argument("project settings must contain positive values");
    }
    settings_ = settings;
}

void Project::addMediaAsset(MediaAsset asset) {
    if (!isValidId(asset.id) || asset.bin.empty()) {
        throw std::invalid_argument("media asset ID is invalid");
    }
    if (asset.path.empty() || asset.displayName.empty() || asset.duration < Rational{}) {
        throw std::invalid_argument("media asset properties are invalid");
    }
    if (findMediaAsset(asset.id) != nullptr) {
        throw std::invalid_argument("media asset ID already exists");
    }
    mediaAssets_.push_back(std::move(asset));
}

void Project::updateMediaAsset(MediaAsset asset) {
    if (!isValidId(asset.id) || asset.path.empty() || asset.displayName.empty() || asset.bin.empty() ||
        asset.duration < Rational{}) {
        throw std::invalid_argument("media asset properties are invalid");
    }
    const auto assetIterator = std::find_if(mediaAssets_.begin(), mediaAssets_.end(),
        [&asset](const MediaAsset& existing) { return existing.id == asset.id; });
    if (assetIterator == mediaAssets_.end()) {
        throw std::invalid_argument("media asset does not exist");
    }
    for (const auto& clip : libraryClips_) {
        if (clip.mediaAssetId == asset.id && clip.sourceRange.end() > asset.duration) {
            throw std::invalid_argument("replacement media is shorter than a referenced clip");
        }
    }
    for (const auto& sequence : sequences_) for (const auto& track : sequence.tracks)
        for (const auto& item : track.items) if (item.mediaAssetId == asset.id && item.sourceRange.end() > asset.duration)
            throw std::invalid_argument("replacement media is shorter than a timeline instance");
    *assetIterator = std::move(asset);
}

void Project::addLibraryClip(LibraryClip clip) {
    insertLibraryClip(libraryClips_.size(), std::move(clip));
}

void Project::insertLibraryClip(const std::size_t index, LibraryClip clip) {
    if (index > libraryClips_.size()) {
        throw std::out_of_range("library clip insertion index is invalid");
    }
    validateLibraryClip(clip, false);
    libraryClips_.insert(libraryClips_.begin() + static_cast<std::ptrdiff_t>(index), std::move(clip));
}

void Project::updateLibraryClip(LibraryClip clip) {
    validateLibraryClip(clip, true);
    const auto item = std::find_if(libraryClips_.begin(), libraryClips_.end(),
        [&clip](const LibraryClip& existing) { return existing.id == clip.id; });
    if (item == libraryClips_.end()) {
        throw std::invalid_argument("library clip does not exist");
    }
    *item = std::move(clip);
}

bool Project::removeLibraryClip(const std::string_view id) {
    const auto item = std::find_if(libraryClips_.begin(), libraryClips_.end(),
        [id](const LibraryClip& clip) { return clip.id == id; });
    if (item == libraryClips_.end()) {
        return false;
    }
    for (auto& sequence : sequences_) for (auto& track : sequence.tracks)
        for (auto& timelineItem : track.items) if (timelineItem.libraryClipId == id)
            timelineItem.libraryClipId.clear();
    libraryClips_.erase(item);
    return true;
}

void Project::addCompoundClip(CompoundClip clip) {
    if (!isValidId(clip.id) || !isValidId(clip.sequenceId) || clip.name.empty() || findCompoundClip(clip.id))
        throw std::invalid_argument("compound clip is invalid");
    if (!findSequence(clip.sequenceId)) throw std::invalid_argument("compound clip sequence is missing");
    compoundClips_.push_back(std::move(clip));
}
void Project::updateCompoundClip(CompoundClip clip) {
    if (!isValidId(clip.id) || !isValidId(clip.sequenceId) || clip.name.empty() || !findSequence(clip.sequenceId))
        throw std::invalid_argument("compound clip is invalid");
    auto found=std::find_if(compoundClips_.begin(),compoundClips_.end(),[&](const auto& value){return value.id==clip.id;});
    if(found==compoundClips_.end())throw std::invalid_argument("compound clip is missing");
    *found=std::move(clip);
}
bool Project::removeCompoundClip(const std::string_view id){const auto found=std::find_if(compoundClips_.begin(),compoundClips_.end(),[&](const auto& value){return value.id==id;});if(found==compoundClips_.end())return false;compoundClips_.erase(found);return true;}
const CompoundClip* Project::findCompoundClip(const std::string_view id) const noexcept {const auto found=std::find_if(compoundClips_.begin(),compoundClips_.end(),[&](const auto& value){return value.id==id;});return found==compoundClips_.end()?nullptr:&*found;}

std::optional<std::size_t> Project::libraryClipIndex(const std::string_view id) const noexcept {
    const auto item = std::find_if(libraryClips_.begin(), libraryClips_.end(),
        [id](const LibraryClip& clip) { return clip.id == id; });
    if (item == libraryClips_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(libraryClips_.begin(), item));
}

void Project::setSequences(std::vector<Sequence> sequences) {
    if (sequences.empty()) throw std::invalid_argument("project needs at least one sequence");
    std::unordered_set<std::string> ids;
    for (const auto& sequence : sequences) {
        if (sequence.validate() || !ids.insert(sequence.id).second) {
            throw std::invalid_argument("project contains an invalid sequence");
        }
    }
    sequences_ = std::move(sequences);
}

void Project::updateSequence(Sequence sequence) {
    if (sequence.validate()) throw std::invalid_argument("sequence is invalid");
    const auto found = std::find_if(sequences_.begin(), sequences_.end(),
        [&sequence](const Sequence& existing) { return existing.id == sequence.id; });
    if (found == sequences_.end()) throw std::invalid_argument("sequence does not exist");
    *found = std::move(sequence);
}

Sequence* Project::findSequence(const std::string_view id) noexcept {
    const auto found = std::find_if(sequences_.begin(), sequences_.end(),
        [id](const Sequence& sequence) { return sequence.id == id; });
    return found == sequences_.end() ? nullptr : &*found;
}

const Sequence* Project::findSequence(const std::string_view id) const noexcept {
    const auto found = std::find_if(sequences_.begin(), sequences_.end(),
        [id](const Sequence& sequence) { return sequence.id == id; });
    return found == sequences_.end() ? nullptr : &*found;
}

const MediaAsset* Project::findMediaAsset(const std::string_view id) const noexcept {
    const auto item = std::find_if(mediaAssets_.begin(), mediaAssets_.end(),
        [id](const MediaAsset& asset) { return asset.id == id; });
    return item == mediaAssets_.end() ? nullptr : &*item;
}

const LibraryClip* Project::findLibraryClip(const std::string_view id) const noexcept {
    const auto item = std::find_if(libraryClips_.begin(), libraryClips_.end(),
        [id](const LibraryClip& clip) { return clip.id == id; });
    return item == libraryClips_.end() ? nullptr : &*item;
}

std::optional<std::string> Project::validate() const {
    if (!isValidId(id_)) {
        return "Project ID is invalid.";
    }
    if (name_.empty()) {
        return "Project name is empty.";
    }
    if (settings_.width <= 0 || settings_.height <= 0 ||
        settings_.frameRateNumerator <= 0 || settings_.frameRateDenominator <= 0 ||
        settings_.audioSampleRate <= 0) {
        return "Project settings are invalid.";
    }

    std::unordered_set<std::string> ids;
    for (const auto& media : mediaAssets_) {
        if (!isValidId(media.id) || media.path.empty() || media.displayName.empty() || media.bin.empty() ||
            media.duration < Rational{} || !ids.insert(media.id).second) {
            return "A media asset is invalid or has a duplicate ID.";
        }
        Rational previous{};
        for (const auto& word : media.transcript) {
            if (!isValidId(word.id) || word.text.empty() || word.start < Rational{} || word.duration <= Rational{} ||
                word.start + word.duration > media.duration || word.start < previous ||
                !std::isfinite(word.confidence) || word.confidence < 0 || word.confidence > 1 || !ids.insert(word.id).second)
                return "A transcript word is invalid.";
            previous = word.start;
        }
    }
    for (const auto& clip : libraryClips_) {
        const auto* media = findMediaAsset(clip.mediaAssetId);
        if (!isValidId(clip.id) || clip.name.empty() || clip.bin.empty() || !ids.insert(clip.id).second ||
            media == nullptr || clip.sourceRange.isEmpty() ||
            clip.sourceRange.start() < Rational{} || clip.sourceRange.end() > media->duration ||
            !clip.sourceRange.contains(clip.thumbnailTime)) {
            return "A library clip is invalid or has a broken media reference.";
        }
    }
    for(const auto& compound:compoundClips_){if(!isValidId(compound.id)||!ids.insert(compound.id).second||compound.name.empty()||!findSequence(compound.sequenceId))return "A compound clip is invalid or has a broken sequence reference.";}
    if (sequences_.empty()) return "Project has no sequence.";
    for (const auto& sequence : sequences_) {
        if (const auto error = sequence.validate()) return error;
        for (const auto& track : sequence.tracks) {
            for (const auto& item : track.items) {
                const auto* media = findMediaAsset(item.mediaAssetId);
                const auto* nested = findSequence(item.nestedSequenceId);
                const bool special = item.adjustmentClip || !item.nestedSequenceId.empty();
                if ((item.adjustmentClip && (!item.mediaAssetId.empty() || !item.nestedSequenceId.empty())) ||
                    (!item.nestedSequenceId.empty() && (!item.mediaAssetId.empty() || !nested || nested->id == sequence.id)) ||
                    (nested && (item.sourceRange.start() < Rational{} || item.sourceRange.end() > nested->duration())) ||
                    (!special && (!media || item.sourceRange.start() < Rational{} || item.sourceRange.end() > media->duration)) ||
                    (!item.libraryClipId.empty() && !findLibraryClip(item.libraryClipId))) {
                    return "A timeline item has a broken media or library reference.";
                }
            }
        }
    }
    std::unordered_map<std::string, int> visit;
    std::function<bool(const Sequence&)> cyclic = [&](const Sequence& sequence) {
        auto& state = visit[sequence.id]; if (state == 1) return true; if (state == 2) return false; state = 1;
        for (const auto& track : sequence.tracks) for (const auto& item : track.items)
            if (!item.nestedSequenceId.empty()) if (const auto* child = findSequence(item.nestedSequenceId); child && cyclic(*child)) return true;
        state = 2; return false;
    };
    for (const auto& sequence : sequences_) if (cyclic(sequence)) return "Nested sequences contain a cycle.";
    return std::nullopt;
}

void Project::validateLibraryClip(const LibraryClip& clip, const bool allowExistingId) const {
    if (!isValidId(clip.id) || clip.name.empty() || clip.bin.empty()) {
        throw std::invalid_argument("library clip identity is invalid");
    }
    if (!allowExistingId && findLibraryClip(clip.id) != nullptr) {
        throw std::invalid_argument("library clip ID already exists");
    }
    const auto* media = findMediaAsset(clip.mediaAssetId);
    if (media == nullptr) {
        throw std::invalid_argument("library clip references missing media");
    }
    if (clip.sourceRange.isEmpty() || clip.sourceRange.start() < Rational{} ||
        clip.sourceRange.end() > media->duration ||
        !clip.sourceRange.contains(clip.thumbnailTime)) {
        throw std::invalid_argument("library clip source range is invalid");
    }
}

} // namespace ytp
