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

#include "systemappmonitor.h"

#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>

static SystemAppMonitor *SELF = nullptr;

// Ordered list of the XDG application directories, highest priority first. The
// user directory comes before the system ones (and the per-user flatpak export
// before its system-wide counterpart), so a user override wins over the
// packaged desktop file, as the XDG basedir spec requires.
static QStringList applicationDirectories()
{
    const QString userData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    QStringList dirs;
    dirs << userData + QStringLiteral("/applications");
    dirs << userData + QStringLiteral("/flatpak/exports/share/applications");

    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dir : dataDirs) {
        if (dir == userData)
            continue;
        dirs << dir + QStringLiteral("/applications");
    }

    // System-wide flatpak exports are not always part of XDG_DATA_DIRS.
    dirs << QStringLiteral("/var/lib/flatpak/exports/share/applications");

    QStringList result;
    for (const QString &dir : dirs) {
        if (!result.contains(dir))
            result << dir;
    }

    return result;
}

static QByteArray detectDesktopEnvironment()
{
    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");

    if (!desktop.isEmpty())
        return desktop.toUpper();

    return QByteArray("UNKNOWN");
}

SystemAppMonitor *SystemAppMonitor::self()
{
    if (SELF == nullptr)
        SELF = new SystemAppMonitor;

    return SELF;
}

SystemAppMonitor::SystemAppMonitor(QObject *parent)
    : QObject(parent)
{
    QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
    for (const QString &dir : applicationDirectories()) {
        if (QDir(dir).exists())
            watcher->addPath(dir);
    }
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &SystemAppMonitor::refresh);
    refresh();
}

SystemAppMonitor::~SystemAppMonitor()
{
    while (!m_items.isEmpty())
        delete m_items.takeFirst();
}

SystemAppItem *SystemAppMonitor::find(const QString &filePath)
{
    for (SystemAppItem *item : m_items)
        if (item->path == filePath)
            return item;

    return nullptr;
}

void SystemAppMonitor::refresh()
{
    QStringList addedEntries;
    for (SystemAppItem *item : m_items)
        addedEntries.append(item->path);

    QStringList allEntries;
    QSet<QString> seenBaseNames;

    for (const QString &dir : applicationDirectories()) {
        QDirIterator it(dir, { "*.desktop" }, QDir::NoFilter, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString &filePath = it.next();

            if (!QFile::exists(filePath))
                continue;

            // A desktop file may exist in several XDG directories at once.
            // Keep only the highest-priority copy (the directories are already
            // ordered user-first) so an app never shows up twice.
            // Qt 6: use completeBaseName() to keep the pre-Qt6 baseName()
            // semantics ("org.kde.dolphin.desktop" dedups as "org.kde.dolphin",
            // not "org", which would collapse every org.kde.* app into one).
            const QString baseName = QFileInfo(filePath).completeBaseName().toLower();
            if (seenBaseNames.contains(baseName))
                continue;

            seenBaseNames.insert(baseName);
            allEntries.append(filePath);
        }
    }

    for (const QString &filePath : allEntries) {
        if (!addedEntries.contains(filePath)) {
            addApplication(filePath);
        }
    }

    for (SystemAppItem *item : m_items) {
        if (!allEntries.contains(item->path)) {
            removeApplication(item);
        }
    }

    emit refreshed();
}

void SystemAppMonitor::addApplication(const QString &filePath)
{
    if (find(filePath))
        return;

    QSettings desktop(filePath, QSettings::IniFormat);
    desktop.beginGroup("Desktop Entry");

    if (desktop.value("Terminal").toBool())
        return;

    if (desktop.contains("OnlyShowIn")) {
        const QString &value = desktop.value("OnlyShowIn").toString();
        if (!value.contains(detectDesktopEnvironment(), Qt::CaseInsensitive)) {
            return;
        }
    }

    if (desktop.value("NoDisplay").toBool() ||
        desktop.value("Hidden").toBool()) {
        return;
    }

    QString appName = desktop.value(QString("Name[%1]").arg(QLocale::system().name())).toString();
    QString appExec = desktop.value("Exec").toString();

    if (appName.isEmpty())
        appName = desktop.value("Name").toString();

    appExec.remove(QRegularExpression("%."));
    appExec.remove(QRegularExpression("^\""));
    // appExec.remove(QRegularExpression(" *$"));
    appExec = appExec.simplified();

    SystemAppItem *item = new SystemAppItem;
    item->path = filePath;
    item->name = appName;
    item->genericName = desktop.value("GenericName").toString();
    item->comment = desktop.value("Comment").toString();
    item->iconName = desktop.value("Icon").toString();
    item->startupWMClass = desktop.value("StartupWMClass").toString();
    item->exec = appExec;
    item->args = appExec.split(" ");

    m_items.append(item);
}

void SystemAppMonitor::removeApplication(SystemAppItem *item)
{
    m_items.removeOne(item);
    item->deleteLater();
}
