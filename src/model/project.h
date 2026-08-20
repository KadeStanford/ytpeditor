#pragma once

#include "core/rational.h"
#include "core/time_range.h"
#include "model/id.h"
#include "model/timeline.h"

#include <optional>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ytp {

inline constexpr int currentProjectFormatVersion = 6;

struct TranscriptWord final {
    Id id;
    Rational start;
    Rational duration;
    std::string text;
    double confidence{1.0};
    friend bool operator==(const TranscriptWord&, const TranscriptWord&) = default;
};

struct ProjectSettings final {
    int width{1920};
    int height{1080};
    std::int64_t frameRateNumerator{30'000};
    std::int64_t frameRateDenominator{1'001};
    int audioSampleRate{48'000};

    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

struct MediaAsset final {
    Id id;
    std::string path;
    std::string displayName;
    Rational duration;
    std::int64_t frameRateNumerator{0};
    std::int64_t frameRateDenominator{1};
    int width{0};
    int height{0};
    int audioSampleRate{0};
    std::string fingerprint;
    std::string proxyPath;
    bool proxyReady{false};
    std::string bin{"Media"};
    std::int64_t createdAtMs{0};
    std::int64_t lastUsedAtMs{0};
    std::string transcriptionLanguage;
    std::string transcriptionModel;
    std::vector<TranscriptWord> transcript;

    friend bool operator==(const MediaAsset&, const MediaAsset&) = default;
};

struct LibraryClip final {
    Id id;
    Id mediaAssetId;
    TimeRange sourceRange;
    std::string name;
    std::vector<std::string> tags;
    std::string notes;
    std::string color{"#df4f8b"};
    std::string bin{"Clips"};
    bool favorite{false};
    Rational thumbnailTime;
    std::int64_t createdAtMs{0};
    std::int64_t lastUsedAtMs{0};

    friend bool operator==(const LibraryClip&, const LibraryClip&) = default;
};

struct CompoundClip final {
    Id id;
    Id sequenceId;
    std::string name;
    std::string color{"#8a6fd1"};
    bool updateAllInstances{true};
    std::int64_t createdAtMs{0};
    friend bool operator==(const CompoundClip&, const CompoundClip&) = default;
};

class Project final {
public:
    Project();
    explicit Project(std::string name);

    [[nodiscard]] int formatVersion() const noexcept { return formatVersion_; }
    [[nodiscard]] const Id& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const ProjectSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] const std::vector<MediaAsset>& mediaAssets() const noexcept { return mediaAssets_; }
    [[nodiscard]] const std::vector<LibraryClip>& libraryClips() const noexcept { return libraryClips_; }
    [[nodiscard]] const std::vector<CompoundClip>& compoundClips() const noexcept { return compoundClips_; }
    [[nodiscard]] const std::vector<Sequence>& sequences() const noexcept { return sequences_; }

    void setIdentity(Id id, int formatVersion);
    void setName(std::string name);
    void setSettings(ProjectSettings settings);
    void addMediaAsset(MediaAsset asset);
    void updateMediaAsset(MediaAsset asset);
    void addLibraryClip(LibraryClip clip);
    void insertLibraryClip(std::size_t index, LibraryClip clip);
    void updateLibraryClip(LibraryClip clip);
    [[nodiscard]] bool removeLibraryClip(std::string_view id);
    void addCompoundClip(CompoundClip clip);
    void updateCompoundClip(CompoundClip clip);
    [[nodiscard]] bool removeCompoundClip(std::string_view id);
    [[nodiscard]] const CompoundClip* findCompoundClip(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> libraryClipIndex(std::string_view id) const noexcept;
    void setSequences(std::vector<Sequence> sequences);
    void updateSequence(Sequence sequence);
    [[nodiscard]] Sequence* findSequence(std::string_view id) noexcept;
    [[nodiscard]] const Sequence* findSequence(std::string_view id) const noexcept;

    [[nodiscard]] const MediaAsset* findMediaAsset(std::string_view id) const noexcept;
    [[nodiscard]] const LibraryClip* findLibraryClip(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::string> validate() const;

private:
    void validateLibraryClip(const LibraryClip& clip, bool allowExistingId) const;
    int formatVersion_{currentProjectFormatVersion};
    Id id_;
    std::string name_{"Untitled YTP"};
    ProjectSettings settings_;
    std::vector<MediaAsset> mediaAssets_;
    std::vector<LibraryClip> libraryClips_;
    std::vector<CompoundClip> compoundClips_;
    std::vector<Sequence> sequences_;
};

} // namespace ytp
