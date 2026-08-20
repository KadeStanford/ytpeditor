#pragma once

#include "model/project.h"

#include <QString>
#include <QVariantMap>

#include <optional>

namespace ytp {

struct SessionDocument final {
    Project project;
    QVariantMap workspace;
    qint64 savedAtMs{0};
};

class SessionSerializer final {
public:
    [[nodiscard]] static bool save(const Project& project,
                                   const QVariantMap& workspace,
                                   const QString& filePath,
                                   QString* errorMessage = nullptr);
    [[nodiscard]] static std::optional<SessionDocument> load(
        const QString& filePath, QString* errorMessage = nullptr);
    [[nodiscard]] static bool isSessionFile(const QString& filePath);
};

} // namespace ytp
