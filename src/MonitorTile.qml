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

    // Workspace tags along the bottom. They are children of the tile, so they
    // follow it while it is dragged. What does not fit becomes "+N".
    readonly property var wsList: monitor.workspaces || []
    readonly property int chipRows: height >= 150 ? 3 : height >= 100 ? 2 : height >= 52 ? 1 : 0
    readonly property var chips: {
        const list = wsList
        const room = (width - 10) * chipRows * 0.85
        const est = w => 12 + 6.5 * String(w.label).length + (w.windows > 0 ? 14 : 0)
                         + (w.persistent ? 9 : 0) + (w.guest ? 11 : 0) + 3
        const all = list.reduce((s, w) => s + est(w), 0)
        if (chipRows === 0 || list.length === 0)
            return { shown: [], hidden: list.length }
        // Most useful first: active, then with windows or a warning, then persistent, then the rest.
        const rank = w => w.active ? 0 : w.windows > 0 ? 1 : w.guest ? 2 : w.persistent ? 3 : 4
        const order = list.map((w, i) => i).sort((a, b) => rank(list[a]) - rank(list[b]) || list[a].id - list[b].id)
        let budget = all <= room ? room : room - 30
        const picked = []
        for (const i of order) {
            const need = est(list[i])
            if (need > budget)
                continue
            budget -= need
            picked.push(i)
        }
        picked.sort((a, b) => a - b)
        return { shown: picked.map(i => list[i]), hidden: list.length - picked.length }
    }

    Column {
        id: labels
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(2, (tile.height - chipFlow.height - (chipFlow.height > 0 ? 4 : 0) - height) / 2)
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

    Flow {
        id: chipFlow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 5
        spacing: 3
        Repeater {
            model: tile.chips.shown
            delegate: WorkspaceChip { required property var modelData; ws: modelData }
        }
        Rectangle {
            visible: tile.chips.hidden > 0
            width: more.implicitWidth + 8
            height: 18
            radius: Math.min(4, theme.radius)
            color: "transparent"
            border.color: "#43434f"
            Label { id: more; anchors.centerIn: parent; text: "+" + tile.chips.hidden; font.pixelSize: 10; opacity: 0.8 }
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
