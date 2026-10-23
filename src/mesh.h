#pragma once

#include <QColor>
#include <QString>
#include <QVector3D>
#include <QVector>

// The model's triangles, as IfcConvert writes them to Wavefront OBJ with one
// group per element, named by its GlobalId. Positions are in metres in scene
// space: Y up (IFC is Z up) and centred on the model, so far-from-origin
// georeferenced coordinates keep their precision in floats.
struct Mesh {
    struct Material {
        QString name;
        QColor color = QColor(200, 200, 200);
        float opacity = 1;
    };
    // A run of triangles in one material, as a vertex range.
    struct Part {
        int material = 0;
        int first = 0;
        int count = 0;
    };
    struct Element {
        QString globalId;
        QVector<Part> parts;
        QVector3D min, max;
    };
    struct Hit {
        int element = -1;
        float distance = 0;
        QVector3D point;
    };

    QVector<Material> materials;
    QVector<Element> elements;
    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector3D min, max;
    // A bounding-volume hierarchy over every triangle, built once when the
    // mesh is read, so picking under the pointer stays quick on models with
    // millions of triangles. A node with a count is a leaf over that run of
    // triangleOrder; the others have two children.
    struct Node {
        QVector3D min, max;
        int left = -1, right = -1;
        int first = 0, count = 0;
    };
    QVector<Node> nodes;
    QVector<int> triangleOrder;   // first vertex of each triangle, in node order
    QVector<int> triangleElement; // element of the triangle at vertex / 3
    // Where the scene origin sits in the file's own coordinates (Z up).
    double originX = 0, originY = 0, originZ = 0;

    bool isEmpty() const { return elements.isEmpty(); }
    QVector3D center() const { return (min + max) / 2; }
    float radius() const { return (max - min).length() / 2; }
    int indexOf(const QString &globalId) const;
    // The bounds the view frames: every element except outliers far from the
    // rest, like a survey point or a site marker hundreds of metres away,
    // which would otherwise shrink the building to a dot.
    void focusBounds(QVector3D *min, QVector3D *max) const;

    void buildIndex();
    // The nearest element a ray through the scene meets, by its triangles.
    Hit pick(const QVector3D &origin, const QVector3D &direction) const;
    // Surface area and enclosed volume of an element's triangles, in m² and
    // m³; the volume is only meaningful for a closed solid.
    double surfaceArea(int element) const;
    double volume(int element) const;
};

// Reads an OBJ and its MTL; false with a message when the file can't be read.
bool readObj(const QString &path, Mesh *mesh, QString *error = nullptr);
