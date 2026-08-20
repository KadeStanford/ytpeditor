#pragma once

#include "model/project.h"

#include <QAbstractListModel>
#include <QString>

#include <cstddef>
#include <vector>

namespace ytp {

class ClipLibraryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(QString binFilter READ binFilter WRITE setBinFilter NOTIFY binFilterChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY favoritesOnlyChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TagsRole,
        NotesRole,
        ColorRole,
        FavoriteRole,
        DurationRole,
        SourceStartMsRole,
        SourceEndMsRole,
        ThumbnailUrlRole
        , BinRole
        , CreatedAtMsRole
        , LastUsedAtMsRole
    };

    explicit ClipLibraryModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filterText() const { return filterText_; }
    void setFilterText(const QString& value);
    [[nodiscard]] QString binFilter() const { return binFilter_; }
    void setBinFilter(const QString& value);
    [[nodiscard]] QString sortMode() const { return sortMode_; }
    void setSortMode(const QString& value);
    [[nodiscard]] bool favoritesOnly() const noexcept { return favoritesOnly_; }
    void setFavoritesOnly(bool value);
    void setProject(const Project* project);
    Q_INVOKABLE void refresh();

signals:
    void filterTextChanged();
    void binFilterChanged();
    void sortModeChanged();
    void favoritesOnlyChanged();

private:
    [[nodiscard]] bool matchesFilter(const LibraryClip& clip) const;

    const Project* project_{nullptr};
    QString filterText_;
    QString binFilter_;
    QString sortMode_{"Recent"};
    bool favoritesOnly_{false};
    std::vector<std::size_t> visibleIndices_;
};

} // namespace ytp
