import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Keystone

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: backend.state === Backend.Ready && backend.fileName ? qsTr("%1 — Keystone").arg(backend.fileName) : "Keystone"
    // The theme's own background when it's a dark theme; the chrome is dark
    // Material either way, so a light theme keeps a dark window around the
    // light viewport.
    color: theme.light ? "#0e0e10" : theme.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool panelVisible: true
    readonly property bool hasModel: backend.elementCount > 0
    readonly property bool busy: backend.state === Backend.Loading

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: backend.open() }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasModel
        onActivated: modelView.frameSelection()
    }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.resetView() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.setView(0, -8) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.setView(90, -8) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.setView(180, -8) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.setView(0, -89.9) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.orbit(-15, 0) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.orbit(15, 0) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.orbit(0, -10) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.orbit(0, 10) }
    Shortcut { sequences: ["+", "=", StandardKey.ZoomIn]; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.zoom(0.8) }
    Shortcut { sequences: ["-", StandardKey.ZoomOut]; context: Qt.ApplicationShortcut; enabled: win.hasModel; onActivated: modelView.zoom(1.25) }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; onActivated: win.panelVisible = !win.panelVisible }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false
            else
                backend.clearSelection()
        }
    }

    // A message about the open model that didn't stop it opening (a failed
    // second open, unreadable properties), shown for a while.
    property string notice: ""
    Connections {
        target: backend
        function onStateChanged() {
            const message = backend.errorMessage || backend.warning
            if (backend.state === Backend.Ready && message) {
                win.notice = message
                noticeTimer.restart()
            }
        }
    }
    Timer { id: noticeTimer; interval: 8000; onTriggered: win.notice = "" }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ModelView {
            id: modelView
            anchors.fill: parent
            visible: win.hasModel
            // The 3D view itself is never mirrored: left still orbits left.
            LayoutMirroring.enabled: false
        }

        // Nothing open yet, opening, or a failed first open.
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 64, 520)
            visible: !win.hasModel
            spacing: 14

            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/keystone.svg"
                sourceSize: Qt.size(72, 72)
                opacity: win.busy ? 0.5 : 0.9
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: win.busy ? qsTr("Opening %1…").arg(backend.fileName)
                    : backend.state === Backend.Failed ? qsTr("That model didn't open")
                    : qsTr("Open an IFC model")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: win.busy ? qsTr("IfcOpenShell is turning the building into triangles.")
                    : backend.state === Backend.Failed ? backend.errorMessage
                    : qsTr("Drop an .ifc file here, or press Ctrl+O.")
                color: backend.state === Backend.Failed ? Material.color(Material.Red, Material.Shade200) : Qt.rgba(1, 1, 1, 0.6)
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            ProgressBar {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 280
                visible: win.busy
                from: 0
                to: 1
                value: backend.loadProgress
                indeterminate: backend.loadProgress <= 0
            }
            Button {
                Layout.alignment: Qt.AlignHCenter
                visible: !win.busy
                text: backend.state === Backend.Failed ? qsTr("Open another model…") : qsTr("Open model…")
                highlighted: true
                onClicked: backend.open()
            }
        }

        // Opening a second model keeps the first on screen until it's ready.
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 16
            visible: win.hasModel && win.busy
            width: loadingRow.implicitWidth + 28
            height: 40
            radius: theme.radius
            color: Qt.rgba(0.07, 0.07, 0.08, 0.92)
            RowLayout {
                id: loadingRow
                anchors.centerIn: parent
                spacing: 10
                Label { text: qsTr("Opening %1…").arg(backend.fileName) }
                ProgressBar {
                    Layout.preferredWidth: 120
                    from: 0
                    to: 1
                    value: backend.loadProgress
                    indeterminate: backend.loadProgress <= 0
                }
            }
        }

        PropertiesPanel {
            anchors.top: parent.top
            anchors.bottom: statusBar.top
            anchors.right: parent.right
            anchors.margins: 12
            width: Math.min(380, parent.width * 0.42)
            visible: win.hasModel && win.panelVisible && backend.selectedElement >= 0
            selection: backend.selection
            onCloseRequested: backend.clearSelection()
        }

        Rectangle {
            id: statusBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 32
            visible: win.hasModel
            color: Qt.rgba(0.05, 0.05, 0.06, 0.85)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 16
                Label {
                    text: backend.fileName
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 360
                }
                Label {
                    text: backend.schema
                    visible: backend.schema !== ""
                    font.pixelSize: 12
                    color: Qt.rgba(1, 1, 1, 0.55)
                }
                Label {
                    text: qsTr("%n elements", "", backend.elementCount)
                    font.pixelSize: 12
                    color: Qt.rgba(1, 1, 1, 0.55)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: backend.selectedElement < 0 ? qsTr("Click an element to see its properties") : ""
                    font.pixelSize: 12
                    color: Qt.rgba(1, 1, 1, 0.55)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: qsTr("? for shortcuts")
                    font.pixelSize: 12
                    color: Qt.rgba(1, 1, 1, 0.4)
                }
            }
        }

        Rectangle {
            anchors.bottom: statusBar.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 16
            visible: win.notice !== ""
            width: Math.min(parent.width - 48, noticeText.implicitWidth + 32)
            height: noticeText.implicitHeight + 20
            radius: theme.radius
            color: Qt.rgba(0.07, 0.07, 0.08, 0.95)
            border.color: Material.color(Material.Red, Material.Shade300)
            Label {
                id: noticeText
                anchors.fill: parent
                anchors.margins: 10
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                text: win.notice
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea { anchors.fill: parent; onClicked: win.notice = "" }
        }

        ShortcutsOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }

        DropArea {
            id: dropArea
            anchors.fill: parent
            enabled: !win.helpVisible
            // Only local files: "file:////host/..." is a network share.
            onEntered: (drag) => {
                drag.accepted = drag.hasUrls && drag.urls.length > 0
                    && /^file:\/\/\/(?!\/)/.test(drag.urls[0].toString())
            }
            onDropped: (drop) => {
                if (backend.openUrl(drop.urls[0]))
                    drop.accept(Qt.CopyAction)
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 8
                visible: dropArea.containsDrag
                color: "#cc0e0e10"
                border.color: theme.accent
                border.width: 2
                radius: theme.radius
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Drop to open the model")
                    font.pixelSize: 20
                }
            }
        }
    }
}
