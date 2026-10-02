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

#include "utils.h"
#include "systemappmonitor.h"
#include "systemappitem.h"

#include <QFile>
#include <QFileInfo>
#include <QUrlQuery>
#include <QSettings>
#include <QDebug>

static Utils *INSTANCE = nullptr;

Utils *Utils::instance()
{
    if (!INSTANCE)
        INSTANCE = new Utils;

    return INSTANCE;
}

Utils::Utils(QObject *parent)
    : QObject(parent)
    , m_sysAppMonitor(SystemAppMonitor::self())
{

}

QStringList Utils::commandFromPid(quint32 pid)
{
    QFile file(QString("/proc/%1/cmdline").arg(pid));

    if (file.open(QIODevice::ReadOnly)) {
        QByteArray cmd = file.readAll();

        // ref: https://github.com/KDE/kcoreaddons/blob/230c98aa7e01f9e36a9c2776f3633182e6778002/src/lib/util/kprocesslist_unix.cpp#L137
        if (!cmd.isEmpty()) {
            // extract non-truncated name from cmdline
            int zeroIndex = cmd.indexOf('\0');
            int processNameStart = cmd.lastIndexOf('/', zeroIndex);
            if (processNameStart == -1) {
                processNameStart = 0;
            } else {
                processNameStart++;
            }

            QString name = QString::fromLocal8Bit(cmd.mid(processNameStart, zeroIndex - processNameStart));

            // reion: Remove parameters
            name = name.split(' ').first();

            cmd.replace('\0', ' ');
            QString command = QString::fromLocal8Bit(cmd).trimmed();

            // There may be parameters.
            if (command.split(' ').size() > 1) {
                command = command.split(' ').first();
            }

            return { command, name };
        }
    }

    return QStringList();
}

QString Utils::desktopPathFromMetadata(const QString &appId, quint32 pid, const QString &xWindowWMClassName)
{
    // Stage 1: match by the (Wayland) appId first. This does not depend on the
    // compositor-reported pid, which under Wayland can point at a portal or
    // launcher process instead of the app itself and therefore made the old
    // /proc based lookup fail for perfectly ordinary applications.
    if (!appId.isEmpty()) {
        for (SystemAppItem *item : m_sysAppMonitor->applications()) {
            const QFileInfo desktopFileInfo(item->path);

            // Qt 6: completeBaseName() keeps the pre-Qt6 baseName() semantics,
            // i.e. "org.kde.dolphin" for "org.kde.dolphin.desktop".
            if (desktopFileInfo.completeBaseName().compare(appId, Qt::CaseInsensitive) == 0)
                return item->path;

            // StartupWMClass=STRING is the canonical Wayland appId hint.
            if (item->startupWMClass.compare(appId, Qt::CaseInsensitive) == 0)
                return item->path;

            if (item->iconName.compare(appId, Qt::CaseInsensitive) == 0)
                return item->path;
        }
    }

    // Stage 2: legacy pid/cmdline based matching.
    QStringList commands = commandFromPid(pid);

    // The value returned from the commandFromPid() may be empty.
    // Calling first() and last() below will cause the statusbar to crash.
    if (commands.isEmpty() || xWindowWMClassName.isEmpty())
        return "";

    QString command = commands.first();
    QString commandName = commands.last();

    if (command.isEmpty())
        return "";

    QString result;

    if (!appId.isEmpty() && !xWindowWMClassName.isEmpty()) {
        for (SystemAppItem *item : m_sysAppMonitor->applications()) {
            // Start search.
            const QFileInfo desktopFileInfo(item->path);

            bool isExecPath = QFile::exists(item->exec);
            bool founded = false;

            if (item->exec == command || item->exec == commandName) {
                founded = true;
            }

            // StartupWMClass=STRING
            // If true, it is KNOWN that the application will map at least one
            // window with the given string as its WM class or WM name hint.
            // ref: https://specifications.freedesktop.org/startup-notification-spec/startup-notification-0.1.txt
            if (item->startupWMClass.startsWith(appId, Qt::CaseInsensitive) ||
                item->startupWMClass.startsWith(xWindowWMClassName, Qt::CaseInsensitive))
                founded = true;

            if (!founded && item->iconName.startsWith(xWindowWMClassName, Qt::CaseInsensitive))
                founded = true;

            // Icon name and cmdline.
            if (!founded && (item->iconName == command || item->iconName == commandName))
                founded = true;

            // Exec name and cmdline.
            if (!founded && (item->exec == command || item->exec == commandName))
                founded = true;

            // Try matching mapped name against 'Name'.
            if (!founded && item->name.startsWith(xWindowWMClassName, Qt::CaseInsensitive))
                founded = true;

            // exec
            if (!founded && item->exec.startsWith(xWindowWMClassName, Qt::CaseInsensitive))
                founded = true;

            if (!founded && desktopFileInfo.completeBaseName().startsWith(xWindowWMClassName, Qt::CaseInsensitive))
                founded = true;

            // For exec path.
            if (isExecPath && !founded && (command.contains(item->exec) || commandName.contains(item->exec))) {
                founded = true;
            }

            if (founded) {
                result = item->path;
                break;
            }
        }
    }

    // Stage 3: last resort - match the appId against the remaining metadata.
    // Some desktop files carry a display name that differs from the appId
    // while the executable line still identifies them.
    if (result.isEmpty() && !appId.isEmpty()) {
        for (SystemAppItem *item : m_sysAppMonitor->applications()) {
            if (item->name.compare(appId, Qt::CaseInsensitive) == 0) {
                result = item->path;
                break;
            }

            const QString execBase = QFileInfo(item->exec).completeBaseName();
            if (!execBase.isEmpty() && execBase.compare(appId, Qt::CaseInsensitive) == 0) {
                result = item->path;
                break;
            }
        }
    }

    return result;
}

QMap<QString, QString> Utils::readInfoFromDesktop(const QString &desktopFile)
{
    QMap<QString, QString> info;
    for (SystemAppItem *item : m_sysAppMonitor->applications()) {
        if (item->path == desktopFile) {
            info.insert("Icon", item->iconName);
            info.insert("Name", item->name);
            info.insert("Exec", item->exec);
            return info;
        }
    }

    // The requested path may differ from the monitor's canonical one (a
    // kickoff drag URL can point at e.g. a flatpak app-install path while the
    // monitor knows the XDG export link). Parse the desktop file directly so a
    // freshly dropped pin still gets its real icon and name.
    if (!desktopFile.isEmpty() && QFile::exists(desktopFile)) {
        QSettings desktopSettings(desktopFile, QSettings::IniFormat);
        desktopSettings.beginGroup("Desktop Entry");

        const QString icon = desktopSettings.value("Icon").toString();
        const QString name = desktopSettings.value("Name").toString();
        const QString exec = desktopSettings.value("Exec").toString();

        if (!icon.isEmpty())
            info.insert("Icon", icon);
        if (!name.isEmpty())
            info.insert("Name", name);
        if (!exec.isEmpty())
            info.insert("Exec", exec);
    }

    return info;
}
