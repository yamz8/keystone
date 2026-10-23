import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The selected element: what it is, where it sits, what it's made of, and
// every property set and quantity the file gives it.
Rectangle {
    id: panel
    property var selection: ({})
    signal closeRequested()

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    color: Qt.rgba(0.07, 0.07, 0.08, 0.94)
    radius: theme.radius
    border.color: Qt.rgba(1, 1, 1, 0.08)
    border.width: 1

    // Rows of label and value; values can be selected and copied.
    component Rows: GridLayout {
        property var rows: []
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 14
        rowSpacing: 6

        Repeater {
            model: parent.rows.length * 2
            delegate: Item {
                required property int index
                readonly property var row: parent.rows[Math.floor(index / 2)]
                readonly property bool isLabel: index % 2 === 0
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: !isLabel
                Layout.preferredWidth: isLabel ? Math.min(labelText.implicitWidth, 128) : 0
                implicitHeight: isLabel ? labelText.implicitHeight : valueText.implicitHeight

                Label {
                    id: labelText
                    visible: isLabel
                    width: parent.width
                    text: row ? row.label : ""
                    color: Qt.rgba(1, 1, 1, 0.5)
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    // Explicit, so right-to-left mirroring flips it.
                    horizontalAlignment: Text.AlignLeft
                }
                TextEdit {
                    id: valueText
                    visible: !isLabel
                    width: parent.width
                    text: row ? row.value : ""
                    color: Qt.rgba(1, 1, 1, 0.9)
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignLeft
                    readOnly: true
                    selectByMouse: true
                    selectionColor: theme.accent
                    selectedTextColor: theme.accentForeground
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.rightMargin: 8
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: 10
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: panel.selection.ifcClass || ""
                    horizontalAlignment: Text.AlignLeft
                    color: theme.accent
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: panel.selection.name || qsTr("Unnamed")
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
            ToolButton {
                Layout.alignment: Qt.AlignTop
                text: "✕"
                font.pixelSize: 14
                onClicked: panel.closeRequested()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close (Esc)")
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroll.availableWidth - 10
                spacing: 18

                Rows { rows: panel.selection.summary || [] }

                Repeater {
                    model: panel.selection.sections || []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Qt.rgba(1, 1, 1, 0.08)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.title
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: Qt.rgba(1, 1, 1, 0.75)
                            wrapMode: Text.Wrap
                        }
                        Rows { rows: modelData.rows }
                    }
                }
            }
        }
    }
}
