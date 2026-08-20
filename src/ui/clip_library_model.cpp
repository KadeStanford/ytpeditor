#include "ui/clip_library_model.h"

#include "media/media_cache.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace ytp {

ClipLibraryModel::ClipLibraryModel(QObject* parent) : QAbstractListModel(parent) {}

int ClipLibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(visibleIndices_.size());
}

QVariant ClipLibraryModel::data(const QModelIndex& index, const int role) const {
    if (!project_ || !index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= visibleIndices_.size()) {
        return {};
    }
    const auto& clip = project_->libraryClips()[visibleIndices_[static_cast<std::size_t>(index.row())]];
    const auto milliseconds = [](const Rational& time) {
        return static_cast<qint64>(time.asLongDouble() * 1'000.0L);
    };
    switch (role) {
    case IdRole: return QString::fromStdString(clip.id);
    case NameRole: return QString::fromStdString(clip.name);
    case TagsRole: {
        QStringList tags;
        for (const auto& tag : clip.tags) {
            tags.append(QString::fromStdString(tag));
        }
        return tags;
    }
    case NotesRole: return QString::fromStdString(clip.notes);
    case ColorRole: return QString::fromStdString(clip.color);
    case FavoriteRole: return clip.favorite;
    case DurationRole: {
        const auto totalMs = milliseconds(clip.sourceRange.duration());
        if (totalMs < 1'000) return QStringLiteral("%1 ms").arg(totalMs);
        if (totalMs < 10'000)
            return QStringLiteral("%1 s").arg(static_cast<double>(totalMs) / 1'000.0, 0, 'f', 2);
        return QStringLiteral("%1 s").arg(static_cast<double>(totalMs) / 1'000.0, 0, 'f', 1);
    }
    case SourceStartMsRole: return milliseconds(clip.sourceRange.start());
    case SourceEndMsRole: return milliseconds(clip.sourceRange.end());
    case ThumbnailUrlRole: {
        const auto path = MediaCache::thumbnailPath(QString::fromStdString(clip.id));
        if (!QFileInfo::exists(path)) return QUrl{};
        auto url = QUrl::fromLocalFile(path);
        // A clip keeps the same cache path when its source range is edited. Make the
        // range part of the URL identity so QML cannot retain the previous frame.
        url.setQuery(QStringLiteral("range=%1-%2")
                         .arg(milliseconds(clip.sourceRange.start()))
                         .arg(milliseconds(clip.sourceRange.end())));
        return url;
    }
    case BinRole: return QString::fromStdString(clip.bin);
    case CreatedAtMsRole: return clip.createdAtMs;
    case LastUsedAtMsRole: return clip.lastUsedAtMs;
    default: return {};
    }
}

QHash<int, QByteArray> ClipLibraryModel::roleNames() const {
    return {{IdRole, "clipId"}, {NameRole, "clipName"}, {TagsRole, "clipTags"},
            {NotesRole, "clipNotes"}, {ColorRole, "clipColor"},
            {FavoriteRole, "clipFavorite"}, {DurationRole, "clipDuration"},
            {SourceStartMsRole, "sourceStartMs"}, {SourceEndMsRole, "sourceEndMs"},
            {ThumbnailUrlRole, "thumbnailUrl"}, {BinRole, "clipBin"},
            {CreatedAtMsRole, "createdAtMs"}, {LastUsedAtMsRole, "lastUsedAtMs"}};
}

void ClipLibraryModel::setBinFilter(const QString& value) {
    if (binFilter_ == value) {
        return;
    }
    binFilter_ = value;
    emit binFilterChanged();
    refresh();
}

void ClipLibraryModel::setSortMode(const QString& value) {
    if (sortMode_ == value) {
        return;
    }
    sortMode_ = value;
    emit sortModeChanged();
    refresh();
}

void ClipLibraryModel::setFavoritesOnly(const bool value) {
    if (favoritesOnly_ == value) {
        return;
    }
    favoritesOnly_ = value;
    emit favoritesOnlyChanged();
    refresh();
}

void ClipLibraryModel::setFilterText(const QString& value) {
    if (filterText_ == value) {
        return;
    }
    filterText_ = value;
    emit filterTextChanged();
    refresh();
}

void ClipLibraryModel::setProject(const Project* project) {
    project_ = project;
    refresh();
}

void ClipLibraryModel::refresh() {
    beginResetModel();
    visibleIndices_.clear();
    if (project_) {
        for (std::size_t index = 0; index < project_->libraryClips().size(); ++index) {
            if (matchesFilter(project_->libraryClips()[index])) {
                visibleIndices_.push_back(index);
            }
        }
        std::stable_sort(visibleIndices_.begin(), visibleIndices_.end(),
            [this](const std::size_t left, const std::size_t right) {
                const auto& a = project_->libraryClips()[left];
                const auto& b = project_->libraryClips()[right];
                if (sortMode_ == "Name") {
                    return QString::fromStdString(a.name).compare(
                        QString::fromStdString(b.name), Qt::CaseInsensitive) < 0;
                }
                if (sortMode_ == "Duration") {
                    return a.sourceRange.duration() < b.sourceRange.duration();
                }
                if (sortMode_ == "Recently used") {
                    return a.lastUsedAtMs > b.lastUsedAtMs;
                }
                return a.createdAtMs > b.createdAtMs;
            });
    }
    endResetModel();
}

bool ClipLibraryModel::matchesFilter(const LibraryClip& clip) const {
    if (favoritesOnly_ && !clip.favorite) {
        return false;
    }
    if (!binFilter_.trimmed().isEmpty() &&
        QString::fromStdString(clip.bin).compare(binFilter_.trimmed(), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const auto needle = filterText_.trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    if (QString::fromStdString(clip.name).contains(needle, Qt::CaseInsensitive) ||
        QString::fromStdString(clip.notes).contains(needle, Qt::CaseInsensitive)) {
        return true;
    }
    for (const auto& tag : clip.tags) {
        if (QString::fromStdString(tag).contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace ytp
