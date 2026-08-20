#include "ui/media_library_model.h"

#include "media/media_cache.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace ytp {

MediaLibraryModel::MediaLibraryModel(QObject* parent) : QAbstractListModel(parent) {}

int MediaLibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visibleIndices_.size());
}

QVariant MediaLibraryModel::data(const QModelIndex& index, const int role) const {
    if (!project_ || !index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= visibleIndices_.size()) {
        return {};
    }
    const auto& asset = project_->mediaAssets()[visibleIndices_[static_cast<std::size_t>(index.row())]];
    switch (role) {
    case IdRole: return QString::fromStdString(asset.id);
    case NameRole: return QString::fromStdString(asset.displayName);
    case PathRole: return QString::fromStdString(asset.path);
    case DurationRole: {
        const auto seconds = static_cast<qint64>(asset.duration.asLongDouble());
        return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    case ResolutionRole:
        return asset.width > 0 ? QStringLiteral("%1x%2").arg(asset.width).arg(asset.height)
                               : QStringLiteral("Audio");
    case BinRole: return QString::fromStdString(asset.bin);
    case MissingRole: {
        const QFileInfo file(QString::fromStdString(asset.path));
        const auto fingerprint = QStringLiteral("%1:%2")
            .arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()).toStdString();
        return !file.exists() || (!asset.fingerprint.empty() && asset.fingerprint != fingerprint);
    }
    case ThumbnailUrlRole: {
        const auto clip = std::find_if(project_->libraryClips().begin(), project_->libraryClips().end(),
            [&](const LibraryClip& candidate) { return candidate.mediaAssetId == asset.id; });
        if (clip == project_->libraryClips().end()) return QUrl{};
        const auto path = MediaCache::thumbnailPath(QString::fromStdString(clip->id));
        return QFileInfo::exists(path) ? QUrl::fromLocalFile(path) : QUrl{};
    }
    default: return {};
    }
}

QHash<int, QByteArray> MediaLibraryModel::roleNames() const {
    return {{IdRole, "mediaId"}, {NameRole, "mediaName"}, {PathRole, "mediaPath"},
            {DurationRole, "mediaDuration"}, {ResolutionRole, "mediaResolution"},
            {BinRole, "mediaBin"}, {MissingRole, "mediaMissing"},
            {ThumbnailUrlRole, "mediaThumbnailUrl"}};
}

void MediaLibraryModel::setFilterText(const QString& value) {
    if (filterText_ == value) return;
    filterText_ = value;
    emit filterTextChanged();
    refresh();
}

void MediaLibraryModel::setBinFilter(const QString& value) {
    if (binFilter_ == value) return;
    binFilter_ = value;
    emit binFilterChanged();
    refresh();
}

void MediaLibraryModel::setProject(const Project* project) {
    project_ = project;
    refresh();
}

void MediaLibraryModel::refresh() {
    beginResetModel();
    visibleIndices_.clear();
    if (project_) {
        for (std::size_t index = 0; index < project_->mediaAssets().size(); ++index) {
            const auto& asset = project_->mediaAssets()[index];
            const auto name = QString::fromStdString(asset.displayName);
            const auto path = QString::fromStdString(asset.path);
            const auto bin = QString::fromStdString(asset.bin);
            const bool textMatches = filterText_.trimmed().isEmpty() ||
                name.contains(filterText_.trimmed(), Qt::CaseInsensitive) ||
                path.contains(filterText_.trimmed(), Qt::CaseInsensitive);
            const bool binMatches = binFilter_.trimmed().isEmpty() ||
                bin.compare(binFilter_.trimmed(), Qt::CaseInsensitive) == 0;
            if (textMatches && binMatches) visibleIndices_.push_back(index);
        }
    }
    endResetModel();
}

} // namespace ytp
