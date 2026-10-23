import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Keystone

// What of the building is shown: the storey slider that peels floors away,
// X-ray by discipline, and the section plane.
Rectangle {
    id: toolbar

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    implicitWidth: row.implicitWidth + 24
    implicitHeight: row.implicitHeight + 12
    radius: theme.radius
    color: Qt.rgba(0.07, 0.07, 0.08, 0.92)
    border.color: Qt.rgba(1, 1, 1, 0.08)

    // A row of small mutually exclusive buttons.
    component Choice: ToolButton {
        property int value
        property int current
        signal chosen(int value)
        checkable: true
        checked: current === value
        onClicked: chosen(value)
        font.pixelSize: 12
        implicitHeight: 32
        padding: 6
    }

    component Caption: Label {
        font.pixelSize: 12
        color: Qt.rgba(1, 1, 1, 0.55)
        Layout.alignment: Qt.AlignVCenter
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Caption {
            text: qsTr("Storeys")
            visible: storeySlider.visible
        }
        Slider {
            id: storeySlider
            visible: backend.storeys.length > 1
            Layout.preferredWidth: 140
            from: 1
            to: Math.max(1, backend.storeys.length)
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: backend.storeyLimit
            onMoved: backend.storeyLimit = Math.round(value)
        }
        Label {
            visible: storeySlider.visible
            Layout.preferredWidth: 150
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            text: backend.storeyLimit >= backend.storeys.length ? qsTr("All storeys")
                : qsTr("Up to %1").arg(backend.storeys[backend.storeyLimit - 1])
        }

        Rectangle {
            visible: storeySlider.visible
            implicitWidth: 1
            implicitHeight: 22
            color: Qt.rgba(1, 1, 1, 0.12)
        }

        Caption { text: qsTr("X-ray") }
        Repeater {
            model: [
                { value: Backend.All, label: qsTr("All") },
                { value: Backend.Structure, label: qsTr("Structure") },
                { value: Backend.Services, label: qsTr("MEP") },
                { value: Backend.Architecture, label: qsTr("Architecture") },
            ]
            delegate: Choice {
                required property var modelData
                text: modelData.label
                value: modelData.value
                current: backend.xray
                onChosen: (v) => backend.xray = v
            }
        }

        Rectangle {
            implicitWidth: 1
            implicitHeight: 22
            color: Qt.rgba(1, 1, 1, 0.12)
        }

        Caption { text: qsTr("Section") }
        Repeater {
            model: [
                { value: Backend.NoSection, label: qsTr("Off") },
                { value: Backend.Plan, label: qsTr("Plan") },
                { value: Backend.AlongX, label: "X" },
                { value: Backend.AlongZ, label: "Z" },
            ]
            delegate: Choice {
                required property var modelData
                text: modelData.label
                value: modelData.value
                current: backend.sectionAxis
                onChosen: (v) => backend.sectionAxis = v
            }
        }
        Slider {
            visible: backend.sectionAxis !== Backend.NoSection
            Layout.preferredWidth: 140
            from: 0
            to: 1
            value: backend.sectionPosition
            onMoved: backend.sectionPosition = value
        }
        ToolButton {
            visible: backend.sectionAxis !== Backend.NoSection
            text: "⇅"
            checkable: true
            checked: backend.sectionFlipped
            onClicked: backend.sectionFlipped = checked
            implicitHeight: 32
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Keep the other side (F)")
        }
    }
}
