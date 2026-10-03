/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     rekols <revenmartin@gmail.com>
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
import QtQuick.Layouts 1.12
import Qt5Compat.GraphicalEffects

import Cutefish.Dock 1.0
import FishUI 1.0 as FishUI

Item {
    id: root
    visible: true

    property bool isHorizontal: Settings.direction === DockSettings.Bottom
    property real windowRadius: isHorizontal ? root.height * 0.3 : root.width * 0.3
    property bool compositing: windowHelper.compositing

    // True while an external drag (a .desktop file from a launcher) hovers the
    // dock. The app icons' own drop areas are disabled during it, so the root
    // DropArea below can follow the cursor and show the insertion slot.
    property bool externalDragActive: false

    // Reorders use the standard Qt Quick drag (see DockItem.qml): the dragged
    // row's icon hides and Qt's drag pixmap — rendered by the compositor on
    // Wayland — follows the cursor everywhere. Rows reorder on drop
    // (AppItem's 300 ms hover timer), so the model changes once per gesture.
    // No band, no ghost, no live gap, no collapse: nothing else animates
    // while the button is held, which is what keeps the dock free of
    // artifacts during fast movement.

    // Wayland auto-hide: the panel shrinks to a thin edge strip and the QML
    // layer fades out underneath the mouse cursor.
    opacity: mainWindow.dockHidden ? 0 : 1

    Behavior on opacity {
        NumberAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }
    }

    onCompositingChanged: {
        mainWindow.updateSize()
    }

    // External drags (a .desktop file from a launcher): the insertion slot
    // sits at the absolute cell under the cursor (floor). Index 0 (the
    // launcher) is protected on the left; the slot may go up to the trash.
    function gapCell(pos, maxIndex) {
        const itemSize = isHorizontal ? appItemView.height : appItemView.width
        if (itemSize <= 0 || appItemView.count === 0)
            return 1

        const res = Math.max(1, Math.min(Math.floor(pos / itemSize), maxIndex))
        return res
    }

    function gapIndex(pos) {
        return gapCell(pos, appItemView.count)
    }

    // Pin apps dragged in from a launcher (Kickoff, the Cutefish launcher,
    // a file manager) by dropping their .desktop file onto the dock.
    // While the drag hovers, a temporary drop slot is shown at the cursor
    // position so the existing icons slide apart (leaving a gap) and the
    // drop can be aimed at exactly the wanted spot. The panel grows by one
    // cell while the slot is open so nothing slides behind the trash.
    DropArea {
        id: rootDropArea
        anchors.fill: parent
        enabled: true

        onEntered: function(drag) {
            if (root.externalDragActive)
                return

            if (drag.urls.length
                    && drag.urls[0].toString().toLowerCase().endsWith(".desktop")) {
                root.externalDragActive = true
                appModel.beginDropSlot(root.gapIndex(isHorizontal ? drag.x : drag.y))
                mainWindow.resizeToContent(0)
            }
        }

        onPositionChanged: function(drag) {
            if (!root.externalDragActive)
                return

            if (drag.urls.length
                    && drag.urls[0].toString().toLowerCase().endsWith(".desktop")) {
                appModel.moveDropSlot(root.gapIndex(isHorizontal ? drag.x : drag.y))
            }
        }

        onExited: function(drag) {
            if (!root.externalDragActive)
                return

            root.externalDragActive = false

            if (appModel.endDropSlot())
                mainWindow.resizeToContent(0)
        }

        onDropped: function(drop) {
            root.externalDragActive = false

            if (drop.hasUrls) {
                const idx = root.gapIndex(isHorizontal ? drop.x : drop.y)
                let first = true

                for (let i = 0; i < drop.urls.length; ++i) {
                    const url = drop.urls[i].toString()
                    if (!url.toLowerCase().endsWith(".desktop"))
                        continue

                    if (first) {
                        mainWindow.addDesktopFileAt(url, idx)
                        first = false
                    } else {
                        mainWindow.addDesktopFile(url)
                    }
                }
            }

            // The drop slot is converted into the pinned app in place, so
            // endDropSlot() is a no-op and the panel keeps its grown size;
            // otherwise (none of the dropped URLs was usable) shrink back.
            if (appModel.endDropSlot())
                mainWindow.resizeToContent(0)
        }
    }

    // Background
    Rectangle {
        id: _background

        property var borderColor: root.compositing ? FishUI.Theme.darkMode ? Qt.rgba(255, 255, 255, 0.3)
                                                                           : Qt.rgba(0, 0, 0, 0.2) : FishUI.Theme.darkMode ? Qt.rgba(255, 255, 255, 0.15)
                                                                                                                         : Qt.rgba(0, 0, 0, 0.15)

        anchors.fill: parent
        radius: root.compositing && Settings.style === 0 ? windowRadius : 0
        color: FishUI.Theme.darkMode ? "#666666" : "#E6E6E6"
        opacity: root.compositing ? FishUI.Theme.darkMode ? 0.5 : 0.5 : 0.9
        border.width: 1 / FishUI.Units.devicePixelRatio
        border.pixelAligned: FishUI.Units.devicePixelRatio > 1 ? false : true
        border.color: borderColor

        Behavior on color {
            ColorAnimation {
                duration: 200
                easing.type: Easing.Linear
            }
        }
    }

    GridLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.topMargin: Settings.style === 1
                           && (Settings.direction === 0 || Settings.direction === 2)
                           ? 28 : 0
        flow: isHorizontal ? Grid.LeftToRight : Grid.TopToBottom
        columnSpacing: 0
        rowSpacing: 0

        ListView {
            id: appItemView
            orientation: isHorizontal ? Qt.Horizontal : Qt.Vertical
            snapMode: ListView.SnapToItem
            interactive: false
            model: appModel
            clip: true

            Layout.fillHeight: true
            Layout.fillWidth: true

            delegate: AppItem {
                // Fixed full-cell size for every row — the ListView layout
                // never re-flows during a drag, so the icons only ever
                // move through moveDisplaced (once per reorder drop).
                implicitWidth: isHorizontal ? appItemView.height : appItemView.width
                implicitHeight: isHorizontal ? appItemView.height : appItemView.width
            }

            moveDisplaced: Transition {
                NumberAnimation {
                    properties: "x, y"
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
            }

            // The drop slot insertion (beginDropSlot) and removal
            // (endDropSlot) displace the surrounding icons: animate those
            // too, so the icons slide apart / close smoothly instead of
            // jumping.
            addDisplaced: Transition {
                NumberAnimation {
                    properties: "x, y"
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
            }

            removeDisplaced: Transition {
                NumberAnimation {
                    properties: "x, y"
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
            }
        }

        // Activities overview. Plasma offers no API for it, so this sends the
        // shortcut bound to [kwin] Overview - see OverviewManager. Sits before
        // the trash so the trash stays the last cell.
        DockItem {
            id: overviewItem
            implicitWidth: isHorizontal ? root.height : root.width
            implicitHeight: isHorizontal ? root.height : root.width
            popupText: overview.status
            enableActivateDot: false
            // "computer" rather than "view-grid": the latter ships only in the
            // 16/22/24 breeze actions, and at a 53 px cell Qt looks for a size
            // that is not there, which renders an empty cell.
            iconName: "computer"
            onClicked: overview.showOverview()
        }

        DockItem {
            id: trashItem
            // The trash cell is as thick as the dock strip (= one window cell).
            implicitWidth: isHorizontal ? root.height : root.width
            implicitHeight: isHorizontal ? root.height : root.width
            popupText: qsTr("Trash")
            enableActivateDot: false
            iconName: trash.count === 0 ? "user-trash-empty" : "user-trash-full"
            onClicked: trash.openTrash()
            onRightClicked: trashMenu.popup()

            dropArea.enabled: !root.externalDragActive

            onDropped: {
                if (drop.hasUrls) {
                    trash.moveToTrash(drop.urls)
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: FishUI.Units.smallSpacing / 2
                color: "transparent"
                border.color: FishUI.Theme.textColor
                radius: height * 0.3
                border.width: 1 / FishUI.Units.devicePixelRatio
                border.pixelAligned: FishUI.Units.devicePixelRatio > 1 ? false : true
                opacity: trashItem.dropArea.containsDrag ? 0.5 : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 200
                    }
                }
            }

            FishUI.DesktopMenu {
                id: trashMenu

                MenuItem {
                    text: qsTr("Open")
                    onTriggered: trash.openTrash()
                }

                MenuItem {
                    text: qsTr("Empty Trash")
                    onTriggered: trash.emptyTrash()
                    visible: trash.count !== 0
                }
            }
        }
    }

    FishUI.WindowHelper {
        id: windowHelper
    }

    FishUI.WindowShadow {
        view: mainWindow
        geometry: Qt.rect(root.x, root.y, root.width, root.height)
        strength: 1
        radius: _background.radius
    }

    FishUI.WindowBlur {
        view: mainWindow
        geometry: Qt.rect(root.x, root.y, root.width, root.height)
        windowRadius: _background.radius
        enabled: true
    }

    FishUI.PopupTips {
        id: popupTips
        // Use the real surface colour rather than the panel colour: the panel
        // is a translucent mid grey, and white text on it lands around 4:1 -
        // too low to read the label.
        backgroundColor: FishUI.Theme.secondBackgroundColor
        blurEnabled: false
    }

    Connections {
        target: Settings

        function onDirectionChanged() {
            popupTips.hide()
        }
    }

    Connections {
        target: mainWindow

        function onVisibleChanged() {
            popupTips.hide()
        }
    }
}
