import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The layout drawn to scale, from logical positions and sizes. Tiles can be
// dragged to preview a new arrangement; nothing here changes Hyprland.
Item {
    id: map

    property var monitors: []
    property var issues: []
    property int selectedIndex: -1
    signal monitorClicked(int index)

    readonly property real pad: 28
    readonly property real snapPixels: 10   // magnet range, in map (screen) pixels

    // Drag state, in logical coordinates.
    property int dragIndex: -1
    property bool dragging: false
    property int dragX: 0
    property int dragY: 0
    property var guides: []
    property real pressX: 0
    property real pressY: 0
    property real grabX: 0   // where inside the monitor it was grabbed, in logical px
    property real grabY: 0
    property int rawX: 0
    property int rawY: 0

    // Room around the monitors so one can be dragged beyond the current edge.
    readonly property var bounds: {
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
        let bigW = 0, bigH = 0
        for (const m of monitors) {
            if (m.disabled)
                continue
            minX = Math.min(minX, m.x)
            minY = Math.min(minY, m.y)
            maxX = Math.max(maxX, m.x + m.logicalWidth)
            maxY = Math.max(maxY, m.y + m.logicalHeight)
            bigW = Math.max(bigW, m.logicalWidth)
            bigH = Math.max(bigH, m.logicalHeight)
        }
        return isFinite(minX) ? { x: minX - bigW / 2, y: minY - bigH / 2,
                                  w: maxX - minX + bigW, h: maxY - minY + bigH } : null
    }
    readonly property real factor: bounds
        ? Math.min((width - 2 * pad) / bounds.w, (height - 2 * pad) / bounds.h) : 1
    readonly property real offsetX: bounds ? (width - bounds.w * factor) / 2 - bounds.x * factor : 0
    readonly property real offsetY: bounds ? (height - bounds.h * factor) / 2 - bounds.y * factor : 0

    function beginDrag(index, p) {
        const m = monitors[index]
        monitorClicked(index)
        if (!m || !m.movable)
            return
        dragIndex = index
        dragging = false
        pressX = p.x
        pressY = p.y
        // Positions follow the pointer absolutely (not by deltas), so the
        // monitor stays under the cursor even if the window re-lays out.
        grabX = (p.x - offsetX) / factor - m.x
        grabY = (p.y - offsetY) / factor - m.y
        rawX = m.x
        rawY = m.y
    }
    function dragTo(p) {
        if (dragIndex < 0)
            return
        if (!dragging && Math.hypot(p.x - pressX, p.y - pressY) < 4)
            return
        dragging = true
        rawX = Math.round((p.x - offsetX) / factor - grabX)
        rawY = Math.round((p.y - offsetY) / factor - grabY)
        const r = backend.dragSnap(dragIndex, rawX, rawY, snapPixels / factor)
        dragX = r.x
        dragY = r.y
        guides = r.guides
    }
    function endDrag() {
        if (dragIndex < 0)
            return
        const index = dragIndex, was = dragging
        dragIndex = -1
        dragging = false
        guides = []
        if (was)
            backend.dragEnd(index, rawX, rawY, snapPixels / factor)
    }
    function cancelDrag() {
        if (dragIndex < 0)
            return
        dragIndex = -1
        dragging = false
        guides = []
        backend.dragCancel()
    }

    Rectangle {
        anchors.fill: parent
        color: "#131317"
        radius: Math.min(8, theme.radius)
        border.color: "#2a2a32"
    }

    // Where each moved monitor really is now.
    Repeater {
        model: map.monitors.length
        delegate: Rectangle {
            required property int index
            readonly property var mon: map.monitors[index]
            visible: !!mon && !mon.disabled && (mon.moved || (map.dragging && map.dragIndex === index))
            x: map.offsetX + (mon ? mon.currentX : 0) * map.factor
            y: map.offsetY + (mon ? mon.currentY : 0) * map.factor
            width: Math.max(1, (mon ? mon.logicalWidth : 0) * map.factor)
            height: Math.max(1, (mon ? mon.logicalHeight : 0) * map.factor)
            color: "transparent"
            border.color: "#6a6a78"
            border.width: 1
            radius: Math.min(4, theme.radius)
            opacity: 0.8
        }
    }

    // The model is the count, not the array, so tiles survive every refresh
    // and preview (a tile being dragged must never be recreated).
    Repeater {
        model: map.monitors.length
        delegate: MonitorTile {
            required property int index
            monitor: map.monitors[index] || ({})
            visible: !monitor.disabled
            selected: index === map.selectedIndex
            dragging: map.dragging && map.dragIndex === index
            z: dragging ? 10 : selected ? 2 : 1
            x: map.offsetX + (dragging ? map.dragX : monitor.x) * map.factor
            y: map.offsetY + (dragging ? map.dragY : monitor.y) * map.factor
            width: Math.max(1, monitor.logicalWidth * map.factor)
            height: Math.max(1, monitor.logicalHeight * map.factor)
            onPressedAt: p => map.beginDrag(index, p)
            onMovedTo: p => map.dragTo(p)
            onReleasedAt: p => map.endDrag()
        }
    }

    // Marked strips on the problems: gaps (amber) and overlaps (red).
    Repeater {
        model: map.issues
        delegate: Rectangle {
            required property var modelData
            readonly property bool gap: modelData.kind === "gap"
            visible: modelData.hasRegion && (gap || modelData.kind === "overlap")
            x: map.offsetX + modelData.x * map.factor
            y: map.offsetY + modelData.y * map.factor
            width: Math.max(4, modelData.w * map.factor)
            height: Math.max(4, modelData.h * map.factor)
            z: 5
            color: gap ? Qt.rgba(0.88, 0.63, 0.19, 0.30) : Qt.rgba(0.85, 0.2, 0.2, 0.40)
            border.color: gap ? "#e0a030" : "#d94040"
            border.width: 1
            Label {
                anchors.centerIn: parent
                visible: parent.width > 46 && parent.height > 18
                text: qsTr("%1 px").arg(modelData.amount)
                font.pixelSize: 12
                font.bold: true
                color: parent.gap ? "#ffd27a" : "#ffb0b0"
            }
        }
    }

    // Magnet guides while dragging.
    Repeater {
        model: map.dragging ? map.guides : []
        delegate: Rectangle {
            required property var modelData
            color: theme.accent
            z: 11
            x: map.offsetX + (modelData.vertical ? modelData.pos : modelData.from) * map.factor - (modelData.vertical ? 0.5 : 0)
            y: map.offsetY + (modelData.vertical ? modelData.from : modelData.pos) * map.factor - (modelData.vertical ? 0 : 0.5)
            width: modelData.vertical ? 1 : (modelData.to - modelData.from) * map.factor
            height: modelData.vertical ? (modelData.to - modelData.from) * map.factor : 1
        }
    }

    // Coordinates of the dragged monitor.
    Rectangle {
        visible: map.dragging
        z: 12
        readonly property real tileX: map.offsetX + map.dragX * map.factor
        readonly property real tileY: map.offsetY + map.dragY * map.factor
        x: Math.max(4, Math.min(map.width - width - 4, tileX))
        y: tileY > height + 8 ? tileY - height - 4 : tileY + 4
        width: coords.implicitWidth + 14
        height: coords.implicitHeight + 8
        radius: 4
        color: "#000000cc"
        border.color: theme.accent
        Label {
            id: coords
            anchors.centerIn: parent
            text: map.dragging ? map.dragX + ", " + map.dragY : ""
            font.family: "monospace"
            font.pixelSize: 13
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !map.bounds
        text: qsTr("No monitors to show")
        opacity: 0.6
    }
}
