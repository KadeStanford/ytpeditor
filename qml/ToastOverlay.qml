import QtQuick
import QtQuick.Controls

Item {
    id: overlay
    property var theme
    property string message: ""
    function show(text) { message = text; toastTimer.restart() }
    width: toast.implicitWidth
    height: toast.implicitHeight
    visible: toastTimer.running && message !== ""
    Rectangle {
        id: toast
        implicitWidth: Math.min(420, toastText.implicitWidth + 30)
        implicitHeight: 38
        radius: 5
        color: overlay.theme.bgActive
        border.color: overlay.theme.borderStrong
        Label { id: toastText; anchors.centerIn: parent; text: overlay.message; color: overlay.theme.text; elide: Text.ElideRight; width: Math.min(390, implicitWidth) }
    }
    Timer { id: toastTimer; interval: 2400 }
}
