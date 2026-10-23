import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    signal closeRequested()

    Material.theme: Material.Dark
    Material.roundedScale: theme.radius
    color: Qt.rgba(0, 0, 0, 0.6)

    readonly property var shortcuts: [
        ["Ctrl+O", qsTr("Open a model")],
        [qsTr("Click"), qsTr("Select an element")],
        [qsTr("Drag"), qsTr("Orbit")],
        [qsTr("Right drag"), qsTr("Pan (or Shift+drag)")],
        [qsTr("Wheel"), qsTr("Zoom towards the pointer")],
        [qsTr("Double-click"), qsTr("Select and frame an element")],
        ["Space", qsTr("Frame the selection, or the whole model")],
        ["Home", qsTr("Reset the view")],
        ["1  2  3  4", qsTr("Front, side, back, and top views")],
        ["← → ↑ ↓", qsTr("Orbit")],
        ["+  −", qsTr("Zoom in and out")],
        ["I", qsTr("Show or hide properties")],
        ["T", qsTr("Show or hide the element list")],
        ["Ctrl+F", qsTr("Search elements")],
        ["PgUp  PgDn", qsTr("Show more or fewer storeys")],
        ["X", qsTr("X-ray: all, structure, MEP, architecture")],
        ["S", qsTr("Section: off, plan, X, Z")],
        ["[  ]", qsTr("Move the section plane")],
        [qsTr("Alt+drag"), qsTr("Drag the section plane")],
        ["F", qsTr("Keep the other side of the section")],
        ["Esc", qsTr("Clear the selection")],
        ["?", qsTr("Show or hide shortcuts")],
        ["Q", qsTr("Quit")],
    ]

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: overlay.closeRequested()
        onWheel: {}
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, column.implicitWidth + 56)
        height: Math.min(parent.height - 48, column.implicitHeight + 48)
        color: "#16161a"
        radius: theme.radius
        border.color: Qt.rgba(1, 1, 1, 0.08)

        ColumnLayout {
            id: column
            anchors.centerIn: parent
            spacing: 14

            Label {
                text: qsTr("Shortcuts")
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            // Two pairs of key and action a row on a wide window, so the list
            // fits a laptop screen's height.
            GridLayout {
                columns: overlay.width > 1000 ? 4 : 2
                columnSpacing: 28
                rowSpacing: 8

                Repeater {
                    model: overlay.shortcuts.length * 2
                    delegate: Label {
                        required property int index
                        readonly property var entry: overlay.shortcuts[Math.floor(index / 2)]
                        text: index % 2 === 0 ? entry[0] : entry[1]
                        color: index % 2 === 0 ? theme.accent : Qt.rgba(1, 1, 1, 0.85)
                        font.family: index % 2 === 0 ? "monospace" : Qt.application.font.family
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
