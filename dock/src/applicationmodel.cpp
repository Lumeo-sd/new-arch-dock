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

#include "applicationmodel.h"
#include "processprovider.h"
#include "utils.h"

#include <QProcess>

ApplicationModel::ApplicationModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_iface(XWindowInterface::instance())
    , m_sysAppMonitor(SystemAppMonitor::self())
{
    connect(m_iface, &XWindowInterface::windowAdded, this, &ApplicationModel::onWindowAdded);
    connect(m_iface, &XWindowInterface::windowRemoved, this, &ApplicationModel::onWindowRemoved);
    connect(m_iface, &XWindowInterface::activeChanged, this, &ApplicationModel::onActiveChanged);

    initPinnedApplications();

    qInfo() << "cutefish-dock debug build ready, rowCount=" << rowCount() << "debug-fork-3";

    QTimer::singleShot(100, m_iface, &XWindowInterface::startInitWindows);
}

int ApplicationModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)

    return m_appItems.size();
}

QHash<int, QByteArray> ApplicationModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AppIdRole] = "appId";
    roles[IconNameRole] = "iconName";
    roles[VisibleNameRole] = "visibleName";
    roles[ActiveRole] = "isActive";
    roles[WindowCountRole] = "windowCount";
    roles[IsPinnedRole] = "isPinned";
    roles[DesktopFileRole] = "desktopFile";
    roles[FixedItemRole] = "fixed";
    roles[DropSlotRole] = "dropSlot";
    return roles;
}

QVariant ApplicationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    ApplicationItem *item = m_appItems.at(index.row());

    switch (role) {
    case AppIdRole:
        return item->id;
    case IconNameRole:
        return item->iconName;
    case VisibleNameRole:
        return item->visibleName;
    case ActiveRole:
        return item->isActive;
    case WindowCountRole:
        return item->wids.count();
    case IsPinnedRole:
        return item->isPinned;
    case DesktopFileRole:
        return item->desktopPath;
    case FixedItemRole:
        return item->fixed;
    case DropSlotRole:
        return item->dropSlot;
    default:
        return QVariant();
    }

    return QVariant();
}

void ApplicationModel::addItem(const QString &desktopFile)
{
    ApplicationItem *existsItem = findItemByDesktop(desktopFile);

    if (existsItem) {
        existsItem->isPinned = true;
        return;
    }

    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    ApplicationItem *item = new ApplicationItem;
    QMap<QString, QString> desktopInfo = Utils::instance()->readInfoFromDesktop(desktopFile);
    item->iconName = desktopInfo.value("Icon");
    item->visibleName = desktopInfo.value("Name");
    item->exec = desktopInfo.value("Exec");
    item->desktopPath = desktopFile;
    item->isPinned = true;

    // First use filename as the id of the item.
    // Why not use exec? Because exec contains the file path,
    // QSettings will have problems, resulting in unrecognized next time.
    QFileInfo fi(desktopFile);
    item->id = fi.completeBaseName();

    m_appItems << item;
    endInsertRows();

    savePinAndUnPinList();

    emit itemAdded();
    emit countChanged();
}

// Insert a new pinned app at a specific row so drag & drop can place it in the
// middle instead of always appending at the end.
void ApplicationModel::insertItem(const QString &desktopFile, int index)
{
    ApplicationItem *existsItem = findItemByDesktop(desktopFile);

    if (existsItem) {
        // Already present (pinned or running) -> just relocate it.
        existsItem->isPinned = true;
        moveItem(existsItem, index);
        handleDataChangedFromItem(existsItem);
        savePinAndUnPinList();
        return;
    }

    // A drop slot is visible where the user releases: reuse that row in place
    // (no insert/remove pair), so the gap the user aimed at becomes the pin.
    int slot = dropSlotIndex();

    if (slot != -1) {
        ApplicationItem *item = m_appItems.at(slot);

        QMap<QString, QString> desktopInfo = Utils::instance()->readInfoFromDesktop(desktopFile);
        item->iconName = desktopInfo.value("Icon");
        item->visibleName = desktopInfo.value("Name");
        item->exec = desktopInfo.value("Exec");
        item->desktopPath = desktopFile;
        item->isPinned = true;
        item->fixed = false;
        item->dropSlot = false;

        QFileInfo fi(desktopFile);
        item->id = fi.completeBaseName();

        moveItem(item, index);

        // The slot row became a real pinned app; refresh the delegate so the
        // icon/name appear immediately (dropSlot=false, fixed=false, ...).
        handleDataChangedFromItem(item);

        savePinAndUnPinList();
        emit itemAdded();
        emit countChanged();
        qInfo() << "slot insert" << desktopFile << "at" << index;
        return;
    }

    // Plain insert (e.g. no slot was ever created).
    int from = qBound(1, index, rowCount());
    beginInsertRows(QModelIndex(), from, from);
    ApplicationItem *item = new ApplicationItem;
    QMap<QString, QString> desktopInfo = Utils::instance()->readInfoFromDesktop(desktopFile);
    item->iconName = desktopInfo.value("Icon");
    item->visibleName = desktopInfo.value("Name");
    item->exec = desktopInfo.value("Exec");
    item->desktopPath = desktopFile;
    item->isPinned = true;

    QFileInfo fi(desktopFile);
    item->id = fi.completeBaseName();

    m_appItems.insert(from, item);
    endInsertRows();

    savePinAndUnPinList();
    emit itemAdded();
    emit countChanged();
}

// --- Drop slot (live insertion gap while an external drag hovers the dock) ---

void ApplicationModel::beginDropSlot(int index)
{
    int slot = dropSlotIndex();

    if (slot != -1) {
        moveDropSlot(index);
        return;
    }

    index = qBound(1, index, rowCount());

    beginInsertRows(QModelIndex(), index, index);
    ApplicationItem *item = new ApplicationItem;
    item->id = "__dropslot__";
    item->dropSlot = true;
    item->fixed = true;
    m_appItems.insert(index, item);
    endInsertRows();
    qInfo() << "slot begin" << index;
}

void ApplicationModel::moveDropSlot(int index)
{
    int from = dropSlotIndex();

    if (from == -1 || from == index)
        return;

    index = qBound(1, index, rowCount() - 1);

    moveItem(m_appItems.at(from), index);
    qInfo() << "slot move" << from << "->" << index;
}

bool ApplicationModel::endDropSlot()
{
    int from = dropSlotIndex();

    if (from == -1)
        return false;

    beginRemoveRows(QModelIndex(), from, from);
    ApplicationItem *item = m_appItems.takeAt(from);
    endRemoveRows();
    delete item;
    qInfo() << "slot end";
    return true;
}

int ApplicationModel::dropSlotIndex() const
{
    for (int i = 0; i < m_appItems.size(); ++i) {
        if (m_appItems.at(i)->dropSlot)
            return i;
    }

    return -1;
}

bool ApplicationModel::dropSlotActive() const
{
    return dropSlotIndex() != -1;
}

void ApplicationModel::moveItem(ApplicationItem *item, int to)
{
    int from = m_appItems.indexOf(item);

    if (from == -1 || from == to)
        return;

    to = qBound(0, to, rowCount() - 1);

    qInfo() << "model move" << item->id << "from" << from << "to" << to;

    m_appItems.move(from, to);

    if (from < to)
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to + 1);
    else
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to);

    endMoveRows();
}

void ApplicationModel::removeItem(const QString &desktopFile)
{
    ApplicationItem *item = findItemByDesktop(desktopFile);

    if (item) {
        ApplicationModel::unPin(item->id);
    }
}

bool ApplicationModel::desktopContains(const QString &desktopFile)
{
    if (desktopFile.isEmpty())
        return false;

    return findItemByDesktop(desktopFile) != nullptr;
}

bool ApplicationModel::isDesktopPinned(const QString &desktopFile)
{
    ApplicationItem *item = findItemByDesktop(desktopFile);

    if (item) {
        return item->isPinned;
    }

    return false;
}

void ApplicationModel::clicked(const QString &id)
{
    ApplicationItem *item = findItemById(id);

    if (!item)
        return;

    // Application Item that has been pinned,
    // We need to open it.
    if (item->wids.isEmpty()) {
        // open application
        openNewInstance(item->id);
        return;
    }

    const WId active = m_iface->activeWindow();

    // The clicked window (or one of the app's windows) is already the active
    // one: minimize it. This is checked BEFORE the multi-window cycle, else an
    // app with a second window (a dialog, a Picture-in-Picture window, ...)
    // could never be minimized from the dock — clicking would just rotate
    // through its windows.
    if (item->wids.contains(active)) {
        m_iface->minimizeWindow(active);
        return;
    }

    // Multiple windows have been opened and none of them is active: switch
    // between them.
    if (item->wids.count() > 1) {
        item->currentActive++;

        if (item->currentActive == item->wids.count())
            item->currentActive = 0;

        m_iface->forceActiveWindow(item->wids.at(item->currentActive));
        return;
    }

    // Single window, not active: activate it.
    m_iface->forceActiveWindow(item->wids.first());
}

void ApplicationModel::raiseWindow(const QString &id)
{
    ApplicationItem *item = findItemById(id);

    if (!item || item->wids.isEmpty())
        return;

    if (item->currentActive < 0 || item->currentActive >= item->wids.size())
        item->currentActive = 0;

    m_iface->forceActiveWindow(item->wids.at(item->currentActive));
}

// The Exec value of a .desktop file carries desktop field codes (%U, %F, %i,
// %c, %k, ...) and, for Flatpak apps, "@@ ... @@" file-forwarding groups. The
// dock launches an app without any file arguments, so those codes must be
// dropped: passing a literal "%U" makes e.g. Flatpak refuse to start. Parsed
// shell-style (quotes/backslash escapes) like the desktop spec requires.
static QStringList launchArguments(const QString &exec)
{
    QStringList args;
    QString token;
    bool inSingle = false;
    bool inDouble = false;

    for (int i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);

        if (inSingle) {
            if (c == QLatin1Char('\''))
                inSingle = false;
            else
                token += c;
            continue;
        }

        if (inDouble) {
            if (c == QLatin1Char('"'))
                inDouble = false;
            else if (c == QLatin1Char('\\') && i + 1 < exec.size())
                token += exec.at(++i);
            else
                token += c;
            continue;
        }

        if (c == QLatin1Char('\'')) {
            inSingle = true;
        } else if (c == QLatin1Char('"')) {
            inDouble = true;
        } else if (c.isSpace()) {
            if (!token.isEmpty()) {
                args << token;
                token.clear();
            }
        } else if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
            token += exec.at(++i);
        } else {
            token += c;
        }
    }

    if (!token.isEmpty())
        args << token;

    // Drop field codes and flatpak file-forwarding groups.
    QStringList cleaned;
    bool inForwardGroup = false;

    for (const QString &arg : args) {
        if (arg.startsWith(QStringLiteral("@@"))) {
            inForwardGroup = true;
            continue;
        }
        if (inForwardGroup) {
            if (arg == QStringLiteral("@@"))
                inForwardGroup = false;
            continue;
        }

        const QString lower = arg.toLower();
        if (lower == QStringLiteral("%u") || lower == QStringLiteral("%f")
                || lower == QStringLiteral("%i") || lower == QStringLiteral("%c")
                || lower == QStringLiteral("%k") || lower == QStringLiteral("%v")
                || lower == QStringLiteral("%m"))
            continue;

        QString cleanedArg = arg;
        cleaned << cleanedArg.replace(QStringLiteral("%%"), QStringLiteral("%"));
    }

    return cleaned;
}

bool ApplicationModel::openNewInstance(const QString &appId)
{
    ApplicationItem *item = findItemById(appId);

    if (!item)
        return false;

    if (!item->exec.isEmpty()) {
        const QStringList launchArgs = launchArguments(item->exec);

        if (launchArgs.isEmpty())
            return false;

        if (launchArgs.size() > 1)
            ProcessProvider::startDetached(launchArgs.first(), launchArgs.mid(1));
        else
            ProcessProvider::startDetached(launchArgs.first());
    } else {
        ProcessProvider::startDetached(appId);
    }

    return true;
}

QVariantList ApplicationModel::windowInfos(const QString &appId)
{
    QVariantList infos;

    ApplicationItem *item = findItemById(appId);

    if (!item) {
        qInfo() << "windowInfos: no app for" << appId;
        return infos;
    }

    qInfo() << "windowInfos for" << appId << "wids:" << item->wids.length();

    for (quint64 wid : item->wids) {
        QMap<QString, QVariant> info = m_iface->requestInfo(wid);
        QVariantMap m;
        // KWin's screencasting lookup does not know the window under the
        // braced QUuid form ("{...}", cf. Krema's "Could not find window id"
        // journal entry); pass the bare id.
        QString uuid = info.value("uuid").toString();
        uuid.remove(QLatin1Char('{')).remove(QLatin1Char('}'));
        m["uuid"] = uuid;
        m["icon"] = item->iconName;
        m["title"] = info.value("visibleName");
        m["active"] = info.value("active");
        m["minimized"] = info.value("minimized");
        infos.append(m);
        qInfo() << "windowInfos entry wid=" << wid
                << "uuid=" << info.value("uuid").toString()
                << "title=" << info.value("visibleName").toString()
                << "active=" << info.value("active").toBool()
                << "minimized=" << info.value("minimized").toBool();
    }

    return infos;
}

void ApplicationModel::activateWindowForApp(const QString &appId, int widIndex)
{
    ApplicationItem *item = findItemById(appId);

    if (!item || widIndex < 0 || widIndex >= item->wids.size())
        return;

    m_iface->forceActiveWindow(item->wids.at(widIndex));
}

void ApplicationModel::closeWindowForApp(const QString &appId, int widIndex)
{
    ApplicationItem *item = findItemById(appId);

    if (!item || widIndex < 0 || widIndex >= item->wids.size())
        return;

    m_iface->closeWindow(item->wids.at(widIndex));
}

void ApplicationModel::closeAllByAppId(const QString &appId)
{
    ApplicationItem *item = findItemById(appId);

    if (!item)
        return;

    for (quint64 wid : item->wids) {
        m_iface->closeWindow(wid);
    }
}

void ApplicationModel::pin(const QString &appId)
{
    ApplicationItem *item = findItemById(appId);

    if (!item)
        return;

    item->isPinned = true;

    handleDataChangedFromItem(item);
    savePinAndUnPinList();
}

void ApplicationModel::unPin(const QString &appId)
{
    qInfo() << "unpin" << appId;
    ApplicationItem *item = findItemById(appId);

    if (!item)
        return;

    item->isPinned = false;
    handleDataChangedFromItem(item);

    // Need to be removed after unpin
    if (item->wids.isEmpty()) {
        int index = indexOf(item->id);
        if (index != -1) {
            beginRemoveRows(QModelIndex(), index, index);
            m_appItems.removeAll(item);
            endRemoveRows();

            emit itemRemoved();
            emit countChanged();
            delete item;
        }
    }

    savePinAndUnPinList();
}

void ApplicationModel::updateGeometries(const QString &id, QRect rect)
{
    ApplicationItem *item = findItemById(id);

    // If not found
    if (!item)
        return;

    for (quint64 id : item->wids) {
        m_iface->setIconGeometry(id, rect);
    }
}

void ApplicationModel::move(int from, int to)
{
    // The indices arrive from QML, so they can be stale by the time the drag
    // finishes. Clamp them instead of letting QList::move abort the process.
    const int last = m_appItems.size() - 1;
    if (from < 0 || from > last)
        return;

    to = qBound(0, to, last);
    if (from == to)
        return;

    // beginMoveRows() must wrap the mutation, and the destination is expressed
    // in the coordinates *before* the move.
    const int destination = (from < to) ? to + 1 : to;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
    m_appItems.move(from, to);
    endMoveRows();
}

ApplicationItem *ApplicationModel::findItemByWId(quint64 wid)
{
    for (ApplicationItem *item : m_appItems) {
        for (quint64 winId : item->wids) {
            if (winId == wid)
                return item;
        }
    }

    return nullptr;
}

ApplicationItem *ApplicationModel::findItemById(const QString &id)
{
    for (ApplicationItem *item : m_appItems) {
        if (item->id == id)
            return item;
    }

    return nullptr;
}

ApplicationItem *ApplicationModel::findItemByDesktop(const QString &desktop)
{
    for (ApplicationItem *item : m_appItems) {
        if (item->desktopPath == desktop)
            return item;
    }

    return nullptr;
}

bool ApplicationModel::contains(const QString &id)
{
    for (ApplicationItem *item : qAsConst(m_appItems)) {
        if (item->id == id)
            return true;
    }

    return false;
}

int ApplicationModel::indexOf(const QString &id)
{
    for (ApplicationItem *item : m_appItems) {
        if (item->id == id)
            return m_appItems.indexOf(item);
    }

    return -1;
}

void ApplicationModel::initPinnedApplications()
{
    QSettings settings(QSettings::UserScope, "cutefishos", "dock_pinned");
    // The default list is configured into the install prefix: a private
    // ~/.local install lands in <prefix>/etc, while PREFIX=/usr lands in the
    // real /etc (GNUInstallDirs special-cases /usr). Keep /etc as a fallback
    // for builds that predate the definition.
#ifdef CUTEFISH_DOCK_LIST_CONF
    QString systemList = QStringLiteral(CUTEFISH_DOCK_LIST_CONF);
    if (!QFile::exists(systemList))
        systemList = QStringLiteral("/etc/cutefish-dock-list.conf");
#else
    QString systemList = QStringLiteral("/etc/cutefish-dock-list.conf");
#endif
    QSettings systemSettings(systemList, QSettings::IniFormat);
    QSettings *set = (QFile(settings.fileName()).exists()) ? &settings
                                                           : &systemSettings;
    QStringList groups = set->childGroups();

    // Launcher. "--show" makes a cold spawn display the grid immediately; a
    // warm spawn finds the D-Bus name taken and makes the running instance
    // toggle instead (see Launcher::main), so one exec line serves both the
    // first click and every click after it.
    ApplicationItem *item = new ApplicationItem;
    item->id = "cutefish-launcher";
    item->exec = "cutefish-launcher --show";
    item->iconName = "qrc:/images/launcher.svg";
    item->visibleName = tr("Launcher");
    item->fixed = true;
    m_appItems.append(item);

    // Activities overview. It lives in the model rather than as a standalone
    // cell in main.qml because it has to sit *after* the launcher, and the
    // launcher is model index 0 - the only thing that can follow it is another
    // model row. Fixed, like the launcher, so it cannot be dragged away.
    // "computer" rather than "view-grid": the latter ships only in the 16/22/24
    // breeze actions, and at a 53 px cell Qt looks for a size that is not
    // there, which renders an empty cell.
    ApplicationItem *overviewItem = new ApplicationItem;
    overviewItem->id = "cutefish-overview";
    overviewItem->iconName = "computer";
    overviewItem->visibleName = tr("Activities overview");
    overviewItem->fixed = true;
    m_appItems.append(overviewItem);

    // Pinned Apps
    for (int i = 0; i < groups.size(); ++i) {
        for (const QString &id : groups) {
            set->beginGroup(id);
            int index = set->value("Index").toInt();

            if (index == i) {
                // Check before beginInsertRows(): skipping an insert from
                // inside the begin/end pair would leave the model unbalanced.
                const QString desktopPath = set->value("DesktopPath").toString();
                if (desktopPath.isEmpty() || !QFile::exists(desktopPath)) {
                    set->endGroup();
                    continue;
                }

                beginInsertRows(QModelIndex(), rowCount(), rowCount());
                ApplicationItem *item = new ApplicationItem;

                item->desktopPath = desktopPath;
                item->id = id;
                item->isPinned = true;

                // Read from desktop file.
                if (!item->desktopPath.isEmpty()) {
                    QMap<QString, QString> desktopInfo = Utils::instance()->readInfoFromDesktop(item->desktopPath);
                    item->iconName = desktopInfo.value("Icon");
                    item->visibleName = desktopInfo.value("Name");
                    item->exec = desktopInfo.value("Exec");
                }

                // Read from config file.
                if (item->iconName.isEmpty())
                    item->iconName = set->value("Icon").toString();

                if (item->visibleName.isEmpty())
                    item->visibleName = set->value("VisibleName").toString();

                if (item->exec.isEmpty())
                    item->exec = set->value("Exec").toString();

                m_appItems.append(item);
                endInsertRows();

                emit itemAdded();
                emit countChanged();

                set->endGroup();
                break;
            } else {
                set->endGroup();
            }
        }
    }
}

void ApplicationModel::savePinAndUnPinList()
{
    QSettings settings(QSettings::UserScope, "cutefishos", "dock_pinned");
    settings.clear();

    int index = 0;

    for (ApplicationItem *item : m_appItems) {
        if (item->isPinned) {
            settings.beginGroup(item->id);
            settings.setValue("Index", index);
            settings.setValue("Icon", item->iconName);
            settings.setValue("VisibleName", item->visibleName);
            settings.setValue("Exec", item->exec);
            settings.setValue("DesktopPath", item->desktopPath);
            settings.endGroup();
            ++index;
        }
    }

    settings.sync();
}

void ApplicationModel::handleDataChangedFromItem(ApplicationItem *item)
{
    if (!item)
        return;

    QModelIndex idx = index(indexOf(item->id), 0, QModelIndex());

    if (idx.isValid()) {
        emit dataChanged(idx, idx);
    }
}

void ApplicationModel::onWindowAdded(quint64 wid)
{
    QMap<QString, QVariant> info = m_iface->requestInfo(wid);
    const QString id = info.value("id").toString();

    // Skip...
    if (id == "cutefish-launcher")
        return;

    QString desktopPath = m_iface->desktopFilePath(wid);
    ApplicationItem *desktopItem = findItemByDesktop(desktopPath);

    // Use desktop find
    if (!desktopPath.isEmpty() && desktopItem != nullptr) {
        desktopItem->wids.append(wid);
        // Need to update application active status.
        desktopItem->isActive = info.value("active").toBool();

        if (desktopItem->id != id) {
            desktopItem->id = id;
            savePinAndUnPinList();
        }

        handleDataChangedFromItem(desktopItem);
    }
    // Find from id
    else if (contains(id)) {
        for (ApplicationItem *item : m_appItems) {
            if (item->id == id) {
                item->wids.append(wid);
                // Need to update application active status.
                item->isActive = info.value("active").toBool();
                handleDataChangedFromItem(item);
            }
        }
    }
    // New item needs to be added.
    else {
        beginInsertRows(QModelIndex(), rowCount(), rowCount());
        ApplicationItem *item = new ApplicationItem;
        item->id = id;
        item->iconName = info.value("iconName").toString();
        item->visibleName = info.value("visibleName").toString();
        item->isActive = info.value("active").toBool();
        item->wids.append(wid);

        if (!desktopPath.isEmpty()) {
            QMap<QString, QString> desktopInfo = Utils::instance()->readInfoFromDesktop(desktopPath);
            item->iconName = desktopInfo.value("Icon");
            item->visibleName = desktopInfo.value("Name");
            item->exec = desktopInfo.value("Exec");
            item->desktopPath = desktopPath;
        }

        m_appItems << item;
        endInsertRows();

        emit itemAdded();
        emit countChanged();
    }
}

void ApplicationModel::onWindowRemoved(quint64 wid)
{
    ApplicationItem *item = findItemByWId(wid);

    if (!item)
        return;

    // Remove from wid list.
    item->wids.removeOne(wid);

    if (item->currentActive >= item->wids.size())
        item->currentActive = 0;

    handleDataChangedFromItem(item);

    if (item->wids.isEmpty()) {
        // If it is not fixed to the dock, need to remove it.
        if (!item->isPinned) {
            int index = indexOf(item->id);

            if (index == -1)
                return;

            beginRemoveRows(QModelIndex(), index, index);
            m_appItems.removeAll(item);
            endRemoveRows();

            emit itemRemoved();
            emit countChanged();
            delete item;
        }
    }
}

void ApplicationModel::onActiveChanged(quint64 wid)
{
    // Using this method will cause the listview scrollbar to reset.
    // beginResetModel();

    for (ApplicationItem *item : m_appItems) {
        if (item->isActive != item->wids.contains(wid)) {
            item->isActive = item->wids.contains(wid);

            QModelIndex idx = index(indexOf(item->id), 0, QModelIndex());
            if (idx.isValid()) {
                emit dataChanged(idx, idx);
            }
        }
    }
}
