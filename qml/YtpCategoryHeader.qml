import QtQuick
import QtQuick.Controls

ToolButton {
    id: header
    required property var appTheme
    property bool expanded: true
    implicitHeight: 26
    leftPadding: 8; rightPadding: 8
    font.pixelSize: 9
    font.weight: Font.DemiBold
    contentItem: Row {
        spacing: 6
        Canvas { width:8;height:8;anchors.verticalCenter:parent.verticalCenter;rotation:header.expanded?90:0
            Behavior on rotation { NumberAnimation { duration:90 } }
            onPaint:{const c=getContext("2d");c.reset();c.strokeStyle=header.appTheme.muted;c.lineWidth=1.4;c.beginPath();c.moveTo(2,1);c.lineTo(6,4);c.lineTo(2,7);c.stroke()}
        }
        Label { anchors.verticalCenter:parent.verticalCenter;text:header.text.toUpperCase();color:header.appTheme.muted;font.pixelSize:header.font.pixelSize;font.weight:header.font.weight;font.letterSpacing:.8 }
    }
    background: Rectangle {
        color: header.hovered ? header.appTheme.panelHover : "transparent"
        Rectangle { anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:1;color:header.appTheme.border }
    }
    onClicked: expanded=!expanded
}
