#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>
#include <QVector3D>
#include <memory>

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

public:
    enum State { Empty, Loading, Ready, Failed };
    Q_ENUM(State)

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

    const Mesh *mesh() const { return m_mesh.get(); }

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

signals:
    void stateChanged();
    void meshChanged();
    void selectionChanged();
    void loadProgressChanged();

private:
    void wireFilePicker();
    void setState(State state);
    void fail(const QString &message);

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
    int m_selected = -1;
    QVariantMap m_selection;
};
