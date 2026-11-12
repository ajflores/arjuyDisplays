import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A small tag for one workspace: its name or id, how many windows it has and,
// by look, what it is. Active: filled with the theme accent. Persistent: a
// diamond. Empty: dimmed. Guest (its id is outside the monitor's Omarchy
// block): amber outline and a warning sign. Read only.
Rectangle {
    id: chip

    required property var ws
    property int fontSize: 10

    readonly property bool active: !!ws.active
    readonly property bool empty: ws.windows === 0

    implicitHeight: fontSize + 8
    implicitWidth: row.implicitWidth + 8
    radius: Math.min(4, theme.radius)
    color: active ? theme.accent : "#2c2c35"
    border.width: 1
    border.color: ws.guest ? "#e0a030" : active ? theme.accent : "#43434f"
    opacity: active || !empty || ws.guest ? 1 : 0.55

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 3
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: ws.label
            font.pixelSize: chip.fontSize
            font.bold: chip.active
            color: chip.active ? theme.accentForeground : Material.foreground
        }
        Rectangle {
            visible: ws.windows > 0
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(chip.fontSize + 2, windows.implicitWidth + 6)
            height: chip.fontSize + 2
            radius: height / 2
            color: chip.active ? Qt.rgba(0, 0, 0, 0.25) : Qt.rgba(1, 1, 1, 0.16)
            Label {
                id: windows
                anchors.centerIn: parent
                text: ws.windows
                font.pixelSize: chip.fontSize - 1
                font.bold: true
                color: chip.active ? theme.accentForeground : Material.foreground
            }
        }
        Label {
            visible: ws.persistent
            anchors.verticalCenter: parent.verticalCenter
            text: "◆"
            font.pixelSize: chip.fontSize - 2
            color: chip.active ? theme.accentForeground : theme.accent
        }
        Label {
            visible: ws.guest
            anchors.verticalCenter: parent.verticalCenter
            text: "⚠"
            font.pixelSize: chip.fontSize
            color: "#e0a030"
        }
    }
}
