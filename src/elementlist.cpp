#include "elementlist.h"

#include <algorithm>

void ElementList::setItems(QVector<Item> items) {
    // Lowest storey first, then by class and name, so like elements sit
    // together under their storey; elements in no storey come last.
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.storeyOrder != b.storeyOrder)
            return a.storeyOrder < b.storeyOrder;
        if (a.ifcClass != b.ifcClass)
            return a.ifcClass < b.ifcClass;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    beginResetModel();
    m_items = std::move(items);
    m_states.clear();
    m_rows.clear();
    for (int i = 0; i < m_items.size(); ++i)
        m_rows.append(i);
    endResetModel();
    refilter();
    emit countChanged();
}

void ElementList::setStates(const QVector<quint8> &states) {
    m_states = states;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {ShownRole});
}

void ElementList::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    refilter();
}

void ElementList::refilter() {
    // Every word has to match somewhere, so "wall 01" finds the walls on
    // the storey called 01.
    const QStringList words = m_filter.split(' ', Qt::SkipEmptyParts);
    QVector<int> rows;
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items.at(i);
        bool matches = true;
        for (const QString &word : words) {
            if (!item.name.contains(word, Qt::CaseInsensitive) && !item.ifcClass.contains(word, Qt::CaseInsensitive)
                && !item.storey.contains(word, Qt::CaseInsensitive) && !item.globalId.contains(word)) {
                matches = false;
                break;
            }
        }
        if (matches)
            rows.append(i);
    }
    if (rows == m_rows)
        return;
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

int ElementList::rowOf(int element) const {
    for (int row = 0; row < m_rows.size(); ++row)
        if (m_items.at(m_rows.at(row)).element == element)
            return row;
    return -1;
}

int ElementList::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ElementList::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Item &item = m_items.at(m_rows.at(index.row()));
    switch (role) {
    case ElementRole: return item.element;
    case NameRole: return item.name;
    case ClassRole: return item.ifcClass;
    case StoreyRole: return item.storey;
    case GlobalIdRole: return item.globalId;
    case ShownRole: return item.element < m_states.size() ? int(m_states.at(item.element)) : 1;
    }
    return {};
}

QHash<int, QByteArray> ElementList::roleNames() const {
    return {{ElementRole, "element"}, {NameRole, "name"}, {ClassRole, "ifcClass"},
            {StoreyRole, "storey"}, {GlobalIdRole, "globalId"}, {ShownRole, "shown"}};
}
