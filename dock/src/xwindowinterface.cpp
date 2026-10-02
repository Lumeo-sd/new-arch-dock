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

#include "xwindowinterface.h"
#include "utils.h"

#include <QDebug>
#include <QTimer>
#include <QWindow>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/plasmawindowmanagement.h>
#include <KWayland/Client/surface.h>

using namespace KWayland::Client;

static XWindowInterface *INSTANCE = nullptr;

XWindowInterface *XWindowInterface::instance()
{
    if (!INSTANCE)
        INSTANCE = new XWindowInterface;

    return INSTANCE;
}

XWindowInterface::XWindowInterface(QObject *parent)
    : QObject(parent)
{
    setupWaylandConnection();
}

void XWindowInterface::setupWaylandConnection()
{
    // Reuse the QtWayland connection instead of opening a second socket to the
    // compositor: fromApplication() returns nullptr off Wayland, which is fine
    // for this port (the dock is a Wayland panel only).
    m_connection = ConnectionThread::fromApplication(this);
    if (!m_connection) {
        qWarning() << "XWindowInterface: not running on Wayland, window tracking disabled.";
        return;
    }

    m_registry = new Registry(this);
    m_registry->create(m_connection);
    // Finalizes the registry setup: without this the global announcements
    // (plasma window management) are never requested from the compositor. The
    // events flow over QtWayland's own connection, so no dedicated EventQueue
    // is attached here (QtWayland only dispatches the default queue).
    m_registry->setup();

    connect(m_registry, &Registry::interfacesAnnounced, this, [this] {
        emit startInitWindows();
    });

    connect(m_registry, &Registry::interfaceAnnounced, this,
            [this](QByteArray interface, quint32 name, quint32 version) {
                if (interface == QByteArrayLiteral("org_kde_plasma_window_management")) {
                    m_management = m_registry->createPlasmaWindowManagement(name, version, this);

                    connect(m_management, &PlasmaWindowManagement::windowCreated,
                            this, &XWindowInterface::onWindowadded);
                    connect(m_management, &PlasmaWindowManagement::activeWindowChanged,
                            this, [this] {
                                PlasmaWindow *window = m_management ? m_management->activeWindow() : nullptr;
                                emit activeChanged(window ? m_ids.value(window, 0) : 0);
                            });
                }
            });
}

WId XWindowInterface::activeWindow()
{
    PlasmaWindow *window = m_management ? m_management->activeWindow() : nullptr;
    return window ? m_ids.value(window, 0) : 0;
}

void XWindowInterface::minimizeWindow(WId win)
{
    PlasmaWindow *window = windowForId(win);
    if (window)
        window->requestToggleMinimized();
}

void XWindowInterface::closeWindow(WId id)
{
    PlasmaWindow *window = windowForId(id);
    if (window)
        window->requestClose();
}

void XWindowInterface::forceActiveWindow(WId win)
{
    PlasmaWindow *window = windowForId(win);
    if (window)
        window->requestActivate();
}

QMap<QString, QVariant> XWindowInterface::requestInfo(quint64 wid)
{
    QMap<QString, QVariant> result;
    PlasmaWindow *window = windowForId(wid);

    if (!window)
        return result;

    const QString appId = window->appId();

    result.insert("iconName", appId.toLower());
    result.insert("active", window->isActive());
    result.insert("visibleName", window->title());
    result.insert("id", appId);

    return result;
}

QString XWindowInterface::requestWindowClass(quint64 wid)
{
    PlasmaWindow *window = windowForId(wid);
    return window ? window->appId() : QString();
}

bool XWindowInterface::isAcceptableWindow(quint64 wid)
{
    PlasmaWindow *window = windowForId(wid);

    if (!window)
        return false;

    // The compositor tells us which windows are meant for the task bar; the
    // X11 version replicated the same list (desktops, docks, menus,
    // notifications and skip-taskbar windows).
    if (window->skipTaskbar())
        return false;

    // Without an appId there is no way to match a desktop file, so the dock
    // would not know what to display.
    if (window->appId().isEmpty())
        return false;

    return true;
}

// Wayland port: reserved screen space is handled by the LayerShellQt exclusive
// zone in MainWindow::updateLayerShell(); the NET WM extended struts these two
// methods used to set are gone.
void XWindowInterface::setViewStruts(QWindow *view, DockSettings::Direction direction, const QRect &rect, bool compositing)
{
    Q_UNUSED(view)
    Q_UNUSED(direction)
    Q_UNUSED(rect)
    Q_UNUSED(compositing)
}

void XWindowInterface::clearViewStruts(QWindow *view)
{
    Q_UNUSED(view)
}

void XWindowInterface::startInitWindows()
{
    // The initial windows are announced through windowCreated right after the
    // registry binds the interface. This is kept as a safety net for the case
    // where the interface appears after the dock finished starting.
    if (!m_management)
        return;

    for (PlasmaWindow *window : m_management->windows())
        onWindowadded(window);
}

QString XWindowInterface::desktopFilePath(quint64 wid)
{
    PlasmaWindow *window = windowForId(wid);

    if (!window)
        return QString();

    const QString appId = window->appId();
    // On Wayland the appId replaces the WM_CLASS pair the X11 code passed in.
    return Utils::instance()->desktopPathFromMetadata(appId, window->pid(), appId);
}

void XWindowInterface::setIconGeometry(quint64 wid, const QRect &rect)
{
    PlasmaWindow *window = windowForId(wid);

    if (!window || !m_panelSurface)
        return;

    window->setMinimizedGeometry(m_panelSurface, rect);
}

QList<quint64> XWindowInterface::windows() const
{
    return m_windows.keys();
}

bool XWindowInterface::isWindowMaximized(quint64 wid) const
{
    PlasmaWindow *window = windowForId(wid);
    return window && window->isMaximized();
}

bool XWindowInterface::isWindowMinimized(quint64 wid) const
{
    PlasmaWindow *window = windowForId(wid);
    return window && window->isMinimized();
}

bool XWindowInterface::isWindowSkipTaskbar(quint64 wid) const
{
    PlasmaWindow *window = windowForId(wid);
    return window && window->skipTaskbar();
}

QString XWindowInterface::activeWindowClass() const
{
    PlasmaWindow *window = m_management ? m_management->activeWindow() : nullptr;
    return window ? window->appId() : QString();
}

void XWindowInterface::setPanelWindow(QWindow *window)
{
    m_panelWindow = window;
    m_panelSurface = window ? Surface::fromWindow(window) : nullptr;
}

void XWindowInterface::onWindowadded(PlasmaWindow *window)
{
    if (m_ids.contains(window))
        return;

    const quint64 wid = m_nextId++;
    m_windows.insert(wid, window);
    m_ids.insert(window, wid);

    connect(window, &PlasmaWindow::unmapped, this, [this, window, wid] {
        onWindowRemoved(window, wid);
    });

    // The dock only ever redraws its buttons from these states.
    connect(window, &PlasmaWindow::activeChanged, this, &XWindowInterface::windowStateChanged);
    connect(window, &PlasmaWindow::minimizedChanged, this, &XWindowInterface::windowStateChanged);
    connect(window, &PlasmaWindow::maximizedChanged, this, &XWindowInterface::windowStateChanged);
    connect(window, &PlasmaWindow::titleChanged, this, &XWindowInterface::windowStateChanged);
    connect(window, &PlasmaWindow::appIdChanged, this, &XWindowInterface::windowStateChanged);

    if (isAcceptableWindow(wid))
        emit windowAdded(wid);

    emit windowStateChanged();
}

void XWindowInterface::onWindowRemoved(PlasmaWindow *window, quint64 wid)
{
    m_windows.remove(wid);
    m_ids.remove(window);

    emit windowRemoved(wid);
    emit windowStateChanged();
}

KWayland::Client::PlasmaWindow *XWindowInterface::windowForId(quint64 wid) const
{
    return m_windows.value(wid, nullptr);
}
