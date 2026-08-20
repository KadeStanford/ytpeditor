import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: bar
    objectName: "nleTopBar"
    property var theme
    property string projectName: "Untitled YTP"
    property bool dirty: false
    property bool canUndo: false
    property bool canRedo: false
    property bool busy: false
    property bool exportBusy: false
    property bool exportEnabled: false
    property bool relinkVisible: false
    property string missingMediaName: ""
    signal newRequested()
    signal openRequested()
    signal importRequested()
    signal saveRequested()
    signal relinkRequested()
    signal undoRequested()
    signal redoRequested()
    signal exportRequested()
    signal shortcutEditorRequested()
    signal guideRequested()
    signal snapshotRequested()
    signal commandPaletteRequested(string scope)
    signal toggleLibraryRequested()
    signal toggleInspectorRequested()

    height: 46
    background: Rectangle {
        color: bar.theme.bgRaised
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: bar.theme.border }
    }

    component MenuButton: ToolButton {
        id: control
        property string helpText: ""
        implicitHeight: 30
        font.pixelSize: 12
        font.weight: Font.Medium
        leftPadding: 9; rightPadding: 9
        contentItem: Label { text: control.text; color: control.enabled ? bar.theme.text : bar.theme.faint; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font: control.font }
        background: Rectangle { radius: 4; color: control.down ? bar.theme.bgActive : control.hovered ? bar.theme.bgHover : "transparent" }
        ToolTip.visible: hovered && helpText !== ""
        ToolTip.text: helpText
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 2

        Rectangle {
            Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 5
            color: bar.theme.accent
            Label { anchors.centerIn: parent; text: "Y"; color: "white"; font.pixelSize: 17; font.weight: Font.Black }
        }
        Label { text: "YTP Editor"; color: bar.theme.text; font.weight: Font.DemiBold; Layout.leftMargin: 5; Layout.rightMargin: 12 }

        MenuButton { text: "File"; onClicked: fileMenu.popup(); Menu { id: fileMenu
            MenuItem { text: "New Project\tCtrl+N"; onTriggered: bar.newRequested() }
            MenuItem { text: "Open Session…\tCtrl+O"; onTriggered: bar.openRequested() }
            MenuItem { text: "Import Media…\tCtrl+I"; onTriggered: bar.importRequested() }
            MenuSeparator {}
            MenuItem { text: "Save Session\tCtrl+S"; onTriggered: bar.saveRequested() }
            MenuItem { text: "Relink Missing Media…"; visible: bar.relinkVisible; onTriggered: bar.relinkRequested() }
            MenuSeparator {}
            MenuItem { text: "Export Video…"; enabled: bar.exportEnabled; onTriggered: bar.exportRequested() }
        } }
        MenuButton { text: "Edit"; onClicked: editMenu.popup(); Menu { id: editMenu
            MenuItem { text: "Undo\tCtrl+Z"; enabled: bar.canUndo; onTriggered: bar.undoRequested() }
            MenuItem { text: "Redo\tCtrl+Shift+Z"; enabled: bar.canRedo; onTriggered: bar.redoRequested() }
            MenuSeparator {}
            MenuItem { text: "Keyboard Shortcuts…"; onTriggered: bar.shortcutEditorRequested() }
            MenuItem { text: "Command Palette…\tCtrl+K"; onTriggered: bar.commandPaletteRequested("") }
        } }
        MenuButton { text: "Clip"; onClicked: bar.commandPaletteRequested("clip") }
        MenuButton { text: "Timeline"; onClicked: bar.commandPaletteRequested("timeline") }
        MenuButton { text: "YTP"; onClicked: bar.commandPaletteRequested("ytp") }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label {
                anchors.centerIn: parent
                text: bar.projectName
                color: bar.theme.text
                font.pixelSize: 14
                font.weight: Font.Medium
                elide: Text.ElideMiddle
                width: Math.min(300, implicitWidth)
                horizontalAlignment: Text.AlignHCenter
            }
            Rectangle { width: 6; height: 6; radius: 3; color: bar.theme.accent; visible: bar.dirty; anchors.left: parent.horizontalCenter; anchors.leftMargin: Math.min(150, parent.children[0].implicitWidth / 2) + 8; anchors.verticalCenter: parent.verticalCenter }
        }

        MenuButton { text: "Library"; helpText: "Show or hide Media and Clips"; onClicked: bar.toggleLibraryRequested() }
        MenuButton { text: "Inspector"; helpText: "Show or hide the Inspector"; onClicked: bar.toggleInspectorRequested() }
        MenuButton { text: "↶"; implicitWidth: 30; font.pixelSize: 17; helpText: "Undo (Ctrl+Z)"; enabled: bar.canUndo; onClicked: bar.undoRequested() }
        MenuButton { text: "↷"; implicitWidth: 30; font.pixelSize: 17; helpText: "Redo (Ctrl+Shift+Z)"; enabled: bar.canRedo; onClicked: bar.redoRequested() }
        Button {
            text: bar.exportBusy ? "Exporting…" : "Export"
            enabled: bar.exportEnabled && !bar.exportBusy
            implicitWidth: 82; implicitHeight: 32
            font.weight: Font.DemiBold
            contentItem: Label { text: parent.text; color: parent.enabled ? "white" : bar.theme.faint; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font: parent.font }
            background: Rectangle { radius: 5; color: parent.enabled ? (parent.hovered ? bar.theme.accentBright : bar.theme.accent) : bar.theme.bgActive }
            onClicked: bar.exportRequested()
        }
    }
}
