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

#include "processprovider.h"
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QProcess>

ProcessProvider::ProcessProvider(QObject *parent)
    : QObject(parent)
{

}

bool ProcessProvider::startDetached(const QString &exec, QStringList args)
{
    // CutefishOS used its session daemon to launch applications. That service
    // does not exist on Plasma, so fall back to starting the process directly.
    QDBusInterface iface("com.cutefish.Session",
                         "/Session",
                         "com.cutefish.Session", QDBusConnection::sessionBus());

    if (iface.isValid()) {
        // Non-blocking: the GUI thread must not wait on a D-Bus reply.
        iface.asyncCall("launch", exec, args);
        return true;
    }

    return QProcess::startDetached(exec, args);
}
