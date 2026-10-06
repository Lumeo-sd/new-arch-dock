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
import QtQuick.Layouts 1.12
import FishUI 1.0 as FishUI

import Cutefish.Launcher 1.0

ListView {
    id: control

    property bool searchMode: false

    property var sourceModel: launcherModel
    property var modelCount: sourceModel.count

    property int iconSize: root.iconSize + FishUI.Units.largeSpacing * 2
    property int cellWidth: iconSize + calcExtraSpacing(iconSize, control.width)
    property int cellHeight: iconSize + calcExtraSpacing(iconSize, control.height)

    // The view can be laid out once with a zero size while the window is
    // being created. Keep the page model valid during that first pass.
    property int rows: Math.max(1, Math.floor(control.width / control.cellWidth))
    property int columns: Math.max(1, Math.floor(control.height / control.cellHeight))
    property int pageCount: Math.max(1, control.rows * control.columns)

    orientation: ListView.Horizontal
    model: control.modelCount > 0 ? Math.ceil(control.modelCount / control.pageCount) : 0

    // The launcher is a strict one-page-at-a-time pager. The Flickable's
    // native drag/snap/wheel handling is disabled (interactive: false) and
    // contentX is bound straight to currentIndex; wheel/trackpad input is
    // steered by the foreground MouseArea (z:10) which maps every gesture to
    // exactly one page step. This kills the two native-list pathologies:
    // a small swipe no longer snaps back ("sometimes does nothing") and a
    // strong one can no longer overshoot into the empty space past the last
    // page and wrap around to the first ("cycles in a circle").

    // Keep one extra page ready without tying the cache size to the model
    // count, which can trigger a binding loop while the view is resizing.
    cacheBuffer: control.width
    boundsBehavior: Flickable.StopAtBounds
    interactive: false
    // The page lock drives contentX directly; keep the ListView from also
    // repositioning the content whenever currentIndex changes (the default
    // highlight-follows-current-item would snap the viewport to the new
    // page instantly and fight the Behavior below -- visible on backward
    // flips as an abrupt jump instead of the smooth slide).
    highlightFollowsCurrentItem: false
    currentIndex: -1
    clip: true

    // Page lock: the viewport always shows exactly the currentIndex item.
    contentX: Math.max(0, control.currentIndex) * control.width

    Behavior on contentX {
        NumberAnimation {
            duration: 300
            easing.type: Easing.InOutQuad
        }
    }

    // When the app list loads asynchronously and the page count goes from 0
    // to N, the ListView has no delegates yet. Jump to the first page so the
    // GridView is populated immediately (otherwise icons only appear after the
    // user scrolls, and a visual artifact from the empty initial layout
    // persists). Keep currentIndex inside [0, count-1] when the model changes
    // so the page lock never points past the last page.
    onCountChanged: {
        if (count > 0 && currentIndex < 0)
            currentIndex = 0
        else if (count > 0 && currentIndex > count - 1)
            currentIndex = count - 1
    }

    DropArea {
        anchors.fill: parent
        z: -1
    }

    // Blank-area click dismisses the launcher. Wheel input is handled by the
    // foreground wheel area below.
    MouseArea {
        anchors.fill: parent
        z: -1

        onClicked: {
            launcher.hideWindow()
        }
    }

    // Foreground wheel interceptor: sits above every delegate (z:10) and
    // consumes wheel events before the Flickable can scroll with them,
    // mapping each gesture to exactly one page step. Qt.NoButton means
    // clicks are not blocked and pass through to the icons underneath.
    MouseArea {
        anchors.fill: parent
        z: 10
        acceptedButtons: Qt.NoButton

        onWheel: function(wheel) {
            wheel.accepted = true

            // Smooth-scroll sources (touchpads, high-res wheels) make Qt emit
            // wheel events with zero deltas -- sub-pixel frames, settle and
            // stop transitions. They carry no direction, so ignore them
            // WITHOUT touching the burst timer: the first real delta of a
            // gesture must be the one that (possibly) steps a page, not a
            // zero event that would start the timer and swallow the gesture.
            var dx = wheel.angleDelta.x
            var dy = wheel.angleDelta.y

            // Dead zone: ignore micro-deltas (touchpad jitter at rest, tiny
            // finger movements) so a barely-moved finger cannot flip a page.
            // A real swipe produces per-event deltas well above this (the
            // touchpad averages ~12 angleDelta units per pixel), jitter stays
            // below it.
            if (dx === 0 && dy === 0)
                return
            if (Math.abs(dx) < 90 && Math.abs(dy) < 90)
                return

            // A mechanical mouse wheel reports one discrete step per detent
            // as an angle delta aligned to the 120-unit notch and usually no
            // pixel delta. Every notch must flip a page -- even when the wheel
            // is spun quickly -- so notches bypass the gesture burst timer
            // below. The touchpad's smooth scroll synthesizes small,
            // non-aligned deltas (~12 units per pixel), so it never matches
            // here and keeps burst suppression: one whole two-finger gesture
            // maps to a single page.
            var notch = (Math.abs(dx) >= 120 && dx % 120 === 0)
                     || (Math.abs(dy) >= 120 && dy % 120 === 0)

            // A trackpad swipe arrives as a burst of wheel events; the timer
            // swallows the rest of the burst so one gesture steps at most
            // one page. The window is extended on every event (restart) so a
            // long burst -- a strong or slow swipe -- still counts as a
            // single gesture. Mechanical wheel notches bypass this check
            // entirely (see above) and still step one page per detent.
            if (!notch && steerTimer.running) {
                steerTimer.restart()
                return
            }

            // Pick the dominant axis for the direction. Trackpads commonly
            // report both deltas for a diagonal gesture. Sign conventions:
            // a touchpad with natural scrolling reports a leftward two-finger
            // swipe as a positive horizontal delta, and flat/vertical scroll
            // (wheel down / two-finger up) as a negative vertical delta; both
            // mean "next page".
            var forward = false
            if (Math.abs(wheel.angleDelta.y) >= Math.abs(wheel.angleDelta.x)) {
                forward = (wheel.angleDelta.y < 0)
            } else {
                forward = (wheel.angleDelta.x > 0)
            }

            if (forward)
                scrollNextPage()
            else
                scrollPreviousPage()

            steerTimer.start()
        }

        Timer {
            id: steerTimer
            interval: 500
            repeat: false
        }
    }

    delegate: GridView {
        id: _page

        width: control.width
        height: control.height

        readonly property int pageIndex: index

        cellHeight: control.cellHeight
        cellWidth: control.cellWidth

        interactive: false

        moveDisplaced: Transition {
            NumberAnimation {
                properties: "x, y"
                duration: 300
                easing.type: Easing.InOutQuad
            }
        }

        model: PageModel {
            id: _pageModel
            sourceModel: launcherModel
            startIndex: control.pageCount * _page.pageIndex
            limitCount: control.pageCount
        }

        delegate: GridItemDelegate {
            searchMode: control.searchMode
            pageIndex: _page.pageIndex
            pageCount: control.pageCount
            width: control.cellWidth
            height: control.cellHeight
        }
    }

    function calcExtraSpacing(cellSize, containerSize) {
        var availableColumns = Math.floor(containerSize / cellSize)
        var extraSpacing = 0
        if (availableColumns > 0) {
            var allColumnSize = availableColumns * cellSize
            var extraSpace = Math.max(containerSize - allColumnSize, 0)
            extraSpacing = extraSpace / availableColumns
        }
        return Math.floor(extraSpacing)
    }

    function scrollNextPage() {
        if (currentIndex < count - 1)
            currentIndex++
    }

    function scrollPreviousPage() {
        if (currentIndex > 0)
            currentIndex--
    }
}
