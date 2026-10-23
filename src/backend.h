#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>
#include <QVector3D>
#include <QVector4D>
#include <memory>

#include "elementlist.h"
#include "loader.h"

class FilePicker;

// The open model and what the user has picked in it, exposed to QML as
// `backend`.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY stateChanged)
    // 0 to 1 while a model opens.
    Q_PROPERTY(double loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(QString schema READ schema NOTIFY meshChanged)
    Q_PROPERTY(int elementCount READ elementCount NOTIFY meshChanged)
    // One {color, opacity} per material, which the view draws a model for.
    Q_PROPERTY(QVariantList materials READ materials NOTIFY meshChanged)
    // The part of the model the view frames, without far-off outliers, and
    // the radius of everything, for the camera's clipping range.
    Q_PROPERTY(QVector3D modelCenter READ modelCenter NOTIFY meshChanged)
    Q_PROPERTY(float modelRadius READ modelRadius NOTIFY meshChanged)
    Q_PROPERTY(float sceneRadius READ sceneRadius NOTIFY meshChanged)
    Q_PROPERTY(int selectedElement READ selectedElement NOTIFY selectionChanged)
    // {ifcClass, name, summary: [{label, value}], sections: [{title, rows}]}
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVector3D selectionCenter READ selectionCenter NOTIFY selectionChanged)
    Q_PROPERTY(float selectionRadius READ selectionRadius NOTIFY selectionChanged)

    // The model tree.
    Q_PROPERTY(ElementList *elements READ elements CONSTANT)
    // The storeys, lowest first, and how many of them are shown: the storey
    // slider peels the top ones away; storeyLimit equal to the count shows all.
    Q_PROPERTY(QStringList storeys READ storeys NOTIFY meshChanged)
    Q_PROPERTY(int storeyLimit READ storeyLimit WRITE setStoreyLimit NOTIFY storeyLimitChanged)
    // X-ray: everything (All), or one discipline drawn with the rest ghosted.
    Q_PROPERTY(XRay xray READ xray WRITE setXray NOTIFY xrayChanged)
    // A section plane through the focus bounds: off, a horizontal plan cut
    // (keeping what's below), or a vertical cut along X or Z; position runs 0
    // to 1 across the model, and flipped keeps the other side.
    Q_PROPERTY(SectionAxis sectionAxis READ sectionAxis WRITE setSectionAxis NOTIFY sectionChanged)
    Q_PROPERTY(double sectionPosition READ sectionPosition WRITE setSectionPosition NOTIFY sectionChanged)
    Q_PROPERTY(bool sectionFlipped READ sectionFlipped WRITE setSectionFlipped NOTIFY sectionChanged)
    // (normal, w): the shaders keep points where dot(normal, p) <= w.
    Q_PROPERTY(QVector4D sectionPlane READ sectionPlane NOTIFY sectionChanged)
    Q_PROPERTY(QVector3D sectionCenter READ sectionCenter NOTIFY sectionChanged)
    Q_PROPERTY(float sectionSize READ sectionSize NOTIFY sectionChanged)
    // Bumped whenever which elements are drawn, ghosted, or hidden changes.
    Q_PROPERTY(int visibilityRevision READ visibilityRevision NOTIFY visibilityChanged)
    Q_PROPERTY(int shownCount READ shownCount NOTIFY visibilityChanged)

public:
    enum State { Empty, Loading, Ready, Failed };
    Q_ENUM(State)
    enum XRay { All, Structure, Services, Architecture };
    Q_ENUM(XRay)
    enum SectionAxis { NoSection, Plan, AlongX, AlongZ };
    Q_ENUM(SectionAxis)
    enum Visibility : quint8 { Hidden = 0, Shown = 1, Ghosted = 2 };

    explicit Backend(QObject *parent = nullptr);
    // Tests hand in a fake picker and their own cache folder.
    Backend(FilePicker *filePicker, const QString &cacheDirectory = {}, QObject *parent = nullptr);

    State state() const { return m_state; }
    QString fileName() const;
    QString filePath() const { return m_path; }
    QString errorMessage() const { return m_error; }
    QString warning() const { return m_warning; }
    double loadProgress() const { return m_loader.progress(); }
    QString schema() const;
    int elementCount() const { return m_mesh ? int(m_mesh->elements.size()) : 0; }
    QVariantList materials() const;
    QVector3D modelCenter() const { return m_focusCenter; }
    float modelRadius() const { return m_focusRadius; }
    float sceneRadius() const { return m_mesh ? (m_mesh->max - m_mesh->min).length() / 2 + m_mesh->center().length() : 0; }
    int selectedElement() const { return m_selected; }
    QVariantMap selection() const { return m_selection; }
    QVector3D selectionCenter() const;
    float selectionRadius() const;

    ElementList *elements() { return &m_elements; }
    QStringList storeys() const { return m_storeyNames; }
    int storeyLimit() const { return m_storeyLimit; }
    void setStoreyLimit(int limit);
    XRay xray() const { return m_xray; }
    void setXray(XRay xray);
    SectionAxis sectionAxis() const { return m_sectionAxis; }
    void setSectionAxis(SectionAxis axis);
    double sectionPosition() const { return m_sectionPosition; }
    void setSectionPosition(double position);
    bool sectionFlipped() const { return m_sectionFlipped; }
    void setSectionFlipped(bool flipped);
    QVector4D sectionPlane() const;
    QVector3D sectionCenter() const;
    float sectionSize() const { return m_focusRadius * 2.2f; }
    int visibilityRevision() const { return m_visibilityRevision; }
    int shownCount() const;

    const Mesh *mesh() const { return m_mesh.get(); }
    // Per element, Hidden, Shown, or Ghosted.
    const QVector<quint8> &visibility() const { return m_visibility; }
    // The storey index of each element (into storeys), -1 for none.
    int storeyOf(int element) const { return element >= 0 && element < m_storeyOfElement.size() ? m_storeyOfElement.at(element) : -1; }

    // Asks the portal for a model.
    Q_INVOKABLE void open();
    // Opens a local file; false with the reason in errorMessage otherwise.
    Q_INVOKABLE bool openUrl(const QUrl &url);
    Q_INVOKABLE bool openPath(const QString &path);
    // Selects the element under a ray through the scene, from the camera's
    // near point to its far point under the pointer; nothing when it misses.
    Q_INVOKABLE void selectAt(const QVector3D &near, const QVector3D &far);
    Q_INVOKABLE void select(int element);
    Q_INVOKABLE void clearSelection();
    // Where a ray meets the model, or undefined, so the view can zoom
    // towards what's under the pointer.
    Q_INVOKABLE QVariant hitPoint(const QVector3D &near, const QVector3D &far) const;
    // Formats the selection again, after a language switch.
    Q_INVOKABLE void refreshSelection();
    // One step through the X-ray modes and section axes, for their keys.
    Q_INVOKABLE void cycleXray();
    Q_INVOKABLE void cycleSection();

signals:
    void stateChanged();
    void meshChanged();
    void selectionChanged();
    void loadProgressChanged();
    // Something to tell the user that doesn't replace what's on screen: an
    // open that failed while a model is shown or another is opening, or a
    // warning about the model that just opened.
    void notice(const QString &message);
    void storeyLimitChanged();
    void xrayChanged();
    void sectionChanged();
    void visibilityChanged();

private:
    void wireFilePicker();
    void setState(State state);
    void fail(const QString &message);
    // An open refused before it started, for the file at path.
    void reject(const QString &path, const QString &message);
    // Element metadata, the tree, and storeys for a newly opened model.
    void describeElements();
    void updateVisibility();
    // Whether a click can land on the element: drawn, not ghosted.
    bool pickable(int element) const;

    FilePicker *m_filePicker = nullptr;
    Loader m_loader;
    State m_state = Empty;
    QString m_path;
    QString m_loadingPath;
    QString m_error;
    QString m_warning;
    std::shared_ptr<const Mesh> m_mesh;
    std::shared_ptr<const IfcInfo> m_info;
    QVector3D m_focusCenter;
    float m_focusRadius = 0;
    QVector3D m_focusMin, m_focusMax;
    int m_selected = -1;
    QVariantMap m_selection;

    ElementList m_elements;
    QStringList m_storeyNames;
    QVector<int> m_storeyOfElement;
    QVector<int> m_disciplines;
    int m_storeyLimit = 0;
    XRay m_xray = All;
    SectionAxis m_sectionAxis = NoSection;
    double m_sectionPosition = 0.5;
    bool m_sectionFlipped = false;
    QVector<quint8> m_visibility;
    int m_visibilityRevision = 0;
};
