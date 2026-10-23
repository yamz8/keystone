#pragma once

#include <QAbstractListModel>
#include <QVector>

// The open model's elements for the sidebar, storey by storey, narrowed by a
// search that matches names, classes, storeys, and GlobalIds.
class ElementList : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)

public:
    struct Item {
        int element = -1;
        QString name;
        QString ifcClass;
        QString storey;
        QString globalId;
        // Where the storey sits in the building, for ordering.
        int storeyOrder = 0;
    };
    enum Role { ElementRole = Qt::UserRole + 1, NameRole, ClassRole, StoreyRole, GlobalIdRole, ShownRole };

    explicit ElementList(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    void setItems(QVector<Item> items);
    // Whether each element is drawn (1), ghosted (2), or hidden (0).
    void setStates(const QVector<quint8> &states);

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int total() const { return int(m_items.size()); }
    // The row showing an element, or -1 when the filter hides it.
    Q_INVOKABLE int rowOf(int element) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void filterChanged();
    void countChanged();

private:
    void refilter();

    QVector<Item> m_items;
    QVector<int> m_rows; // indices into m_items that pass the filter
    QVector<quint8> m_states;
    QString m_filter;
};
