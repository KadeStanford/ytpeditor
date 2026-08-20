import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: commandPaletteRoot
    objectName: "commandPalette"
    property var theme
    signal commandTriggered(string command)
    modal: true
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 40 : 560)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: commandPaletteRoot.theme.bgRaised; radius: 6; border.color: commandPaletteRoot.theme.borderStrong }
    property var commands: [
        {id:"import", title:"Import media", group:"File", keys:"Ctrl+I"},
        {id:"save", title:"Save exact session", group:"File", keys:"Ctrl+S"},
        {id:"export", title:"Export video", group:"File", keys:""},
        {id:"split", title:"Split at playhead", group:"Timeline", keys:"S"},
        {id:"marker", title:"Add marker", group:"Timeline", keys:""},
        {id:"fit", title:"Fit timeline", group:"Timeline", keys:""},
        {id:"reverse", title:"Reverse selected clip", group:"YTP", keys:""},
        {id:"stutter", title:"Stutter selected clip", group:"YTP", keys:""},
        {id:"shake", title:"Add screen shake", group:"YTP", keys:""},
        {id:"source", title:"Show Source viewer", group:"View", keys:""},
        {id:"program", title:"Show Program viewer", group:"View", keys:""},
        {id:"library", title:"Toggle media panel", group:"View", keys:""},
        {id:"inspector", title:"Toggle inspector", group:"View", keys:""}
    ]
    function openWithQuery(value) { query.text = value || ""; open(); query.forceActiveFocus() }
    ColumnLayout {
        width: commandPaletteRoot.width; spacing: 0
        TextField { id: query; Layout.fillWidth: true; Layout.margins: 10; placeholderText: "Type a command…"; font.pixelSize: 15; onAccepted: {
            for (let i=0;i<commandPaletteRoot.commands.length;++i) if (commandRepeater.itemAt(i).visible) { commandPaletteRoot.commandTriggered(commandPaletteRoot.commands[i].id); commandPaletteRoot.close(); return }
        } }
        Rectangle { Layout.fillWidth: true; height: 1; color: commandPaletteRoot.theme.border }
        ColumnLayout { Layout.fillWidth: true; Layout.margins: 6; spacing: 1
            Repeater { id: commandRepeater; model: commandPaletteRoot.commands
                ToolButton {
                    id: commandButton
                    required property var modelData
                    visible: query.text.trim() === "" || (modelData.title + " " + modelData.group).toLowerCase().indexOf(query.text.toLowerCase()) >= 0
                    Layout.fillWidth: true; implicitHeight: visible ? 38 : 0
                    contentItem: RowLayout { Label { text: commandButton.modelData.title; color: commandPaletteRoot.theme.text; Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft } Label { text: commandButton.modelData.group + (commandButton.modelData.keys ? "  " + commandButton.modelData.keys : ""); color: commandPaletteRoot.theme.muted; font.pixelSize: 11 } }
                    background: Rectangle { radius: 4; color: parent.hovered || parent.activeFocus ? commandPaletteRoot.theme.bgHover : "transparent" }
                    onClicked: { commandPaletteRoot.commandTriggered(modelData.id); commandPaletteRoot.close() }
                }
            }
        }
    }
}
