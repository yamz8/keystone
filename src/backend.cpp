#include "backend.h"

#include <QFileInfo>
#include <algorithm>

#include "filepicker.h"
#include "portalfilepicker.h"

Backend::Backend(QObject *parent) : Backend(new PortalFilePicker(), QString(), parent) {}

Backend::Backend(FilePicker *filePicker, const QString &cacheDirectory, QObject *parent)
    : QObject(parent), m_filePicker(filePicker), m_loader(cacheDirectory) {
    if (!m_filePicker->parent())
        m_filePicker->setParent(this);
    wireFilePicker();

    connect(&m_loader, &Loader::finished, this, [this](const Loader::Result &result) {
        clearSelection();
        m_mesh = result.mesh;
        m_info = result.info;
        m_mesh->focusBounds(&m_focusMin, &m_focusMax);
        m_focusCenter = (m_focusMin + m_focusMax) / 2;
        m_focusRadius = (m_focusMax - m_focusMin).length() / 2;
        describeElements();
        m_storeyLimit = int(m_storeyNames.size());
        updateVisibility();
        m_path = result.path;
        m_warning = result.warning;
        m_error.clear();
        emit meshChanged();
        emit storeyLimitChanged();
        emit sectionChanged();
        setState(Ready);
        if (!m_warning.isEmpty())
            emit notice(m_warning);
    });
    connect(&m_loader, &Loader::failed, this, &Backend::fail);
    connect(&m_loader, &Loader::progressChanged, this, &Backend::loadProgressChanged);
}

void Backend::wireFilePicker() {
    connect(m_filePicker, &FilePicker::openSelected, this, &Backend::openUrl);
    connect(m_filePicker, &FilePicker::failed, this, [this](const QString &message) { reject(QString(), message); });
}

QString Backend::fileName() const {
    const QString path = m_state == Loading || m_state == Failed ? m_loadingPath : m_path;
    return QFileInfo(path).fileName();
}

QString Backend::schema() const {
    return m_info ? m_info->schema() : QString();
}

QVariantList Backend::materials() const {
    QVariantList list;
    if (!m_mesh)
        return list;
    for (const Mesh::Material &material : m_mesh->materials)
        list.append(QVariantMap{{"color", material.color.name(QColor::HexRgb)},
                                {"opacity", qBound(0.0f, material.opacity, 1.0f)}});
    return list;
}

QVector3D Backend::selectionCenter() const {
    if (!m_mesh || m_selected < 0)
        return {};
    const Mesh::Element &element = m_mesh->elements.at(m_selected);
    return (element.min + element.max) / 2;
}

float Backend::selectionRadius() const {
    if (!m_mesh || m_selected < 0)
        return 0;
    const Mesh::Element &element = m_mesh->elements.at(m_selected);
    return (element.max - element.min).length() / 2;
}

void Backend::open() {
    m_filePicker->openModel();
}

bool Backend::openUrl(const QUrl &url) {
    // Only local files: a remote or network-share URL would reach IfcConvert
    // as something other than the file the user meant.
    if (!url.isLocalFile() || url.toLocalFile().startsWith("//")) {
        reject(url.toString(), tr("Keystone opens files on this computer only."));
        return false;
    }
    return openPath(url.toLocalFile());
}

bool Backend::openPath(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        reject(info.absoluteFilePath(), tr("%1 doesn't exist.").arg(info.fileName()));
        return false;
    }
    if (!info.isFile() || !info.isReadable()) {
        reject(info.absoluteFilePath(), tr("%1 can't be read.").arg(info.fileName()));
        return false;
    }
    m_loadingPath = info.absoluteFilePath();
    m_error.clear();
    setState(Loading);
    m_loader.start(m_loadingPath);
    return true;
}

void Backend::reject(const QString &path, const QString &message) {
    // An open that never started leaves a load in progress, and the file it
    // names, alone.
    if (m_state == Loading) {
        emit notice(message);
        return;
    }
    if (!path.isEmpty())
        m_loadingPath = path;
    fail(message);
}

void Backend::fail(const QString &message) {
    m_error = message;
    // A failed open leaves the model that was already open, if any, alone.
    setState(m_mesh ? Ready : Failed);
    if (m_mesh)
        emit notice(message);
}

void Backend::setState(State state) {
    m_state = state;
    emit stateChanged();
}

void Backend::describeElements() {
    const int count = int(m_mesh->elements.size());
    m_storeyNames.clear();
    m_storeyOfElement.fill(-1, count);
    m_disciplines.fill(IfcInfo::Architecture, count);
    QHash<qint64, int> storeyIndex;
    if (m_info) {
        for (const IfcInfo::Storey &storey : m_info->storeys()) {
            storeyIndex.insert(storey.id, int(m_storeyNames.size()));
            m_storeyNames.append(storey.name.isEmpty() ? tr("Storey %1").arg(m_storeyNames.size() + 1) : storey.name);
        }
    }
    QVector<ElementList::Item> items;
    items.reserve(count);
    for (int e = 0; e < count; ++e) {
        const QString globalId = m_mesh->elements.at(e).globalId;
        ElementList::Item item;
        item.element = e;
        item.globalId = globalId;
        if (m_info && m_info->contains(globalId)) {
            m_storeyOfElement[e] = storeyIndex.value(m_info->storeyIdOf(globalId), -1);
            m_disciplines[e] = m_info->disciplines(globalId);
            item.ifcClass = m_info->className(globalId);
            item.name = m_info->nameOf(globalId);
        }
        const int storey = m_storeyOfElement.at(e);
        item.storey = storey >= 0 ? m_storeyNames.at(storey) : tr("Not in a storey");
        item.storeyOrder = storey >= 0 ? storey : int(m_storeyNames.size());
        items.append(item);
    }
    m_elements.setItems(items);
}

void Backend::updateVisibility() {
    if (!m_mesh)
        return;
    const int mask = m_xray == Structure ? IfcInfo::Structure
                   : m_xray == Services ? IfcInfo::Services
                   : m_xray == Architecture ? IfcInfo::Architecture : 0;
    QVector<quint8> visibility(m_mesh->elements.size(), Shown);
    for (int e = 0; e < visibility.size(); ++e) {
        // Elements in no storey (the site, the terrain) stay put as storeys
        // are peeled away.
        const int storey = m_storeyOfElement.value(e, -1);
        if (storey >= 0 && storey >= m_storeyLimit)
            visibility[e] = Hidden;
        else if (mask && !(m_disciplines.value(e, IfcInfo::Architecture) & mask))
            visibility[e] = Ghosted;
    }
    m_visibility = visibility;
    ++m_visibilityRevision;
    m_elements.setStates(m_visibility);
    if (m_selected >= 0 && m_visibility.at(m_selected) == Hidden)
        clearSelection();
    emit visibilityChanged();
}

bool Backend::pickable(int element) const {
    return element >= 0 && element < m_visibility.size() && m_visibility.at(element) == Shown;
}

int Backend::shownCount() const {
    return int(std::count(m_visibility.begin(), m_visibility.end(), quint8(Shown)));
}

void Backend::setStoreyLimit(int limit) {
    // At least the lowest storey stays, so there's always a floor to stand on.
    limit = m_storeyNames.isEmpty() ? 0 : qBound(1, limit, int(m_storeyNames.size()));
    if (limit == m_storeyLimit)
        return;
    m_storeyLimit = limit;
    emit storeyLimitChanged();
    updateVisibility();
}

void Backend::setXray(XRay xray) {
    if (xray == m_xray)
        return;
    m_xray = xray;
    emit xrayChanged();
    updateVisibility();
}

void Backend::cycleXray() {
    setXray(XRay((int(m_xray) + 1) % 4));
}

void Backend::setSectionAxis(SectionAxis axis) {
    if (axis == m_sectionAxis)
        return;
    m_sectionAxis = axis;
    emit sectionChanged();
}

void Backend::cycleSection() {
    setSectionAxis(SectionAxis((int(m_sectionAxis) + 1) % 4));
}

void Backend::setSectionPosition(double position) {
    position = qBound(0.0, position, 1.0);
    if (qFuzzyCompare(position + 1, m_sectionPosition + 1))
        return;
    m_sectionPosition = position;
    emit sectionChanged();
}

void Backend::setSectionFlipped(bool flipped) {
    if (flipped == m_sectionFlipped)
        return;
    m_sectionFlipped = flipped;
    emit sectionChanged();
}

namespace {
int sectionAxisIndex(Backend::SectionAxis axis) {
    return axis == Backend::Plan ? 1 : axis == Backend::AlongX ? 0 : 2;
}
}

QVector4D Backend::sectionPlane() const {
    if (m_sectionAxis == NoSection)
        return QVector4D(0, 0, 0, 1);
    const int axis = sectionAxisIndex(m_sectionAxis);
    // A hair past the bounds at either end, so 0 and 1 cut everything or
    // nothing rather than leaving a face flickering on the plane.
    const float margin = 0.001f * qMax(1.0f, m_focusRadius);
    const float lo = m_focusMin[axis] - margin, hi = m_focusMax[axis] + margin;
    const float value = lo + float(m_sectionPosition) * (hi - lo);
    QVector3D normal;
    normal[axis] = m_sectionFlipped ? -1 : 1;
    return QVector4D(normal, m_sectionFlipped ? -value : value);
}

QVector3D Backend::sectionCenter() const {
    QVector3D center = m_focusCenter;
    if (m_sectionAxis != NoSection) {
        const int axis = sectionAxisIndex(m_sectionAxis);
        const QVector4D plane = sectionPlane();
        center[axis] = plane.w() * plane[axis];
    }
    return center;
}

void Backend::selectAt(const QVector3D &near, const QVector3D &far) {
    if (!m_mesh)
        return;
    const QVector4D plane = sectionPlane();
    const Mesh::Hit hit = m_mesh->pick(near, far - near, [this](int e) { return pickable(e); },
                                       m_sectionAxis != NoSection ? &plane : nullptr);
    if (hit.element < 0)
        clearSelection();
    else
        select(hit.element);
}

QVariant Backend::hitPoint(const QVector3D &near, const QVector3D &far) const {
    if (!m_mesh)
        return {};
    // Zooming lands on what's drawn, ghosts included, but not on what the
    // section plane has cut away or the storey slider has hidden.
    const QVector4D plane = sectionPlane();
    const Mesh::Hit hit = m_mesh->pick(near, far - near,
                                       [this](int e) { return e < m_visibility.size() && m_visibility.at(e) != Hidden; },
                                       m_sectionAxis != NoSection ? &plane : nullptr);
    return hit.element < 0 ? QVariant() : QVariant(hit.point);
}

void Backend::select(int element) {
    // Clicks only land on drawn elements, but the tree can pick a ghosted one.
    if (!m_mesh || element < 0 || element >= m_mesh->elements.size() || m_visibility.value(element) == Hidden) {
        clearSelection();
        return;
    }
    m_selected = element;
    refreshSelection();
}

void Backend::clearSelection() {
    if (m_selected < 0 && m_selection.isEmpty())
        return;
    m_selected = -1;
    m_selection.clear();
    emit selectionChanged();
}

void Backend::refreshSelection() {
    if (!m_mesh || m_selected < 0)
        return;
    const QString globalId = m_mesh->elements.at(m_selected).globalId;
    IfcInfo::Measured measured;
    measured.area = m_mesh->surfaceArea(m_selected);
    measured.volume = m_mesh->volume(m_selected);
    IfcInfo::Details details;
    if (m_info)
        details = m_info->describe(globalId, measured);

    QVariantList summary, sections;
    for (const IfcInfo::Row &row : details.summary)
        summary.append(QVariantMap{{"label", row.label}, {"value", row.value}});
    if (!details.found) {
        // Geometry with no matching entity in the file, or a file whose
        // properties couldn't be read: say what is known.
        summary.append(QVariantMap{{"label", tr("GlobalId")}, {"value", globalId}});
        if (measured.volume > 0)
            summary.append(QVariantMap{{"label", tr("Volume (measured)")},
                                       {"value", IfcInfo::formatNumber(measured.volume) + " m³"}});
    }
    for (const IfcInfo::Section &section : details.sections) {
        QVariantList rows;
        for (const IfcInfo::Row &row : section.rows)
            rows.append(QVariantMap{{"label", row.label}, {"value", row.value}});
        sections.append(QVariantMap{{"title", section.title}, {"rows", rows}});
    }
    m_selection = {
        {"ifcClass", details.found ? details.ifcClass : tr("Element")},
        {"name", details.name},
        {"summary", summary},
        {"sections", sections},
    };
    emit selectionChanged();
}
