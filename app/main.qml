import QtQuick
import QtQuick.Window
import QtQuick.Dialogs

Window {
    id: win
    width: 400; height: 740; minimumWidth: 340; minimumHeight: 520
    visible: true; title: "PocketDrop"; color: theme.base

    PocketDropView { id: view; anchors.fill: parent }

    Connections {
        target: pocketDrop
        function onBrowseRequested(folders) { if (folders) view.chooseFolder(); else view.chooseFiles() }
        function onChooseInboxRequested() { view.chooseInbox() }
        function onAlertRequested(title, message) { alertBox.text = title; alertBox.informativeText = message; alertBox.open() }
        function onTopmostRequested(on) { win.flags = on ? (win.flags | Qt.WindowStaysOnTopHint) : (win.flags & ~Qt.WindowStaysOnTopHint) }
        function onAttentionRequested() { if (!win.active) win.alert(0) }
    }
    MessageDialog { id: alertBox; buttons: MessageDialog.Ok }
}
