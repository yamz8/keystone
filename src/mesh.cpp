#include "mesh.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QVarLengthArray>
#include <algorithm>
#include <cmath>
#include <limits>

int Mesh::indexOf(const QString &globalId) const {
    for (int i = 0; i < elements.size(); ++i)
        if (elements.at(i).globalId == globalId)
            return i;
    return -1;
}

void Mesh::focusBounds(QVector3D *outMin, QVector3D *outMax) const {
    *outMin = min;
    *outMax = max;
    if (elements.size() < 3)
        return;
    // Distances of element centres from the median centre; anything beyond
    // several times the typical distance is an outlier.
    auto median = [](QVector<float> values) {
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        return values.at(values.size() / 2);
    };
    QVector<float> xs, ys, zs;
    for (const Element &e : elements) {
        const QVector3D c = (e.min + e.max) / 2;
        xs.append(c.x());
        ys.append(c.y());
        zs.append(c.z());
    }
    const QVector3D middle(median(xs), median(ys), median(zs));
    QVector<float> distances;
    for (const Element &e : elements)
        distances.append(((e.min + e.max) / 2 - middle).length());
    // The typical element size keeps a compact model from rejecting its
    // own outer walls.
    QVector<float> sizes;
    for (const Element &e : elements)
        sizes.append((e.max - e.min).length());
    const float limit = std::max(6 * median(distances), 3 * median(sizes)) + 1;

    QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D hi = -lo;
    int kept = 0;
    for (const Element &e : elements) {
        if (((e.min + e.max) / 2 - middle).length() > limit)
            continue;
        ++kept;
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::min(lo[axis], e.min[axis]);
            hi[axis] = std::max(hi[axis], e.max[axis]);
        }
    }
    if (kept > 0) {
        *outMin = lo;
        *outMax = hi;
    }
}

namespace {

bool rayHitsBox(const QVector3D &origin, const QVector3D &inverse, const QVector3D &min, const QVector3D &max,
                float limit) {
    float near = 0, far = limit;
    for (int axis = 0; axis < 3; ++axis) {
        float t1 = (min[axis] - origin[axis]) * inverse[axis];
        float t2 = (max[axis] - origin[axis]) * inverse[axis];
        if (t1 > t2)
            std::swap(t1, t2);
        near = std::max(near, t1);
        far = std::min(far, t2);
        if (near > far)
            return false;
    }
    return true;
}

// Möller–Trumbore, both faces: IFC solids are not reliably wound.
bool rayHitsTriangle(const QVector3D &origin, const QVector3D &direction, const QVector3D &a, const QVector3D &b,
                     const QVector3D &c, float *distance) {
    const QVector3D edge1 = b - a, edge2 = c - a;
    const QVector3D p = QVector3D::crossProduct(direction, edge2);
    const float determinant = QVector3D::dotProduct(edge1, p);
    if (std::abs(determinant) < 1e-12f)
        return false;
    const float inverse = 1.0f / determinant;
    const QVector3D s = origin - a;
    const float u = QVector3D::dotProduct(s, p) * inverse;
    if (u < 0 || u > 1)
        return false;
    const QVector3D q = QVector3D::crossProduct(s, edge1);
    const float v = QVector3D::dotProduct(direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return false;
    const float t = QVector3D::dotProduct(edge2, q) * inverse;
    if (t <= 0)
        return false;
    *distance = t;
    return true;
}

}

void Mesh::buildIndex() {
    nodes.clear();
    triangleOrder.clear();
    triangleElement.fill(-1, positions.size() / 3);
    for (int e = 0; e < elements.size(); ++e)
        for (const Part &part : elements.at(e).parts)
            for (int i = part.first; i + 2 < part.first + part.count; i += 3) {
                triangleElement[i / 3] = e;
                triangleOrder.append(i);
            }
    if (triangleOrder.isEmpty())
        return;

    QVector<QVector3D> centroids(positions.size() / 3);
    for (const int i : std::as_const(triangleOrder))
        centroids[i / 3] = (positions.at(i) + positions.at(i + 1) + positions.at(i + 2)) / 3;

    // Split each node's triangles at the median of its longest axis, until
    // a handful are left.
    constexpr int leafSize = 8;
    nodes.reserve(2 * triangleOrder.size() / leafSize + 1);
    nodes.append(Node{{}, {}, -1, -1, 0, int(triangleOrder.size())});
    QVector<int> pending{0};
    while (!pending.isEmpty()) {
        const int index = pending.takeLast();
        const int first = nodes.at(index).first, count = nodes.at(index).count;
        QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D hi = -lo, clo = lo, chi = hi;
        for (int t = first; t < first + count; ++t) {
            const int i = triangleOrder.at(t);
            for (int k = 0; k < 3; ++k)
                for (int axis = 0; axis < 3; ++axis) {
                    lo[axis] = std::min(lo[axis], positions.at(i + k)[axis]);
                    hi[axis] = std::max(hi[axis], positions.at(i + k)[axis]);
                }
            for (int axis = 0; axis < 3; ++axis) {
                clo[axis] = std::min(clo[axis], centroids.at(i / 3)[axis]);
                chi[axis] = std::max(chi[axis], centroids.at(i / 3)[axis]);
            }
        }
        nodes[index].min = lo;
        nodes[index].max = hi;
        if (count <= leafSize)
            continue;
        const QVector3D extent = chi - clo;
        const int axis = extent.x() >= extent.y() && extent.x() >= extent.z() ? 0 : extent.y() >= extent.z() ? 1 : 2;
        const int middle = first + count / 2;
        std::nth_element(triangleOrder.begin() + first, triangleOrder.begin() + middle,
                         triangleOrder.begin() + first + count, [&centroids, axis](int a, int b) {
                             return centroids.at(a / 3)[axis] < centroids.at(b / 3)[axis];
                         });
        const int left = int(nodes.size());
        nodes.append(Node{{}, {}, -1, -1, first, middle - first});
        nodes.append(Node{{}, {}, -1, -1, middle, first + count - middle});
        nodes[index].left = left;
        nodes[index].right = left + 1;
        nodes[index].count = 0;
        pending.append(left);
        pending.append(left + 1);
    }
}

Mesh::Hit Mesh::pick(const QVector3D &origin, const QVector3D &rayDirection) const {
    Hit hit;
    const QVector3D direction = rayDirection.normalized();
    if (direction.isNull() || nodes.isEmpty())
        return hit;
    auto invert = [](float v) { return v == 0 ? std::numeric_limits<float>::infinity() : 1.0f / v; };
    const QVector3D inverse(invert(direction.x()), invert(direction.y()), invert(direction.z()));
    float best = std::numeric_limits<float>::max();
    int bestTriangle = -1;
    // Only nodes the ray enters before the nearest hit so far are opened.
    QVarLengthArray<int, 64> stack{0};
    while (!stack.isEmpty()) {
        const Node &node = nodes.at(stack.last());
        stack.removeLast();
        if (!rayHitsBox(origin, inverse, node.min, node.max, best))
            continue;
        if (node.count == 0) {
            stack.append(node.left);
            stack.append(node.right);
            continue;
        }
        for (int t = node.first; t < node.first + node.count; ++t) {
            const int i = triangleOrder.at(t);
            float distance = 0;
            if (rayHitsTriangle(origin, direction, positions.at(i), positions.at(i + 1), positions.at(i + 2), &distance)
                && distance < best) {
                best = distance;
                bestTriangle = i;
            }
        }
    }
    if (bestTriangle >= 0) {
        hit.element = triangleElement.at(bestTriangle / 3);
        hit.distance = best;
        hit.point = origin + direction * best;
    }
    return hit;
}

double Mesh::surfaceArea(int element) const {
    if (element < 0 || element >= elements.size())
        return -1;
    double area = 0;
    for (const Part &part : elements.at(element).parts)
        for (int i = part.first; i + 2 < part.first + part.count; i += 3)
            area += QVector3D::crossProduct(positions.at(i + 1) - positions.at(i),
                                            positions.at(i + 2) - positions.at(i)).length() / 2.0;
    return area;
}

double Mesh::volume(int element) const {
    if (element < 0 || element >= elements.size())
        return -1;
    // The divergence theorem over the closed surface, about the element's own
    // centre to keep the sum's terms small.
    const QVector3D c = (elements.at(element).min + elements.at(element).max) / 2;
    double volume = 0;
    for (const Part &part : elements.at(element).parts)
        for (int i = part.first; i + 2 < part.first + part.count; i += 3)
            volume += QVector3D::dotProduct(positions.at(i) - c,
                                            QVector3D::crossProduct(positions.at(i + 1) - c, positions.at(i + 2) - c)) / 6.0;
    return std::abs(volume);
}

namespace {

// The next whitespace-separated token of a line, as a view.
bool nextToken(const char *&p, const char *end, const char **start, const char **stop) {
    while (p < end && (*p == ' ' || *p == '\t'))
        ++p;
    if (p >= end)
        return false;
    *start = p;
    while (p < end && *p != ' ' && *p != '\t')
        ++p;
    *stop = p;
    return true;
}

double toDouble(const char *start, const char *stop) {
    return QByteArray::fromRawData(start, int(stop - start)).toDouble();
}

void readMtl(const QString &path, QVector<Mesh::Material> *materials, QHash<QString, int> *byName) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    int current = -1;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        const QList<QByteArray> words = line.simplified().split(' ');
        if (words.isEmpty())
            continue;
        if (words.first() == "newmtl" && words.size() > 1) {
            const QString name = QString::fromUtf8(line.mid(7).trimmed());
            current = byName->value(name, -1);
            if (current < 0) {
                current = materials->size();
                materials->append({name, QColor(200, 200, 200), 1});
                byName->insert(name, current);
            }
        } else if (current >= 0 && words.first() == "Kd" && words.size() >= 4) {
            (*materials)[current].color = QColor::fromRgbF(float(words.at(1).toDouble()), float(words.at(2).toDouble()),
                                                          float(words.at(3).toDouble()));
        } else if (current >= 0 && words.first() == "d" && words.size() >= 2) {
            (*materials)[current].opacity = float(words.at(1).toDouble());
        } else if (current >= 0 && words.first() == "Tr" && words.size() >= 2) {
            (*materials)[current].opacity = 1.0f - float(words.at(1).toDouble());
        }
    }
}

}

bool readObj(const QString &path, Mesh *mesh, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    *mesh = Mesh();

    QHash<QString, int> materialByName;
    QHash<QString, int> elementByGlobalId;
    QVector<double> vertices; // x, y, z in the file's Z-up space
    QVector<float> vertexNormals;
    // Faces as indices first, because the model's centre is only known once
    // every vertex has been read.
    struct Corner { int vertex; int normal; };
    QVector<Corner> corners;
    struct Run { int element; int material; int firstCorner; };
    QVector<Run> runs;

    int element = -1, material = -1;
    auto startRun = [&] {
        if (element < 0)
            return;
        if (!runs.isEmpty() && runs.last().firstCorner == corners.size())
            runs.last() = {element, qMax(0, material), int(corners.size())};
        else
            runs.append({element, qMax(0, material), int(corners.size())});
    };

    const char *p = data.constData(), *end = p + data.size();
    while (p < end) {
        const char *lineEnd = static_cast<const char *>(memchr(p, '\n', size_t(end - p)));
        if (!lineEnd)
            lineEnd = end;
        const char *q = p, *a = nullptr, *b = nullptr;
        if (nextToken(q, lineEnd, &a, &b)) {
            const QByteArray keyword = QByteArray::fromRawData(a, int(b - a));
            if (keyword == "v") {
                for (int i = 0; i < 3; ++i)
                    vertices.append(nextToken(q, lineEnd, &a, &b) ? toDouble(a, b) : 0.0);
            } else if (keyword == "vn") {
                for (int i = 0; i < 3; ++i)
                    vertexNormals.append(nextToken(q, lineEnd, &a, &b) ? float(toDouble(a, b)) : 0.0f);
            } else if (keyword == "f") {
                // A polygon fans out into triangles; IfcConvert writes
                // triangles already.
                QVector<Corner> polygon;
                while (nextToken(q, lineEnd, &a, &b)) {
                    const QByteArray token = QByteArray(a, int(b - a)).trimmed();
                    const QList<QByteArray> parts = token.split('/');
                    auto resolve = [](int index, int count) { return index < 0 ? count + index : index - 1; };
                    const int v = resolve(parts.value(0).toInt(), int(vertices.size() / 3));
                    const int n = parts.size() > 2 && !parts.at(2).isEmpty()
                        ? resolve(parts.at(2).toInt(), int(vertexNormals.size() / 3)) : -1;
                    polygon.append({v, n});
                }
                if (element < 0) {
                    // Faces before any group belong to one unnamed element.
                    element = int(mesh->elements.size());
                    mesh->elements.append(Mesh::Element());
                    startRun();
                }
                for (int i = 1; i + 1 < polygon.size(); ++i) {
                    corners.append(polygon.at(0));
                    corners.append(polygon.at(i));
                    corners.append(polygon.at(i + 1));
                }
            } else if (keyword == "g" || keyword == "o") {
                const QString name = QString::fromUtf8(QByteArray(b, int(lineEnd - b)).trimmed());
                element = elementByGlobalId.value(name, -1);
                if (element < 0) {
                    element = int(mesh->elements.size());
                    mesh->elements.append({name, {}, {}, {}});
                    elementByGlobalId.insert(name, element);
                }
                startRun();
            } else if (keyword == "usemtl") {
                const QString name = QString::fromUtf8(QByteArray(b, int(lineEnd - b)).trimmed());
                material = materialByName.value(name, -1);
                if (material < 0) {
                    material = int(mesh->materials.size());
                    mesh->materials.append({name, QColor(200, 200, 200), 1});
                    materialByName.insert(name, material);
                }
                startRun();
            } else if (keyword == "mtllib") {
                const QString name = QString::fromUtf8(QByteArray(b, int(lineEnd - b)).trimmed());
                readMtl(QFileInfo(path).dir().filePath(name), &mesh->materials, &materialByName);
            }
        }
        p = lineEnd + 1;
    }
    if (mesh->materials.isEmpty())
        mesh->materials.append({QStringLiteral("default"), QColor(200, 200, 200), 1});

    const int vertexCount = int(vertices.size() / 3);
    double lo[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    double hi[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
    for (const Corner &corner : corners) {
        if (corner.vertex < 0 || corner.vertex >= vertexCount)
            continue;
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::min(lo[axis], vertices.at(corner.vertex * 3 + axis));
            hi[axis] = std::max(hi[axis], vertices.at(corner.vertex * 3 + axis));
        }
    }
    if (corners.isEmpty() || lo[0] > hi[0]) {
        mesh->elements.clear();
        return true;
    }
    mesh->originX = (lo[0] + hi[0]) / 2;
    mesh->originY = (lo[1] + hi[1]) / 2;
    mesh->originZ = (lo[2] + hi[2]) / 2;

    // IFC's (x, y, z) with Z up becomes (x, z, -y) with Y up, which keeps the
    // handedness and the plan view the same way round.
    auto toScene = [mesh](double x, double y, double z) {
        return QVector3D(float(x - mesh->originX), float(z - mesh->originZ), float(-(y - mesh->originY)));
    };

    // Sort runs by element, then material, so each element's triangles are
    // one contiguous block of parts.
    QVector<int> order(runs.size());
    for (int i = 0; i < runs.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&runs](int x, int y) {
        return runs.at(x).element != runs.at(y).element ? runs.at(x).element < runs.at(y).element
                                                        : runs.at(x).material < runs.at(y).material;
    });

    mesh->positions.reserve(corners.size());
    mesh->normals.reserve(corners.size());
    for (const int r : order) {
        const Run &run = runs.at(r);
        const int stop = r + 1 < runs.size() ? runs.at(r + 1).firstCorner : int(corners.size());
        if (stop <= run.firstCorner)
            continue;
        Mesh::Element &target = mesh->elements[run.element];
        const int first = int(mesh->positions.size());
        for (int c = run.firstCorner; c + 2 < stop; c += 3) {
            QVector3D triangle[3];
            bool valid = true;
            for (int k = 0; k < 3; ++k) {
                const Corner &corner = corners.at(c + k);
                valid = valid && corner.vertex >= 0 && corner.vertex < vertexCount;
                if (valid)
                    triangle[k] = toScene(vertices.at(corner.vertex * 3), vertices.at(corner.vertex * 3 + 1),
                                          vertices.at(corner.vertex * 3 + 2));
            }
            if (!valid)
                continue;
            const QVector3D flat = QVector3D::normal(triangle[0], triangle[1], triangle[2]);
            for (int k = 0; k < 3; ++k) {
                const Corner &corner = corners.at(c + k);
                QVector3D normal = flat;
                if (corner.normal >= 0 && corner.normal * 3 + 2 < vertexNormals.size()) {
                    const float *n = vertexNormals.constData() + corner.normal * 3;
                    normal = QVector3D(n[0], n[2], -n[1]);
                }
                mesh->positions.append(triangle[k]);
                mesh->normals.append(normal);
            }
        }
        const int count = int(mesh->positions.size()) - first;
        if (count == 0)
            continue;
        if (!target.parts.isEmpty() && target.parts.last().material == run.material
            && target.parts.last().first + target.parts.last().count == first)
            target.parts.last().count += count;
        else
            target.parts.append({run.material, first, count});
    }

    // Elements without triangles (a group with no faces) are dropped.
    QVector<Mesh::Element> kept;
    for (Mesh::Element &e : mesh->elements) {
        if (e.parts.isEmpty())
            continue;
        QVector3D emin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D emax = -emin;
        for (const Mesh::Part &part : e.parts)
            for (int i = part.first; i < part.first + part.count; ++i) {
                const QVector3D &v = mesh->positions.at(i);
                for (int axis = 0; axis < 3; ++axis) {
                    emin[axis] = std::min(emin[axis], v[axis]);
                    emax[axis] = std::max(emax[axis], v[axis]);
                }
            }
        e.min = emin;
        e.max = emax;
        kept.append(e);
    }
    mesh->elements = kept;
    mesh->buildIndex();
    if (!mesh->elements.isEmpty()) {
        mesh->min = mesh->elements.first().min;
        mesh->max = mesh->elements.first().max;
        for (const Mesh::Element &e : mesh->elements)
            for (int axis = 0; axis < 3; ++axis) {
                mesh->min[axis] = std::min(mesh->min[axis], e.min[axis]);
                mesh->max[axis] = std::max(mesh->max[axis], e.max[axis]);
            }
    }
    return true;
}
