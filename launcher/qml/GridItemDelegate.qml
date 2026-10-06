/*
 * Copyright (C) 2021 CutefishOS.
 *
 * Author:     Reoin Wong <reion@cutefishos.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.12
import QtQuick.Controls 2.12
import Qt5Compat.GraphicalEffects
import QtQuick.Window 2.12
import QtQuick.Layouts 1.12
import FishUI 1.0 as FishUI
import Cutefish.Launcher 1.0

Item {
    id: control

    property bool searchMode: false
    property bool dragStarted: false
    property var dragItemIndex: index
    property var dragSource: null

    property int pageIndex: 0
    property int pageCount: 0

    Drag.active: iconMouseArea.drag.active
    Drag.mimeData: [model.appId]
    Drag.keys: ["cutefish-launcher"]
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.MoveAction
    Drag.hotSpot.x: icon.width / 2
    Drag.hotSpot.y: icon.height / 2

    Drag.onDragStarted:  {
        dragStarted = true
    }

    Drag.onDragFinished: {
        dragStarted = false
    }

    DropArea {
        anchors.fill: icon
        enabled: true

        onContainsDragChanged: {
            if (control.searchMode)
                return

            if (drag.source) {
                control.dragSource = drag.source
                dragTimer.restart()
            } else {
                control.dragSource = null
                dragTimer.stop()
            }
        }
    }

    Timer {
        id: dragTimer
        interval: 300
        onTriggered: {
            if (control.dragSource) {
                launcherModel.move(control.dragSource.dragItemIndex,
                                   control.dragItemIndex,
                                   control.pageIndex,
                                   control.pageCount)
                _pageModel.move(control.dragSource.dragItemIndex,
                                control.dragItemIndex)
            }
        }
    }

    IconItem {
        id: icon

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: label.top
        anchors.margins: FishUI.Units.largeSpacing * 2

        width: height
        height: width

        source: model.iconName
        visible: !dragStarted

        ColorOverlay {
            id: colorOverlay
            anchors.fill: icon
            source: icon
            color: "#000000"
            opacity: 0.5
            visible: iconMouseArea.pressed
        }
    }

    // Defer the DesktopMenu (MenuPopupWindow = Wayland xdg-popup) until
    // the first right-click.  Creating the popup per-delegate eagerly at
    // startup stalls the first composited frame for tens of seconds
    // because each MenuPopupWindow allocates a separate Wayland surface
    // and scene-graph context.  With a Loader the surface is created at
    // most once per delegate, only on demand.
    Loader {
        id: menuLoader
        active: false
        sourceComponent: Component {
            FishUI.DesktopMenu {
                // Guard onActiveChanged: on Wayland an xdg-popup steals keyboard
                // focus from the launcher, which would otherwise hide it before
                // the menu can appear. DesktopMenu is a MenuPopupWindow: the
                // standard Popup signals are not exposed, use visibility instead.
                onVisibleChanged: launcher.setContextMenuOpen(visible)

                MenuItem {
                    text: qsTr("Open")
                    onTriggered: launcherModel.launch(model.appId)
                }

                MenuItem {
                    id: sendToDock
                    text: qsTr("Send to dock")
                    onTriggered: launcherModel.sendToDock(model.appId)
                }

                MenuItem {
                    id: sendToDesktop
                    text: qsTr("Send to desktop")
                    onTriggered: launcherModel.sendToDesktop(model.appId)
                }

                MenuItem {
                    id: removeFromDock
                    text: qsTr("Remove from dock")
                    onTriggered: launcherModel.removeFromDock(model.appId)
                }

                MenuItem {
                    id: uninstallItem
                    text: qsTr("Uninstall")
                    onTriggered: {
                        root.uninstallDialog.desktopPath = model.appId
                        root.uninstallDialog.appName = model.name
                        root.uninstallDialog.visible = true
                    }
                }

                function updateActions() {
                    sendToDock.visible = launcher.dockAvailable() && !launcher.isPinedDock(model.appId)
                    removeFromDock.visible = launcher.dockAvailable() && launcher.isPinedDock(model.appId)
                }

                // Called from the delegate before popup(). Must run inside this
                // component's scope: ids declared in a Component are not
                // exposed on the loaded item (Loader.item.uninstallItem is
                // undefined), so the delegate cannot touch them directly.
                function prepare() {
                    uninstallItem.visible = appManager.isCutefishOS()
                    updateActions()
                }
            }
        }
    }

    MouseArea {
        id: iconMouseArea
        anchors.fill: icon
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        drag.axis: Drag.XAndYAxis

        onClicked: {
            if (mouse.button == Qt.LeftButton)
                launcherModel.launch(model.appId)
            else if (mouse.button == Qt.RightButton) {
                menuLoader.active = true
                var m = menuLoader.item
                if (!m)
                    return
                m.prepare()
                m.popup()
            }
        }

        onPositionChanged: {
            if (pressed) {
                if (mouse.source !== Qt.MouseEventSynthesizedByQt) {
                    drag.target = icon
                    icon.grabToImage(function(result) {
                        control.Drag.imageSource = result.url
                    })
                } else {
                    drag.target = null
                }
            }
        }

        onReleased: {
            drag.target = null
        }
    }

    TextMetrics {
        id: fontMetrics
        font.family: label.font.family
        text: label.text
    }

    Rectangle {
        id: newInstallPoint

        width: 6
        height: 6

        visible: !dragStarted && model.newInstalled

        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: label.top
            bottomMargin: FishUI.Units.smallSpacing
        }

        color: FishUI.Theme.highlightColor
        radius: height / 2
    }

    Label {
        id: label

        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            margins: FishUI.Units.smallSpacing
        }

        visible: !dragStarted

        text: model.name
        elide: Text.ElideRight
        textFormat: Text.PlainText
        maximumLineCount: 2
        wrapMode: "WordWrap"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        width: parent.width - 2 * FishUI.Units.smallSpacing
        height: fontMetrics.height * 2
        color: "white"

        MouseArea {
            anchors.fill: parent
        }
    }
}
