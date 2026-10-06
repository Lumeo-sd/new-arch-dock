/*
 * Hover-preview strip for multi-window apps.
 *
 * Shown just above/beside the dock as a separate layer-shell surface owned by
 * PreviewController.  Each cell is a live PipeWire stream of that window
 * (KWin screencasting): a frozen last frame for minimized windows, with an
 * icon fallback until the stream warms up and a dim overlay on minimized
 * ones.  Single-window apps never get here:
 * AppItem only calls showPreview() when model.windowCount > 1.
 *
 * Implementation notes (mirroring Krema, which solves the same problems):
 * - The ListModel is updated incrementally (set/append/trim keyed by uuid)
 *   instead of clear()+append, so delegates - and their PipeWire streams -
 *   survive title/state updates.
 * - Delegates whose ScreencastingRequest still has nodeId 0 get their uuid
 *   re-armed (KWin sometimes hasn't registered the window on first request).
 * - Styling follows the dock theme tokens (FishUI.Theme): same background,
 *   radius, border and text color as the dock tooltip.
 */

import QtQuick 2.12
import QtQuick.Controls 2.12
import org.kde.taskmanager as TaskManager
import org.kde.pipewire as PipeWire
import FishUI 1.0 as FishUI

Item {
    id: root

    property real cellWidth: 176
    property real cellHeight: 132
    property real spacing: 6
    property real padding: 8

    // appId the model was last (re)built for. A different app means different
    // PipeWire streams, so delegates must be recreated (clear + append);
    // the same app reuses delegates (set in place).
    property string _builtFor: ""

    // Surface-level hover: while the cursor is anywhere on the input region
    // (popup + margins, see PreviewController::applyInputRegion) the hide
    // timer stays cancelled, so travelling from the icon into the strip
    // keeps the popup open.
    HoverHandler {
        onHoveredChanged: preview.setHovered(hovered)
    }

    ListModel {
        id: windowModel
        // roles: uuid, title, active, minimized, windowIndex
    }

    function rebuild() {
        var windows = appModel.windowInfos(preview.appId)
        console.log("PreviewPopup.rebuild: appId=", preview.appId, "windows=", windows.length)
        // The window list changed under us (e.g. a window was closed from a
        // card's close button, or the mouse left and hidePreview cleared the
        // app): fewer than two windows means no strip. Clear the model and
        // zero the size first - otherwise the stale cards stay visible and
        // jump to x=0 when hidePreview resets contentX.
        if (windows.length < 2) {
            windowModel.clear()
            root._builtFor = ""
            preview.setContentSize(0, 0)
            if (preview.visible)
                preview.hidePreview()
            return
        }
        if (preview.appId !== root._builtFor) {
            windowModel.clear()
            root._builtFor = preview.appId
        }
        for (var i = 0; i < windows.length; ++i) {
            var entry = {
                uuid: windows[i].uuid,
                icon: windows[i].icon,
                title: windows[i].title,
                active: windows[i].active,
                minimized: windows[i].minimized,
                windowIndex: i
            }
            if (i < windowModel.count)
                windowModel.set(i, entry)
            else
                windowModel.append(entry)
        }
        while (windowModel.count > windows.length)
            windowModel.remove(windowModel.count - 1)

        var n = windowModel.count
        preview.setContentSize(
            n > 0 ? n * (root.cellWidth + root.spacing) - root.spacing + 2 * root.padding : 0,
            n > 0 ? root.cellHeight + 2 * root.padding : 0)

        // KWin may not have registered a window on the first request; retry
        // delegates that still have no nodeId, now and a few times later.
        root.retryScreencast()
        retryState.attempts = 0
        retryTimer.restart()
    }

    // Re-arm the screencast request on delegates with nodeId 0.
    // Returns true when every delegate has a nodeId.
    function retryScreencast() {
        var allReady = true
        for (var i = 0; i < cards.count; ++i) {
            var item = cards.itemAt(i)
            if (item && !item.retryScreencast())
                allReady = false
        }
        return allReady
    }

    Timer {
        id: retryTimer
        interval: 1500
        repeat: true
        onTriggered: {
            retryState.attempts += 1
            if (root.retryScreencast() || retryState.attempts >= 6)
                retryTimer.stop()
        }
    }

    QtObject {
        id: retryState
        property int attempts: 0
    }

    Connections {
        target: preview
        function onAppIdChanged() { root.rebuild() }
        function onVisibleChanged() { if (preview.visible) root.rebuild() }
    }

    // The window list under an open strip changes on its own (window closed
    // from a card, minimized/unminimized, title or active state changed -
    // ApplicationModel emits dataChanged for all of these): rebuild so the
    // closed card vanishes at once and the survivors shrink back and
    // re-center around the icon via setContentSize -> layout.
    Connections {
        target: appModel
        function onDataChanged() { if (preview.visible) root.rebuild() }
        function onRowsInserted() { if (preview.visible) root.rebuild() }
        function onRowsRemoved() { if (preview.visible) root.rebuild() }
    }

    Rectangle {
        id: popup
        visible: windowModel.count > 0
        x: preview.contentX
        y: preview.contentY
        width: windowModel.count > 0
               ? windowModel.count * (root.cellWidth + root.spacing) - root.spacing + 2 * root.padding
               : 0
        height: root.cellHeight + 2 * root.padding
        radius: FishUI.Theme.windowRadius
        color: FishUI.Theme.secondBackgroundColor
        border.color: FishUI.Theme.darkMode ? Qt.rgba(255, 255, 255, 0.3)
                                            : Qt.rgba(0, 0, 0, 0.2)
        border.width: 1

        onWidthChanged:  preview.setContentSize(width, height)
        onHeightChanged: preview.setContentSize(width, height)

        Repeater {
            id: cards
            model: windowModel

            delegate: Item {
                width: root.cellWidth
                height: root.cellHeight
                x: windowIndex * (root.cellWidth + root.spacing) + root.padding
                y: root.padding

                // Re-arm the screencast request when KWin hasn't registered
                // the window yet (nodeId 0). Returns true if a nodeId exists.
                function retryScreencast() {
                    if (screencast.nodeId === 0 && uuid !== undefined && uuid !== "") {
                        console.log("PreviewPopup.retryScreencast: re-arming uuid=", uuid)
                        screencast.uuid = ""
                        screencast.uuid = uuid
                    }
                    return screencast.nodeId !== 0
                }

                TaskManager.ScreencastingRequest {
                    id: screencast
                    uuid: uuid !== undefined ? uuid : ""
                    onNodeIdChanged: console.log("PreviewPopup.nodeId:", screencast.nodeId,
                                                 "for uuid:", screencast.uuid)
                }

                PipeWire.PipeWireSourceItem {
                    id: stream
                    anchors.fill: parent
                    anchors.margins: 4
                    nodeId: screencast.nodeId
                    allowDmaBuf: true
                    // Always painted, even for minimized windows: KWin keeps
                    // serving the last frame, so a minimized card shows a
                    // frozen thumbnail instead of nothing.
                    visible: true
                    onStateChanged: console.log("PreviewPopup.pipewire state:", stream.state,
                                                "ready:", stream.ready,
                                                "nodeId:", stream.nodeId)
                }

                // Fallback only while no stream is up (never a live one for
                // minimized windows, hence frozen frame + dim below). Uses the
                // real app icon, not a generic placeholder.
                FishUI.IconItem {
                    anchors.centerIn: parent
                    width: 48
                    height: 48
                    source: (icon !== undefined && icon !== "") ? icon : "application-x-executable"
                    visible: !stream.ready
                }

                // Minimized dim overlay.
                Rectangle {
                    anchors.fill: parent
                    color: FishUI.Theme.darkMode ? "#80000000" : "#80FFFFFF"
                    radius: 8
                    visible: minimized
                }

                // Active-window highlight, in dock theme accent.
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    radius: 8
                    border.color: FishUI.Theme.highlightColor
                    border.width: 2
                    visible: active === true
                }

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: FishUI.Theme.textColor
                    font: FishUI.Theme.smallFont
                    text: title !== undefined ? title : ""
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: preview.activate(windowIndex)
                }

                // Close button: themed circular button with window-close icon.
                Rectangle {
                    id: closeButton
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    width: 22
                    height: 22
                    radius: width / 2
                    z: 1
                    color: closeArea.containsMouse
                           ? FishUI.Theme.highlightColor
                           : (FishUI.Theme.darkMode ? Qt.rgba(255, 255, 255, 0.15)
                                                   : Qt.rgba(0, 0, 0, 0.15))

                    FishUI.IconItem {
                        anchors.centerIn: parent
                        width: 12
                        height: 12
                        source: "window-close"
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: preview.closeWindow(windowIndex)
                    }
                }
            }
        }
    }
}
