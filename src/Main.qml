import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 700
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: qsTr("Displays")
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Shift+?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    readonly property bool busy: helpVisible || quitDialog.visible
    readonly property var warningIssues: backend.issues.filter(i => i.severity === "warning")

    function requestQuit() {
        if (backend.hasPending)
            quitDialog.open()
        else
            Qt.quit()
    }
    function pendingText(p) {
        return qsTr("%1: %2, %3 → %4, %5").arg(p.name).arg(p.fromX).arg(p.fromY).arg(p.toX).arg(p.toY)
    }

    Shortcut {
        sequence: "Escape"; context: Qt.ApplicationShortcut
        enabled: win.helpVisible || identifier.active || backend.hasPending || map.dragging
        onActivated: {
            if (win.helpVisible || identifier.active) { win.helpVisible = false; identifier.hide() }
            else if (map.dragging) map.cancelDrag()
            else backend.resetPending()
        }
    }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !quitDialog.visible; onActivated: win.requestQuit() }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; onActivated: identifier.show(backend.selected.name || "") }
    Connections { target: backend; function onSelectedIndexChanged() { identifier.setHighlight(backend.selected.name || "") } }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequence: "F5"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(1) }
    Shortcut { sequence: "Backtab"; context: Qt.ApplicationShortcut; onActivated: backend.selectNext(-1) }
    // Arrows move the selected monitor in the preview: 10 px, Shift 100 px, Ctrl 1 px.
    Repeater {
        model: [
            { key: "Left", dx: -1, dy: 0 }, { key: "Right", dx: 1, dy: 0 },
            { key: "Up", dx: 0, dy: -1 }, { key: "Down", dx: 0, dy: 1 }
        ]
        delegate: Item {
            required property var modelData
            Shortcut { sequence: modelData.key; context: Qt.ApplicationShortcut; enabled: !win.busy
                onActivated: backend.moveSelected(modelData.dx * 10, modelData.dy * 10) }
            Shortcut { sequence: "Shift+" + modelData.key; context: Qt.ApplicationShortcut; enabled: !win.busy
                onActivated: backend.moveSelected(modelData.dx * 100, modelData.dy * 100) }
            Shortcut { sequence: "Ctrl+" + modelData.key; context: Qt.ApplicationShortcut; enabled: !win.busy
                onActivated: backend.moveSelected(modelData.dx, modelData.dy) }
        }
    }
    Shortcut { sequence: "Ctrl+Z"; context: Qt.ApplicationShortcut; enabled: !win.busy; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; context: Qt.ApplicationShortcut; enabled: !win.busy; onActivated: backend.redo() }
    Shortcut { sequence: "Ctrl+Y"; context: Qt.ApplicationShortcut; enabled: !win.busy; onActivated: backend.redo() }

    onClosing: close => {
        if (backend.hasPending) {
            close.accepted = false
            quitDialog.open()
        }
    }

    Dialog {
        id: quitDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Quit with unsaved changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        onDiscarded: Qt.quit()
        Label {
            width: Math.min(380, win.width - 120)
            wrapMode: Text.Wrap
            text: qsTr("Your new monitor positions were not applied and will be lost.")
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            // Read-only notice: always true in this version, and louder when
            // the Hyprland or Omarchy version has not been tested.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: notice.implicitHeight + 16
                radius: Math.min(6, theme.radius)
                color: backend.versionsTested ? "#1a2a1f" : "#3a2c10"
                border.color: backend.versionsTested ? "#2f5a3b" : "#a37a1a"
                ColumnLayout {
                    id: notice
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.bold: true
                        text: backend.versionsTested
                            ? qsTr("Preview mode: you can arrange the monitors here, but nothing is applied or changed.")
                            : qsTr("Read-only mode: untested version. Nothing will be changed.")
                    }
                    Repeater {
                        model: backend.warnings
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: modelData
                            opacity: 0.85
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: backend.error !== ""
                implicitHeight: errorText.implicitHeight + 16
                radius: Math.min(6, theme.radius)
                color: "#3a1616"
                border.color: "#b03a3a"
                Label {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.Wrap
                    text: backend.error
                }
            }

            // The banners keep their height while a monitor is being dragged, so
            // the map does not move under the cursor when a warning comes or goes.
            Item {
                id: bannerBox
                Layout.fillWidth: true
                property real held: 0
                readonly property real wanted: bannerCol.implicitHeight
                Layout.preferredHeight: map.dragging ? Math.max(held, wanted) : wanted
                visible: Layout.preferredHeight > 0
                Connections {
                    target: map
                    function onDraggingChanged() { bannerBox.held = map.dragging ? bannerBox.wanted : 0 }
                }
                ColumnLayout {
                    id: bannerCol
                    width: parent.width
                    spacing: 10

                    // Layout problems, checked live (also while dragging).
                    Rectangle {
                        Layout.fillWidth: true
                        visible: win.warningIssues.length > 0
                        implicitHeight: warnCol.implicitHeight + 16
                        radius: Math.min(6, theme.radius)
                        color: "#3a2c10"
                        border.color: "#a37a1a"
                        ColumnLayout {
                            id: warnCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            Label { text: qsTr("Layout warnings"); font.bold: true }
                            Repeater {
                                model: win.warningIssues
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: "• " + modelData.message + (modelData.hint ? " " + modelData.hint : "")
                                }
                            }
                        }
                    }

                    // Workspaces: notes about Omarchy's blocks (information, not errors).
                    Rectangle {
                        Layout.fillWidth: true
                        visible: backend.workspaceNotices.length > 0 || backend.workspaceError !== ""
                        implicitHeight: wsCol.implicitHeight + 16
                        radius: Math.min(6, theme.radius)
                        color: "#182230"
                        border.color: "#3a5580"
                        ColumnLayout {
                            id: wsCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2
                            Label { text: qsTr("Workspace notes"); font.bold: true }
                            Repeater {
                                model: backend.workspaceNotices
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: "• " + modelData.message
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: backend.workspaceError !== ""
                                wrapMode: Text.Wrap
                                color: "#ffd27a"
                                text: backend.workspaceError
                            }
                        }
                    }

                    // Pending (previewed) positions versus the real ones.
                    Rectangle {
                        Layout.fillWidth: true
                        visible: backend.hasPending
                        implicitHeight: pendCol.implicitHeight + 16
                        radius: Math.min(6, theme.radius)
                        color: "#13293a"
                        border.color: "#2f6a99"
                        ColumnLayout {
                            id: pendCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("Unsaved changes — not applied"); font.bold: true; Layout.fillWidth: true }
                                Button { text: qsTr("Undo"); flat: true; focusPolicy: Qt.NoFocus; enabled: backend.canUndo; onClicked: backend.undo() }
                                Button { text: qsTr("Redo"); flat: true; focusPolicy: Qt.NoFocus; enabled: backend.canRedo; onClicked: backend.redo() }
                                Button { text: qsTr("Reset"); flat: true; focusPolicy: Qt.NoFocus; onClicked: backend.resetPending() }
                            }
                            Repeater {
                                model: backend.pendingList
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: win.pendingText(modelData)
                                    font.family: "monospace"
                                    opacity: 0.9
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: backend.pendingNotice !== ""
                                wrapMode: Text.Wrap
                                color: "#ffd27a"
                                text: backend.pendingNotice
                            }
                        }
                    }

                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: backend.monitors
                    delegate: Button {
                        required property int index
                        required property var modelData
                        text: modelData.name + (modelData.disabled ? " " + qsTr("(disabled)") : "")
                        checkable: true
                        checked: index === backend.selectedIndex
                        flat: !checked
                        focusPolicy: Qt.NoFocus
                        onClicked: backend.selectedIndex = index
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Identify")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: identifier.show(backend.selected.name || "")
                }
                Button {
                    text: qsTr("Refresh")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: backend.refresh()
                }
                Button {
                    text: "?"
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: win.helpVisible = true
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: identifier.warning !== ""
                implicitHeight: idWarn.implicitHeight + 16
                radius: Math.min(6, theme.radius)
                color: "#3a2c10"
                border.color: "#a37a1a"
                Label {
                    id: idWarn
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.Wrap
                    text: identifier.warning
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                MonitorMap {
                    id: map
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    monitors: backend.monitors
                    issues: backend.issues
                    selectedIndex: backend.selectedIndex
                    onMonitorClicked: index => backend.selectedIndex = index
                }
                MonitorPanel {
                    Layout.preferredWidth: 340
                    Layout.fillHeight: true
                    monitor: backend.selected
                    issues: backend.issues
                    workspaceNotices: backend.workspaceNotices
                }
            }

            Label {
                text: qsTr("Hyprland %1 · Omarchy %2").arg(backend.hyprlandVersion || "?").arg(backend.omarchyVersion || "?")
                      + "   ·   " + qsTr("? help")
                opacity: 0.5
                font.pixelSize: 12
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onDismissed: win.helpVisible = false
        }
    }
}
