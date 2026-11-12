import QtQuick

// The banner shown on one screen by the Identify function.
Item {
    id: root
    property string label: ""
    property string detail: ""
    property string accent: "#FFD60A"
    property bool highlighted: false
    property bool matched: true
    signal dismissed()

    readonly property color base: Qt.color(accent)

    Rectangle {
        anchors.fill: parent
        anchors.margins: 6
        radius: 28
        color: Qt.rgba(base.r * 0.35, base.g * 0.35, base.b * 0.35, 0.88)
        border.width: root.highlighted ? 8 : 3
        border.color: root.highlighted ? "#ffffff" : root.base

        Column {
            anchors.centerIn: parent
            spacing: 10
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.label
                color: "white"
                font.pixelSize: 120
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.detail
                color: "#e6e6e6"
                font.pixelSize: 30
                visible: root.detail !== ""
            }
        }
        MouseArea { anchors.fill: parent; onClicked: root.dismissed() }
    }
}
