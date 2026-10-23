#include "meshgeometry.h"

#include "backend.h"

MeshGeometry::MeshGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent) {}

void MeshGeometry::setBackend(Backend *backend) {
    if (m_backend == backend)
        return;
    if (m_meshConnection)
        disconnect(m_meshConnection);
    m_backend = backend;
    if (m_backend)
        m_meshConnection = connect(m_backend, &Backend::meshChanged, this, &MeshGeometry::rebuild);
    emit backendChanged();
    rebuild();
}

void MeshGeometry::setMaterial(int material) {
    if (m_material == material)
        return;
    m_material = material;
    emit materialChanged();
    rebuild();
}

void MeshGeometry::setElement(int element) {
    if (m_element == element)
        return;
    m_element = element;
    emit elementChanged();
    rebuild();
}

void MeshGeometry::setHiddenElement(int element) {
    if (m_hiddenElement == element)
        return;
    // Only the materials the old or new element uses change; every other
    // batch keeps its buffer, so selecting stays quick on large models.
    const bool affected = drawsElement(m_hiddenElement) || drawsElement(element);
    m_hiddenElement = element;
    emit hiddenElementChanged();
    if (affected)
        rebuild();
}

bool MeshGeometry::drawsElement(int element) const {
    const Mesh *mesh = m_backend ? m_backend->mesh() : nullptr;
    if (!mesh || element < 0 || element >= mesh->elements.size() || m_element >= 0 || m_material < 0)
        return false;
    for (const Mesh::Part &part : mesh->elements.at(element).parts)
        if (part.material == m_material)
            return true;
    return false;
}

void MeshGeometry::rebuild() {
    clear();
    m_vertexCount = 0;
    const Mesh *mesh = m_backend ? m_backend->mesh() : nullptr;

    // Interleaved position and normal, three floats each.
    QByteArray data;
    QVector3D min(1e30f, 1e30f, 1e30f), max(-1e30f, -1e30f, -1e30f);
    auto addPart = [&](const Mesh::Part &part) {
        const qsizetype offset = data.size();
        data.resize(offset + qsizetype(part.count) * 6 * sizeof(float));
        float *out = reinterpret_cast<float *>(data.data() + offset);
        for (int i = part.first; i < part.first + part.count; ++i) {
            const QVector3D &p = mesh->positions.at(i), &n = mesh->normals.at(i);
            *out++ = p.x(); *out++ = p.y(); *out++ = p.z();
            *out++ = n.x(); *out++ = n.y(); *out++ = n.z();
            for (int axis = 0; axis < 3; ++axis) {
                min[axis] = std::min(min[axis], p[axis]);
                max[axis] = std::max(max[axis], p[axis]);
            }
        }
        m_vertexCount += part.count;
    };
    if (mesh) {
        if (m_element >= 0 && m_element < mesh->elements.size()) {
            for (const Mesh::Part &part : mesh->elements.at(m_element).parts)
                addPart(part);
        } else if (m_element < 0 && m_material >= 0) {
            for (int e = 0; e < mesh->elements.size(); ++e)
                for (const Mesh::Part &part : mesh->elements.at(e).parts)
                    if (e != m_hiddenElement && part.material == m_material)
                        addPart(part);
        }
    }

    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setStride(6 * sizeof(float));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
    setVertexData(data);
    if (m_vertexCount > 0)
        setBounds(min, max);
    update();
}
