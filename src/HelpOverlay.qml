import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown on top of the window.
Rectangle {
    id: help
    color: Qt.rgba(0, 0, 0, 0.7)
    signal dismissed()

    MouseArea { anchors.fill: parent; onClicked: help.dismissed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 560)
        height: col.implicitHeight + 40
        color: "#1c1c22"
        radius: theme.radius
        border.color: theme.accent

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            Label { text: qsTr("Shortcuts"); font.pixelSize: 20; font.bold: true }
            GridLayout {
                columns: 2
                columnSpacing: 24
                rowSpacing: 6
                Label { text: "?"; font.bold: true }
                Label { text: qsTr("Show or hide this help") }
                Label { text: "R"; font.bold: true }
                Label { text: qsTr("Refresh the monitors") }
                Label { text: "Tab, Shift+Tab"; font.bold: true }
                Label { text: qsTr("Select another monitor") }
                Label { text: qsTr("Drag"); font.bold: true }
                Label { text: qsTr("Move a monitor; edges snap to the others (preview only)") }
                Label { text: qsTr("Arrows"); font.bold: true }
                Label { text: qsTr("Move the selected monitor 10 px (Shift: 100 px, Ctrl: 1 px)") }
                Label { text: "Ctrl+Z"; font.bold: true }
                Label { text: qsTr("Undo the last move") }
                Label { text: "Ctrl+Shift+Z"; font.bold: true }
                Label { text: qsTr("Redo") }
                Label { text: "Esc"; font.bold: true }
                Label { text: qsTr("Reset to the real positions (or cancel a drag)") }
                Label { text: "I"; font.bold: true }
                Label { text: qsTr("Identify: show each monitor's name on its screen") }
                Label { text: qsTr("Workspace tags"); font.bold: true }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("On each monitor: id and window count. Filled with the accent: shown now. ◆: persistent. Dimmed: empty. ⚠: outside the monitor's Omarchy block. +N: more. Read only.")
                }
                Label { text: "Q"; font.bold: true }
                Label { text: qsTr("Quit") }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("This version only previews. Nothing you move is applied and your configuration is never changed.")
                wrapMode: Text.Wrap
                opacity: 0.7
            }
        }
    }
}
