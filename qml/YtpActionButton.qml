import QtQuick
import QtQuick.Controls

Button {
    id: control
    required property var appTheme
    property string description: ""
    property bool showDescription: false
    property string badge: ""
    implicitHeight: showDescription ? 66 : 46
    font.pixelSize: 12
    font.weight: Font.Medium
    leftPadding: 12; rightPadding: 8
    contentItem: Column {
        spacing: 2
        Label { width: parent.width; text: control.text + (control.badge !== "" ? "  " + control.badge : ""); color: control.enabled ? control.appTheme.text : control.appTheme.faint; font: control.font; elide: Text.ElideRight }
        Label { width: parent.width; visible: control.showDescription; text: control.description; color: control.enabled ? control.appTheme.muted : control.appTheme.faint; font.pixelSize: 9; elide: Text.ElideRight }
    }
    background: Rectangle {
        radius: 4
        color: control.down ? control.appTheme.bgActive : control.hovered ? control.appTheme.bgHover : control.appTheme.bgRaised
        border.color: control.hovered || control.activeFocus ? control.appTheme.accent : control.appTheme.border
        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; color: control.appTheme.accent; radius: 2 }
    }
    ToolTip.visible: hovered && description !== ""
    ToolTip.delay: 400
    ToolTip.text: description
}
