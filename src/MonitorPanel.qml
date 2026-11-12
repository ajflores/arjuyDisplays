import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Details of the selected monitor, including every mode it offers.
Rectangle {
    id: panel

    property var monitor: ({})
    property var issues: []
    readonly property var mine: has ? issues.filter(i => i.monitors.indexOf(monitor.name) >= 0) : []
    readonly property bool has: monitor && monitor.name !== undefined

    color: "#17171c"
    radius: Math.min(8, theme.radius)
    border.color: "#2a2a32"

    function transformName(t) {
        const names = [qsTr("Normal"), qsTr("90°"), qsTr("180°"), qsTr("270°"),
                       qsTr("Flipped"), qsTr("Flipped 90°"), qsTr("Flipped 180°"), qsTr("Flipped 270°")]
        return names[t] !== undefined ? names[t] : String(t)
    }
    function num(v, digits) { return Number(v).toLocaleString(Qt.locale(), 'f', digits) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        visible: panel.has

        Label {
            text: panel.has ? panel.monitor.name : ""
            font.pixelSize: 26
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: panel.has ? (panel.monitor.description || "") : ""
            opacity: 0.7
            wrapMode: Text.Wrap
            visible: text.length > 0
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 14
            rowSpacing: 5

            Label { text: qsTr("Resolution"); opacity: 0.6 }
            Label { text: panel.has ? panel.monitor.width + "×" + panel.monitor.height : "" ; Layout.fillWidth: true }
            Label { text: qsTr("Refresh rate"); opacity: 0.6 }
            Label { text: panel.has ? qsTr("%1 Hz").arg(translator.language && panel.num(panel.monitor.refreshRate, 2)) : "" }
            Label { text: qsTr("Scale"); opacity: 0.6 }
            Label { text: panel.has ? panel.num(panel.monitor.scale, 2) : "" }
            Label { text: qsTr("Transform"); opacity: 0.6 }
            Label { text: panel.has ? panel.transformName(panel.monitor.transform) : "" }
            Label { text: qsTr("Position"); opacity: 0.6 }
            Label { text: panel.has ? panel.monitor.currentX + ", " + panel.monitor.currentY : "" }
            Label { text: qsTr("Pending position"); opacity: 0.6; visible: panel.has && panel.monitor.moved }
            Label {
                text: panel.has ? panel.monitor.x + ", " + panel.monitor.y : ""
                visible: panel.has && panel.monitor.moved
                color: theme.accent
                font.bold: true
            }
            Label { text: qsTr("Logical size"); opacity: 0.6 }
            Label {
                text: panel.has ? panel.num(panel.monitor.logicalWidth, 0) + "×" + panel.num(panel.monitor.logicalHeight, 0) : ""
            }
            Label { text: qsTr("Make"); opacity: 0.6; visible: panel.has && panel.monitor.make !== "" }
            Label { text: panel.has ? panel.monitor.make : ""; visible: panel.has && panel.monitor.make !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("Model"); opacity: 0.6; visible: panel.has && panel.monitor.model !== "" }
            Label { text: panel.has ? panel.monitor.model : ""; visible: panel.has && panel.monitor.model !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("Serial"); opacity: 0.6; visible: panel.has && panel.monitor.serial !== "" }
            Label { text: panel.has ? panel.monitor.serial : ""; visible: panel.has && panel.monitor.serial !== "" }
            Label { text: qsTr("VRR"); opacity: 0.6 }
            Label { text: panel.has && panel.monitor.vrr ? qsTr("On") : qsTr("Off") }
            Label { text: qsTr("Mirror of"); opacity: 0.6; visible: panel.has && panel.monitor.mirrorOf !== "" }
            Label { text: panel.has ? panel.monitor.mirrorOf : ""; visible: panel.has && panel.monitor.mirrorOf !== "" }
            Label { text: qsTr("Status"); opacity: 0.6 }
            Label {
                text: !panel.has ? "" : panel.monitor.disabled ? qsTr("Disabled")
                    : panel.monitor.focused ? qsTr("Enabled, focused") : qsTr("Enabled")
            }
        }

        Label { text: qsTr("Layout checks"); font.bold: true }
        ListView {
            id: checks
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 140)
            clip: true
            model: panel.mine
            spacing: 4
            ScrollBar.vertical: ScrollBar {}
            delegate: Label {
                required property var modelData
                width: checks.width
                wrapMode: Text.Wrap
                font.pixelSize: 12
                text: (modelData.severity === "warning" ? "⚠ " : "• ") + modelData.message
                      + (modelData.hint ? " " + modelData.hint : "")
                color: modelData.severity === "warning" ? "#e0a030" : Material.foreground
            }
        }
        Label {
            visible: panel.mine.length === 0
            text: qsTr("No layout problems")
            opacity: 0.6
            font.pixelSize: 12
        }

        Label {
            text: panel.has ? qsTr("Available modes (%n)", "", panel.monitor.availableModes.length) : ""
            font.bold: true
        }
        ListView {
            id: modes
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.has ? panel.monitor.availableModes : []
            ScrollBar.vertical: ScrollBar {}
            delegate: Label {
                required property string modelData
                width: modes.width
                text: modelData
                font.family: "monospace"
                opacity: 0.85
                readonly property bool current: panel.has
                    && modelData === panel.monitor.width + "x" + panel.monitor.height + "@" + Number(panel.monitor.refreshRate).toFixed(2) + "Hz"
                color: current ? theme.accent : Material.foreground
                font.bold: current
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !panel.has
        text: qsTr("Select a monitor")
        opacity: 0.6
    }
}
