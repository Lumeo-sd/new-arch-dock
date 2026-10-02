/*
 * Copyright (C) 2021 CutefishOS Team.
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

#include "trashmanager.h"

#include <QDebug>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

const QString TrashDir = QDir::homePath() + "/.local/share/Trash";
const QDir::Filters ItemsShouldCount = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;

TrashManager::TrashManager(QObject *parent)
    : QObject(parent),
      m_filesWatcher(new QFileSystemWatcher(this)),
      m_count(0)
{
    onDirectoryChanged();
    connect(m_filesWatcher, &QFileSystemWatcher::directoryChanged, this, &TrashManager::onDirectoryChanged, Qt::QueuedConnection);
}

void TrashManager::moveToTrash(QList<QUrl> urls)
{
    QStringList paths;

    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        paths.append(url.toLocalFile());
    }

    if (paths.isEmpty())
        return;

    // CutefishOS shipped its own file manager that handled --move-to-trash.
    // It is not present on Plasma, so fall back to kioclient and finally to
    // the Qt trash implementation.
    if (QProcess::startDetached("cutefish-filemanager", QStringList() << "--move-to-trash" << paths))
        return;

    if (QProcess::startDetached("kioclient6", QStringList() << "move" << paths << QStringLiteral("trash:/")))
        return;

    for (const QString &path : qAsConst(paths)) {
        if (QFile::exists(path))
            QFile::moveToTrash(path);
    }
}

void TrashManager::emptyTrash()
{
    if (QProcess::startDetached("cutefish-filemanager", QStringList() << "-e"))
        return;

    // Plasma fallback: drop the contents of the standard per-user trash
    // directories. The trash:// protocol has no "empty" verb.
    const QStringList dirs = { TrashDir + QLatin1String("/files"), TrashDir + QLatin1String("/info") };

    for (const QString &dir : dirs) {
        QDir d(dir);

        if (!d.exists())
            continue;

        for (const QString &entry : d.entryList(ItemsShouldCount)) {
            const QString path = d.filePath(entry);
            const QFileInfo info(path);

            if (info.isDir() && !info.isSymLink())
                QDir(path).removeRecursively();
            else
                QFile::remove(path);
        }
    }

    onDirectoryChanged();
}

void TrashManager::openTrash()
{
    // Fall back to kioclient6, which opens a URL with the default handler.
    if (!QProcess::startDetached("cutefish-filemanager", QStringList() << "trash:///"))
        QProcess::startDetached("kioclient6", QStringList() << "exec" << QStringLiteral("trash:/"));
}

void TrashManager::onDirectoryChanged()
{
    m_filesWatcher->addPath(TrashDir);

    if (QDir(TrashDir + "/files").exists()) {
        m_filesWatcher->addPath(TrashDir + "/files");
        m_count = QDir(TrashDir + "/files").entryList(ItemsShouldCount).count();
    } else {
        m_count = 0;
    }

    emit countChanged();
}
