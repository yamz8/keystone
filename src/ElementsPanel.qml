import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The model tree: every element under its storey, searchable, so things can
// be found without hunting for them in 3D.
Rectangle {
    id: panel
    signal frameRequested()
    signal closeRequested()

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
    }

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    color: Qt.rgba(0.07, 0.07, 0.08, 0.94)
    radius: theme.radius
    border.color: Qt.rgba(1, 1, 1, 0.08)

    // Keep the selection in view when it's picked in 3D.
    Connections {
        target: backend
        function onSelectionChanged() {
            const row = backend.elements.rowOf(backend.selectedElement)
            if (row >= 0)
                list.positionViewAtIndex(row, ListView.Contain)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("Elements")
                font.pixelSize: 15
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignLeft
            }
            Label {
                text: backend.elements.count === backend.elements.total ? backend.elements.total.toLocaleString(Qt.locale(), 'f', 0)
                    : qsTr("%1 of %2").arg(backend.elements.count.toLocaleString(Qt.locale(), 'f', 0))
                                     .arg(backend.elements.total.toLocaleString(Qt.locale(), 'f', 0))
                font.pixelSize: 12
                color: Qt.rgba(1, 1, 1, 0.5)
            }
            ToolButton {
                text: "✕"
                font.pixelSize: 13
                onClicked: panel.closeRequested()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close (T)")
            }
        }

        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: qsTr("Search elements")
            text: backend.elements.filter
            onTextEdited: backend.elements.filter = text
            Keys.onDownPressed: {
                list.forceActiveFocus()
                list.currentIndex = 0
            }
            Keys.onReturnPressed: {
                if (list.count > 0) {
                    backend.select(list.itemAtIndex(0) ? list.itemAtIndex(0).element : -1)
                    panel.frameRequested()
                }
            }
            Keys.onEscapePressed: (event) => {
                if (text !== "") {
                    text = ""
                    backend.elements.filter = ""
                } else {
                    panel.forceActiveFocus()
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.elements
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: false
            Keys.onReturnPressed: {
                if (currentItem) {
                    backend.select(currentItem.element)
                    panel.frameRequested()
                }
            }
            Keys.onEscapePressed: panel.forceActiveFocus()

            section.property: "storey"
            section.criteria: ViewSection.FullString
            section.delegate: Label {
                required property string section
                width: ListView.view.width
                topPadding: 10
                bottomPadding: 4
                text: section
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: theme.accent
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
            }

            delegate: ItemDelegate {
                id: row
                required property int index
                required property int element
                required property string name
                required property string ifcClass
                required property int shown
                width: ListView.view.width
                implicitHeight: 40
                highlighted: element === backend.selectedElement || ListView.isCurrentItem
                // Hidden by the storey slider, or ghosted by X-ray.
                opacity: shown === 1 ? 1 : 0.4
                enabled: shown !== 0
                onClicked: {
                    list.currentIndex = index
                    backend.select(element)
                }
                onDoubleClicked: {
                    backend.select(element)
                    panel.frameRequested()
                }
                contentItem: ColumnLayout {
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: row.name || row.ifcClass || qsTr("Unnamed")
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignLeft
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: row.name !== "" && row.ifcClass !== ""
                        text: row.ifcClass
                        font.pixelSize: 11
                        color: Qt.rgba(1, 1, 1, 0.5)
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignLeft
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: list.count === 0
            text: qsTr("Nothing matches “%1”.").arg(search.text)
            color: Qt.rgba(1, 1, 1, 0.5)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
        }
    }
}
