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

#ifndef XWINDOWINTERFACE_H
#define XWINDOWINTERFACE_H

#include "applicationitem.h"
#include "docksettings.h"
#include <QObject>
#include <QHash>
#include <QPointer>

namespace KWayland
{
namespace Client
{
class ConnectionThread;
class Registry;
class PlasmaWindowManagement;
class PlasmaWindow;
class Surface;
}
}

#include <QWindow>

/**
 * Window tracking for the dock.
 *
 * The original implementation polled the X11 root window through KX11Extras /
 * NETWM. On Wayland that information comes from the compositor through the
 * org_kde_plasma_window_management protocol, which KWin exposes for task
 * managers. The WId values handed to the rest of the dock are no longer X
 * window ids: they are internal sequential ids assigned to every announced
 * PlasmaWindow, so ApplicationModel and the QML layer do not have to change.
 */
class XWindowInterface : public QObject
{
    Q_OBJECT

public:
    static XWindowInterface *instance();
    explicit XWindowInterface(QObject *parent = nullptr);

    WId activeWindow();
    void minimizeWindow(WId win);
    void closeWindow(WId id);
    void forceActiveWindow(WId win);

    QMap<QString, QVariant> requestInfo(quint64 wid);
    QString requestWindowClass(quint64 wid);
    bool isAcceptableWindow(quint64 wid);

    // No-ops on Wayland: the screen-edge space reservation is handled by the
    // LayerShellQt exclusive zone in MainWindow::updateLayerShell().
    void setViewStruts(QWindow *view, DockSettings::Direction direction, const QRect &rect, bool compositing = false);
    void clearViewStruts(QWindow *view);

    void startInitWindows();

    QString desktopFilePath(quint64 wid);

    void setIconGeometry(quint64 wid, const QRect &rect);

    // Wayland port additions, used by Activity.
    QList<quint64> windows() const;
    bool isWindowMaximized(quint64 wid) const;
    bool isWindowMinimized(quint64 wid) const;
    bool isWindowSkipTaskbar(quint64 wid) const;
    QString activeWindowClass() const;

    // The dock's own surface is the anchor for every taskbar entry: the
    // compositor shows the task switcher popup on top of it.
    void setPanelWindow(QWindow *window);

signals:
    void windowAdded(quint64 wid);
    void windowRemoved(quint64 wid);
    void activeChanged(quint64 wid);
    // Any window property the dock reacts to (active/minimized/maximized,
    // title, appId, added or removed) changed.
    void windowStateChanged();

private:
    void setupWaylandConnection();
    void onWindowadded(KWayland::Client::PlasmaWindow *window);
    void onWindowRemoved(KWayland::Client::PlasmaWindow *window, quint64 wid);
    KWayland::Client::PlasmaWindow *windowForId(quint64 wid) const;

    quint64 m_nextId = 1;
    QHash<quint64, KWayland::Client::PlasmaWindow *> m_windows;
    QHash<KWayland::Client::PlasmaWindow *, quint64> m_ids;

    KWayland::Client::ConnectionThread *m_connection = nullptr;
    KWayland::Client::Registry *m_registry = nullptr;
    KWayland::Client::PlasmaWindowManagement *m_management = nullptr;

    QPointer<QWindow> m_panelWindow;
    KWayland::Client::Surface *m_panelSurface = nullptr;
};

#endif // XWINDOWINTERFACE_H
