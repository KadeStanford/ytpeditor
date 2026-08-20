#pragma once

#include "model/project.h"

#include <QAbstractListModel>

#include <cstddef>
#include <vector>

namespace ytp {

class MediaLibraryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(QString binFilter READ binFilter WRITE setBinFilter NOTIFY binFilterChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        DurationRole,
        ResolutionRole,
        BinRole,
        MissingRole,
        ThumbnailUrlRole
    };

    explicit MediaLibraryModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filterText() const { return filterText_; }
    void setFilterText(const QString& value);
    [[nodiscard]] QString binFilter() const { return binFilter_; }
    void setBinFilter(const QString& value);
    void setProject(const Project* project);
    Q_INVOKABLE void refresh();

signals:
    void filterTextChanged();
    void binFilterChanged();

private:
    const Project* project_{nullptr};
    QString filterText_;
    QString binFilter_;
    std::vector<std::size_t> visibleIndices_;
};

} // namespace ytp
