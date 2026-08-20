import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: strip
    objectName: "statusStrip"
    property var theme
    property bool busy: false
    property bool dirty: false
    property string detailText: "Ready"
    height: 24
    background: Rectangle { color: strip.theme.bgSunken; Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: strip.theme.border } }
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9; spacing: 7
        Rectangle { width: 6; height: 6; radius: 3; color: strip.busy ? strip.theme.amber : strip.theme.green }
        Label { text: strip.busy ? "Background task running" : "Ready"; color: strip.theme.muted; font.pixelSize: 11 }
        BusyIndicator { running: strip.busy; visible: running; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
        Item { Layout.fillWidth: true }
        Label { text: strip.dirty ? "Autosave on" : "Autosaved ✓"; color: strip.dirty ? strip.theme.muted : strip.theme.green; font.pixelSize: 11 }
    }
    ToolTip.visible: detailHover.containsMouse
    ToolTip.text: detailText
    MouseArea { id: detailHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
}
