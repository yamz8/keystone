#pragma once

#include <QPointer>
#include <QtQuick3D/QQuick3DGeometry>

#include "backend.h"

// The open model's triangles for one Qt Quick 3D Model. Drawing the model one
// material at a time keeps it to a few draw calls however many elements it
// has; the selection outline draws a single element on its own.
class MeshGeometry : public QQuick3DGeometry {
    Q_OBJECT
    Q_PROPERTY(Backend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    // The material whose triangles to draw.
    Q_PROPERTY(int material READ material WRITE setMaterial NOTIFY materialChanged)
    // Only this element's triangles, in every material, when it's 0 or
    // more; with neither an element nor a material nothing is drawn.
    Q_PROPERTY(int element READ element WRITE setElement NOTIFY elementChanged)
    // Leaves this element out, so its highlighted copy draws without
    // fighting the original for the same depth.
    Q_PROPERTY(int hiddenElement READ hiddenElement WRITE setHiddenElement NOTIFY hiddenElementChanged)

public:
    explicit MeshGeometry(QQuick3DObject *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *backend);
    int material() const { return m_material; }
    void setMaterial(int material);
    int element() const { return m_element; }
    void setElement(int element);
    int hiddenElement() const { return m_hiddenElement; }
    void setHiddenElement(int element);

    int vertexCount() const { return m_vertexCount; }

signals:
    void backendChanged();
    void materialChanged();
    void elementChanged();
    void hiddenElementChanged();

private:
    void rebuild();
    bool drawsElement(int element) const;

    QPointer<Backend> m_backend;
    QMetaObject::Connection m_meshConnection;
    int m_material = -1;
    int m_element = -1;
    int m_hiddenElement = -1;
    int m_vertexCount = 0;
};
