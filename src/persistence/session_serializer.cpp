#include "persistence/session_serializer.h"

#include "persistence/project_serializer.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace ytp {
namespace {

const QByteArray kMagic{"YTPS1\r\n", 7};

void setError(QString* destination, const QString& message) {
    if (destination) *destination = message;
}

} // namespace

bool SessionSerializer::isSessionFile(const QString& filePath) {
    QFile file(filePath);
    return file.open(QIODevice::ReadOnly) && file.read(kMagic.size()) == kMagic;
}

bool SessionSerializer::save(const Project& project,
                             const QVariantMap& workspace,
                             const QString& filePath,
                             QString* errorMessage) {
    QString projectError;
    const auto projectData = ProjectSerializer::serialize(project, false, &projectError);
    if (projectData.isEmpty()) {
        setError(errorMessage, projectError);
        return false;
    }
    const auto projectDocument = QJsonDocument::fromJson(projectData);
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("ytp-editor-session")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("savedAtMs"), QString::number(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("project"), projectDocument.object()},
        {QStringLiteral("workspace"), QJsonObject::fromVariantMap(workspace)}
    };
    const auto compressed = qCompress(QJsonDocument(root).toJson(QJsonDocument::Compact), 9);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    if (file.write(kMagic) != kMagic.size() || file.write(compressed) != compressed.size() ||
            !file.commit()) {
        setError(errorMessage, file.errorString());
        return false;
    }
    return true;
}

std::optional<SessionDocument> SessionSerializer::load(const QString& filePath,
                                                       QString* errorMessage) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, file.errorString());
        return std::nullopt;
    }
    if (file.read(kMagic.size()) != kMagic) {
        setError(errorMessage, QStringLiteral("Not a YTP Editor session file."));
        return std::nullopt;
    }
    const auto data = qUncompress(file.readAll());
    if (data.isEmpty()) {
        setError(errorMessage, QStringLiteral("The session payload is damaged or empty."));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, parseError.errorString());
        return std::nullopt;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("ytp-editor-session") ||
            root.value(QStringLiteral("formatVersion")).toInt() != 1) {
        setError(errorMessage, QStringLiteral("Unsupported YTP Editor session version."));
        return std::nullopt;
    }
    QString projectError;
    auto project = ProjectSerializer::deserialize(
        QJsonDocument(root.value(QStringLiteral("project")).toObject()).toJson(QJsonDocument::Compact),
        QFileInfo(filePath).absolutePath(), &projectError);
    if (!project) {
        setError(errorMessage, projectError);
        return std::nullopt;
    }
    return SessionDocument{
        .project = std::move(*project),
        .workspace = root.value(QStringLiteral("workspace")).toObject().toVariantMap(),
        .savedAtMs = root.value(QStringLiteral("savedAtMs")).toString().toLongLong()
    };
}

} // namespace ytp
