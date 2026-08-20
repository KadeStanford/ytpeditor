#include "model/project.h"
#include "persistence/project_serializer.h"
#include "persistence/session_serializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>
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

ytp::Project exampleProject() {
    ytp::Project project{"Infomercial remix"};
    ytp::MediaAsset media{
        .id = ytp::createId(),
        .path = "media/source infomercial.mp4",
        .displayName = "source infomercial.mp4",
        .duration = ytp::Rational{60, 1},
        .frameRateNumerator = 30'000,
        .frameRateDenominator = 1'001,
        .width = 1920,
        .height = 1080,
        .audioSampleRate = 48'000,
        .fingerprint = "size:1234:mtime:5678"
    };
    const auto mediaId = media.id;
    project.addMediaAsset(std::move(media));
    project.addLibraryClip(ytp::LibraryClip{
        .id = ytp::createId(),
        .mediaAssetId = mediaId,
        .sourceRange = ytp::TimeRange{ytp::Rational{10, 1}, ytp::Rational{3, 2}},
        .name = "But wait",
        .tags = {"speech", "salesman"},
        .notes = "Clean phrase",
        .color = "#ff5b9e",
        .favorite = true,
        .thumbnailTime = ytp::Rational{21, 2}
    });
    return project;
}

} // namespace

int main() {
    const auto firstId = ytp::createId();
    const auto secondId = ytp::createId();
    check(ytp::isValidId(firstId), "generated ID has UUID shape");
    check(firstId != secondId, "generated IDs are distinct");

    auto project = exampleProject();
    check(!project.validate().has_value(), "example project validates");
    check(project.libraryClips().size() == 1, "library clip is stored separately");

    QTemporaryDir directory;
    check(directory.isValid(), "temporary project directory is available");
    const auto projectPath = directory.filePath("roundtrip.ytp.json");
    QString error;
    check(ytp::ProjectSerializer::save(project, projectPath, &error),
          "valid project saves atomically");
    const auto loaded = ytp::ProjectSerializer::load(projectPath, &error);
    check(loaded.has_value(), "saved project loads");
    if (loaded) {
        check(loaded->id() == project.id(), "project ID round trips");
        check(loaded->name() == project.name(), "project name round trips");
        check(loaded->settings() == project.settings(), "project settings round trip");
        check(loaded->mediaAssets() == project.mediaAssets(), "media metadata round trips");
        check(loaded->libraryClips() == project.libraryClips(), "clip metadata and range round trip");
    }

    const auto sessionPath = directory.filePath("exact-session.ytps");
    const QVariantMap workspace{{"playheadMs", 12'345}, {"pixelsPerSecond", 240.0},
                                {"workspaceMode", 1}, {"selectedIds", QStringList{"clip-a", "clip-b"}}};
    check(ytp::SessionSerializer::save(project, workspace, sessionPath, &error),
          "compact YTPS session saves atomically");
    check(ytp::SessionSerializer::isSessionFile(sessionPath), "YTPS magic identifies session files");
    const auto session = ytp::SessionSerializer::load(sessionPath, &error);
    check(session.has_value() && session->project.id() == project.id() &&
              session->project.mediaAssets() == project.mediaAssets() &&
              session->project.libraryClips() == project.libraryClips() &&
              session->workspace.value("playheadMs").toLongLong() == 12'345 &&
              session->workspace.value("selectedIds").toStringList().size() == 2,
          "YTPS restores the exact project and workspace state");
    check(QFileInfo(sessionPath).size() < QFileInfo(projectPath).size(),
          "compressed YTPS is smaller than indented legacy JSON");

    check(QDir{}.mkpath(directory.filePath("portable/media")),
          "portable media directory is created");
    QFile portableMedia(directory.filePath("portable/media/source.mp4"));
    check(portableMedia.open(QIODevice::WriteOnly), "portable media fixture opens");
    portableMedia.write("fixture");
    portableMedia.close();
    auto portableProject = exampleProject();
    auto portableAsset = portableProject.mediaAssets().front();
    portableAsset.path = "media/source.mp4";
    portableProject.updateMediaAsset(std::move(portableAsset));
    const auto portablePath = directory.filePath("portable/project.ytp.json");
    check(ytp::ProjectSerializer::save(portableProject, portablePath, &error),
          "portable project saves");
    const auto portableLoaded = ytp::ProjectSerializer::load(portablePath, &error);
    check(portableLoaded.has_value(), "portable project loads");
    if (portableLoaded) {
        const auto resolved = QFileInfo(QString::fromStdString(portableLoaded->mediaAssets().front().path));
        check(resolved.isAbsolute() && resolved.canonicalFilePath() == portableMedia.fileName(),
              "existing relative media resolves against the project directory");
    }

    const auto invalidPath = directory.filePath("invalid.ytp.json");
    QFile invalid(invalidPath);
    check(invalid.open(QIODevice::WriteOnly), "invalid test file opens");
    invalid.write("{ definitely not json }");
    invalid.close();
    check(!ytp::ProjectSerializer::load(invalidPath, &error).has_value(),
          "malformed JSON is rejected without throwing");

    try {
        project.addLibraryClip(ytp::LibraryClip{
            .id = ytp::createId(),
            .mediaAssetId = ytp::createId(),
            .sourceRange = ytp::TimeRange{ytp::Rational{}, ytp::Rational{1, 1}},
            .name = "Broken",
            .thumbnailTime = ytp::Rational{1, 2}
        });
        check(false, "broken media reference is rejected");
    } catch (const std::invalid_argument&) {
        check(true, "broken media reference is rejected");
    }

    if (failures == 0) {
        std::cout << "All project persistence tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
