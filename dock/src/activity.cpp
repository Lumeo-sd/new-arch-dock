/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     Reion Wong <reionwong@gmail.com>
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

#include "activity.h"
#include "docksettings.h"
#include "xwindowinterface.h"

#include <QDebug>

static Activity *SELF = nullptr;

Activity *Activity::self()
{
    if (!SELF)
        SELF = new Activity;

    return SELF;
}

Activity::Activity(QObject *parent)
    : QObject(parent)
    , m_existsWindowMaximized(false)
    , m_launchPad(false)
{
    onActiveWindowChanged();

    XWindowInterface *iface = XWindowInterface::instance();
    connect(iface, &XWindowInterface::windowStateChanged, this, &Activity::onActiveWindowChanged);
    connect(iface, &XWindowInterface::activeChanged, this, &Activity::onActiveWindowChanged);

    // Switching AlwaysShow -> IntellHide has to re-evaluate the maximized
    // state right away, not on the next window event.
    connect(DockSettings::self(), &DockSettings::visibilityChanged,
            this, &Activity::onActiveWindowChanged);
}

bool Activity::existsWindowMaximized() const
{
    return m_existsWindowMaximized;
}

bool Activity::launchPad() const
{
    return m_launchPad;
}

void Activity::onActiveWindowChanged()
{
    XWindowInterface *iface = XWindowInterface::instance();

    // On Wayland the window class is the appId reported by the compositor.
    m_windowClass = iface->activeWindowClass();
    bool launchPad = m_windowClass == "cutefish-launcher";

    // Always recompute, also when IntellHide is off: the flag has to fall back
    // to false so the mode starts from a clean state when it is enabled later.
    bool existsWindowMaximized = false;

    if (DockSettings::self()->visibility() == DockSettings::IntellHide) {
        for (quint64 wid : iface->windows()) {
            if (iface->isWindowMinimized(wid) || iface->isWindowSkipTaskbar(wid))
                continue;

            if (iface->isWindowMaximized(wid)) {
                existsWindowMaximized = true;
                break;
            }
        }
    }

    if (m_existsWindowMaximized != existsWindowMaximized) {
        m_existsWindowMaximized = existsWindowMaximized;
        emit existsWindowMaximizedChanged();
    }

    if (m_launchPad != launchPad) {
        m_launchPad = launchPad;
        emit launchPadChanged();
    }
}
