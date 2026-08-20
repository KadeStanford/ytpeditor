#pragma once

#include "model/project.h"

#include <QString>
#include <QByteArray>

#include <optional>

namespace ytp {

class ProjectSerializer final {
public:
    [[nodiscard]] static bool save(const Project& project,
                                   const QString& filePath,
                                   QString* errorMessage = nullptr);
    [[nodiscard]] static std::optional<Project> load(const QString& filePath,
                                                     QString* errorMessage = nullptr);
    [[nodiscard]] static QByteArray serialize(const Project& project,
                                              bool pretty = false,
                                              QString* errorMessage = nullptr);
    [[nodiscard]] static std::optional<Project> deserialize(const QByteArray& data,
                                                            const QString& baseDirectory,
                                                            QString* errorMessage = nullptr);
};

} // namespace ytp
