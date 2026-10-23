import QtQuick
import QtQuick3D
import Keystone

// The model in 3D: orbit with the left button, pan with the right or middle
// button (or Shift and the left), zoom with the wheel towards what's under
// the pointer, and click an element to select it.
Item {
    id: root

    // The backend under a name of its own, since MeshGeometry has a
    // `backend` property that would shadow the context property.
    readonly property var source: backend
    property real yaw: -35
    property real pitch: -25
    property real distance: 10
    property vector3d target: Qt.vector3d(0, 0, 0)
    readonly property real fieldOfView: 50
    readonly property real radius: Math.max(0.5, backend.modelRadius)
    readonly property real sceneRadius: Math.max(radius, backend.sceneRadius)

    function clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }

    // The distance at which a sphere of this radius fills the view.
    function fitDistance(r) {
        const half = fieldOfView / 2 * Math.PI / 180
        const aspect = Math.max(0.2, Math.min(1, width / Math.max(1, height)))
        return Math.max(0.5, r) / Math.sin(half) / aspect * 0.9
    }

    function frame(center, r) {
        frameAnimation.stop()
        targetAnimation.to = center
        distanceAnimation.to = fitDistance(r)
        frameAnimation.start()
    }

    function frameModel() { frame(backend.modelCenter, radius) }

    function frameSelection() {
        if (backend.selectedElement >= 0)
            frame(backend.selectionCenter, Math.max(0.3, backend.selectionRadius))
        else
            frameModel()
    }

    function setView(newYaw, newPitch) {
        yaw = newYaw
        pitch = newPitch
        frameModel()
    }

    function resetView() { setView(-35, -25) }

    function orbit(dYaw, dPitch) {
        yaw = (yaw + dYaw) % 360
        pitch = clamp(pitch + dPitch, -89.9, 89.9)
    }

    function zoom(factor, x, y) {
        frameAnimation.stop()
        if (x !== undefined) {
            // Towards the point under the pointer, which then stays put on
            // screen: the target moves as far towards it as the camera does.
            const r = ray(x, y)
            const hit = backend.hitPoint(r.near, r.far)
            if (hit !== undefined && hit !== null)
                target = target.plus(hit.minus(target).times(1 - factor))
        }
        distance = clamp(distance * factor, radius * 0.002 + 0.05, sceneRadius * 40)
    }

    // The camera's axes in the scene, from the same yaw-then-pitch the
    // nodes below apply. Computed here rather than asked of the camera, so
    // a click maps to the right ray even before a frame has been drawn.
    function rotate(v) {
        const p = pitch * Math.PI / 180, a = yaw * Math.PI / 180
        const y1 = v.y * Math.cos(p) - v.z * Math.sin(p)
        const z1 = v.y * Math.sin(p) + v.z * Math.cos(p)
        return Qt.vector3d(v.x * Math.cos(a) + z1 * Math.sin(a), y1, -v.x * Math.sin(a) + z1 * Math.cos(a))
    }

    function pan(dx, dy) {
        frameAnimation.stop()
        const metresPerPixel = 2 * distance * Math.tan(fieldOfView / 2 * Math.PI / 180) / Math.max(1, height)
        const right = rotate(Qt.vector3d(1, 0, 0)), up = rotate(Qt.vector3d(0, 1, 0))
        target = target.minus(right.times(dx * metresPerPixel)).plus(up.times(dy * metresPerPixel))
    }

    function ray(x, y) {
        const tanHalf = Math.tan(fieldOfView / 2 * Math.PI / 180)
        const u = (2 * x / Math.max(1, width) - 1) * tanHalf * width / Math.max(1, height)
        const v = (1 - 2 * y / Math.max(1, height)) * tanHalf
        const eye = target.plus(rotate(Qt.vector3d(0, 0, distance)))
        const direction = rotate(Qt.vector3d(u, v, -1))
        return { near: eye, far: eye.plus(direction) }
    }

    function selectAt(x, y) {
        const r = ray(x, y)
        backend.selectAt(r.near, r.far)
    }

    Connections {
        target: backend
        function onMeshChanged() {
            root.yaw = -35
            root.pitch = -25
            root.target = backend.modelCenter
            root.distance = root.fitDistance(root.radius)
        }
    }

    ParallelAnimation {
        id: frameAnimation
        PropertyAnimation { id: targetAnimation; target: root; property: "target"; duration: 260; easing.type: Easing.OutCubic }
        NumberAnimation { id: distanceAnimation; target: root; property: "distance"; duration: 260; easing.type: Easing.OutCubic }
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: camera

        environment: SceneEnvironment {
            clearColor: theme.background
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            position: root.target
            eulerRotation.y: root.yaw

            Node {
                eulerRotation.x: root.pitch

                PerspectiveCamera {
                    id: camera
                    z: root.distance
                    fieldOfView: root.fieldOfView
                    clipNear: Math.max(0.01, root.distance / 500)
                    clipFar: root.distance + root.target.length() + root.sceneRadius * 2 + 100

                    // A headlight, so the side being looked at is never dark.
                    DirectionalLight {
                        eulerRotation.x: -12
                        eulerRotation.y: 18
                        brightness: 0.75
                        ambientColor: Qt.tint(Qt.rgba(0.22, 0.22, 0.24, 1), Qt.alpha(theme.foreground, 0.12))
                    }
                }
            }
        }

        // The sky: high and to one side, tinted with the theme's foreground,
        // so walls facing different ways read apart.
        DirectionalLight {
            eulerRotation.x: -55
            eulerRotation.y: 35
            brightness: 0.55
            color: Qt.tint("white", Qt.alpha(theme.foreground, 0.25))
        }

        Repeater3D {
            model: backend.materials
            delegate: Model {
                required property var modelData
                required property int index
                geometry: MeshGeometry {
                    backend: root.source
                    material: index
                    hiddenElement: root.source.selectedElement
                }
                materials: PrincipledMaterial {
                    baseColor: modelData.color
                    opacity: modelData.opacity
                    alphaMode: modelData.opacity < 0.999 ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
                    cullMode: Material.NoCulling
                    roughness: 0.85
                    metalness: 0
                    specularAmount: 0.2
                }
            }
        }

        Model {
            visible: root.source.selectedElement >= 0
            geometry: MeshGeometry {
                backend: root.source
                element: root.source.selectedElement
            }
            materials: PrincipledMaterial {
                baseColor: theme.accent
                emissiveFactor: Qt.vector3d(0.25, 0.25, 0.25)
                cullMode: Material.NoCulling
                roughness: 0.6
                metalness: 0
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        property point last
        property bool dragged: false
        property bool panning: false

        onPressed: (event) => {
            last = Qt.point(event.x, event.y)
            dragged = false
            panning = event.button !== Qt.LeftButton || (event.modifiers & Qt.ShiftModifier)
        }
        onPositionChanged: (event) => {
            const dx = event.x - last.x, dy = event.y - last.y
            if (!dragged && Math.abs(dx) + Math.abs(dy) < 4)
                return
            dragged = true
            last = Qt.point(event.x, event.y)
            if (panning)
                root.pan(dx, dy)
            else
                root.orbit(-dx * 0.4, -dy * 0.4)
        }
        onReleased: (event) => {
            if (!dragged && event.button === Qt.LeftButton)
                root.selectAt(event.x, event.y)
        }
        onDoubleClicked: (event) => {
            if (event.button === Qt.LeftButton) {
                root.selectAt(event.x, event.y)
                root.frameSelection()
            }
        }
        onWheel: (wheel) => {
            const steps = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y / 120 : wheel.pixelDelta.y / 40
            if (steps !== 0)
                root.zoom(Math.pow(0.85, steps), wheel.x, wheel.y)
        }
    }

    // Touchpad pinches arrive as native zoom gestures.
    PinchHandler {
        target: null
        property real previous: 1
        onActiveChanged: previous = 1
        onActiveScaleChanged: {
            const factor = previous / Math.max(0.01, activeScale)
            previous = activeScale
            root.zoom(factor, centroid.position.x, centroid.position.y)
        }
    }
}
