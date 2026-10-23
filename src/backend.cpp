#include "backend.h"

#include <QFileInfo>

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
        QVector3D min, max;
        m_mesh->focusBounds(&min, &max);
        m_focusCenter = (min + max) / 2;
        m_focusRadius = (max - min).length() / 2;
        m_path = m_loadingPath;
        m_warning = result.warning;
        m_error.clear();
        emit meshChanged();
        setState(Ready);
    });
    connect(&m_loader, &Loader::failed, this, &Backend::fail);
    connect(&m_loader, &Loader::progressChanged, this, &Backend::loadProgressChanged);
}

void Backend::wireFilePicker() {
    connect(m_filePicker, &FilePicker::openSelected, this, &Backend::openUrl);
    connect(m_filePicker, &FilePicker::failed, this, &Backend::fail);
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
        m_loadingPath = url.toString();
        fail(tr("Keystone opens files on this computer only."));
        return false;
    }
    return openPath(url.toLocalFile());
}

bool Backend::openPath(const QString &path) {
    const QFileInfo info(path);
    m_loadingPath = info.absoluteFilePath();
    if (!info.exists()) {
        fail(tr("%1 doesn't exist.").arg(info.fileName()));
        return false;
    }
    if (!info.isFile() || !info.isReadable()) {
        fail(tr("%1 can't be read.").arg(info.fileName()));
        return false;
    }
    m_error.clear();
    setState(Loading);
    m_loader.start(m_loadingPath);
    return true;
}

void Backend::fail(const QString &message) {
    m_error = message;
    // A failed open leaves the model that was already open, if any, alone.
    setState(m_mesh ? Ready : Failed);
}

void Backend::setState(State state) {
    m_state = state;
    emit stateChanged();
}

void Backend::selectAt(const QVector3D &near, const QVector3D &far) {
    if (!m_mesh)
        return;
    const Mesh::Hit hit = m_mesh->pick(near, far - near);
    if (hit.element < 0)
        clearSelection();
    else
        select(hit.element);
}

QVariant Backend::hitPoint(const QVector3D &near, const QVector3D &far) const {
    if (!m_mesh)
        return {};
    const Mesh::Hit hit = m_mesh->pick(near, far - near);
    return hit.element < 0 ? QVariant() : QVariant(hit.point);
}

void Backend::select(int element) {
    if (!m_mesh || element < 0 || element >= m_mesh->elements.size()) {
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
