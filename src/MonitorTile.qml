import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// One monitor drawn at map scale. A press selects it; moving the mouse past a
// few pixels starts a drag, handled by the map (the preview never touches Hyprland).
Rectangle {
    id: tile

    required property var monitor
    property bool selected: false
    property bool dragging: false
    // Pointer positions are reported in the parent's (the map's) coordinates.
    signal pressedAt(point p)
    signal movedTo(point p)
    signal releasedAt(point p)

    readonly property bool focusedMonitor: monitor.focused

    color: focusedMonitor ? Qt.rgba(Qt.color(theme.accent).r, Qt.color(theme.accent).g, Qt.color(theme.accent).b, 0.22)
                          : "#1c1c22"
    border.width: selected ? 3 : 1
    border.color: selected ? theme.accent : monitor.warn ? "#e0a030" : "#4a4a55"
    opacity: dragging ? 0.85 : 1
    radius: Math.min(4, theme.radius)
    clip: true

    Column {
        anchors.centerIn: parent
        spacing: 2
        width: parent.width - 12

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: tile.monitor.name
            font.bold: true
            font.pixelSize: Math.max(14, Math.min(34, tile.height * 0.22))
            elide: Text.ElideRight
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: tile.height > 70
            text: tile.monitor.width + "×" + tile.monitor.height
            opacity: 0.75
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: tile.focusedMonitor && tile.height > 90
            text: qsTr("Focused")
            color: theme.accent
            font.pixelSize: 12
            font.bold: true
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        cursorShape: !tile.monitor.movable ? Qt.ArrowCursor : tile.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => tile.pressedAt(mapToItem(tile.parent, mouse.x, mouse.y))
        onPositionChanged: mouse => { if (pressed) tile.movedTo(mapToItem(tile.parent, mouse.x, mouse.y)) }
        onReleased: mouse => tile.releasedAt(mapToItem(tile.parent, mouse.x, mouse.y))
    }
}
