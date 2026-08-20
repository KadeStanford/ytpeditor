import QtQuick
import QtQuick.Controls

Button {
    id: row
    objectName: "ytpPackRow"
    required property var appTheme
    property string name: ""
    property string description: ""
    property string badge: ""
    implicitHeight: 48
    leftPadding: 8;rightPadding: 8
    background: Rectangle {
        radius:3
        color:row.down?row.appTheme.bgActive:row.hovered?row.appTheme.panelHover:"transparent"
        border.width:row.activeFocus?1:0
        border.color:row.appTheme.cyan
        Rectangle{anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:1;color:row.appTheme.border;opacity:.65}
    }
    contentItem: Row {
        spacing:8
        YtpToolIcon{width:18;height:18;anchors.verticalCenter:parent.verticalCenter;kind:"spark";color:row.enabled?row.appTheme.muted:row.appTheme.faint}
        Column { width:Math.max(0,parent.width-badgeLabel.width-54);anchors.verticalCenter:parent.verticalCenter;spacing:1
            Label{width:parent.width;text:row.name;color:row.enabled?row.appTheme.text:row.appTheme.faint;font.pixelSize:11;font.weight:Font.Medium;elide:Text.ElideRight}
            Label{width:parent.width;text:row.description;color:row.appTheme.muted;font.pixelSize:9;elide:Text.ElideRight}
        }
        Label{id:badgeLabel;anchors.verticalCenter:parent.verticalCenter;text:row.badge;color:row.appTheme.accentBright;font.pixelSize:8;font.weight:Font.DemiBold;visible:text!==""}
        Item{width:20;height:20;anchors.verticalCenter:parent.verticalCenter
            Canvas{anchors.fill:parent;onPaint:{const c=getContext("2d");c.reset();c.strokeStyle=row.enabled?row.appTheme.muted:row.appTheme.faint;c.lineWidth=1.5;c.lineCap="round";c.beginPath();c.moveTo(5,10);c.lineTo(15,10);c.moveTo(10,5);c.lineTo(10,15);c.stroke()}}
        }
    }
    ToolTip.visible:hovered
    ToolTip.delay:600
    ToolTip.text:description
}
