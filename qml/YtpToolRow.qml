import QtQuick
import QtQuick.Controls

Item {
    id: row
    objectName: "ytpToolRow"
    required property var appTheme
    property string text: ""
    property string description: ""
    property string iconKind: "spark"
    property string shortcut: ""
    property bool hasOptions: false
    property bool active: false
    signal triggered()
    signal optionsRequested()
    implicitHeight: 32

    Button {
        id: mainButton
        anchors.fill: parent
        enabled: row.enabled
        leftPadding: 8
        rightPadding: row.hasOptions ? 34 : 8
        background: Rectangle {
            radius: 3
            color: mainButton.down ? row.appTheme.bgActive
                  : row.active ? "#352f5a"
                  : mainButton.hovered ? row.appTheme.panelHover : "transparent"
            border.width: mainButton.activeFocus ? 1 : 0
            border.color: row.appTheme.cyan
        }
        contentItem: Row {
            spacing: 8
            YtpToolIcon { width:18;height:18;anchors.verticalCenter:parent.verticalCenter;kind:row.iconKind;color:row.enabled?(row.active?row.appTheme.accentBright:row.appTheme.muted):row.appTheme.faint }
            Label { width:Math.max(0,parent.width-shortcutLabel.width-26);anchors.verticalCenter:parent.verticalCenter;text:row.text;color:row.enabled?row.appTheme.text:row.appTheme.faint;font.pixelSize:11;font.weight:row.active?Font.DemiBold:Font.Medium;elide:Text.ElideRight }
            Label { id:shortcutLabel;anchors.verticalCenter:parent.verticalCenter;text:row.shortcut;color:row.appTheme.faint;font.pixelSize:9;visible:text!=="" }
        }
        onClicked: row.triggered()
        ToolTip.visible: hovered && row.description !== ""
        ToolTip.delay: 500
        ToolTip.text: row.description
    }

    ToolButton {
        id: optionsButton
        visible: row.hasOptions
        enabled: row.enabled
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        width: 27; height: 26
        background: Rectangle { radius:3;color:optionsButton.down?row.appTheme.bgActive:optionsButton.hovered?row.appTheme.panelHigh:"transparent" }
        contentItem: Canvas {
            onPaint: { const c=getContext("2d");c.reset();c.fillStyle=row.enabled?row.appTheme.muted:row.appTheme.faint;for(let x=8;x<=18;x+=5){c.beginPath();c.arc(x,13,1.2,0,6.3);c.fill()} }
        }
        onClicked: row.optionsRequested()
        ToolTip.visible: hovered
        ToolTip.text: "Custom settings"
    }
}
