/*
 * Copyright (C) 2021 CutefishOS.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
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

#include <QApplication>
#include <QFile>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QPixmapCache>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include <QStandardPaths>

#include "launcher.h"
#include "launchermodel.h"
#include "pagemodel.h"
#include "iconitem.h"
#include "appmanager.h"

#include <QDebug>
#include <QTranslator>
#include <QLocale>

#define DBUS_NAME "com.cutefish.Launcher"
#define DBUS_PATH "/Launcher"
#define DBUS_INTERFACE "com.cutefish.Launcher"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QByteArray uri = "Cutefish.Launcher";
    qmlRegisterType<LauncherModel>(uri, 1, 0, "LauncherModel");
    qmlRegisterType<PageModel>(uri, 1, 0, "PageModel");
    qmlRegisterType<IconItem>(uri, 1, 0, "IconItem");
    qmlRegisterType<AppManager>(uri, 1, 0, "AppManager");

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    qmlRegisterType<QAbstractItemModel>();
#else
    qmlRegisterAnonymousType<QAbstractItemModel>(uri, 0);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cutefish-launcher"));

    // The launcher is a persistent D-Bus service whose window visibility is
    // toggled by the dock; its process lifetime must not be tied to whether a
    // window happens to be mapped. Every grid delegate instantiates a
    // FishUI.MenuPopupWindow (a real Qt::Popup window), and Qt closes all
    // popup windows whenever the application state or focus changes
    // (QGuiApplicationPrivate::closeAllPopups). On Wayland the fullscreen
    // window is mapped asynchronously - the compositor's configure arrives
    // after Qt considers the window shown - and a dock toggle hides it from
    // Qt's point of view (isVisible() == false) even though the compositor
    // may keep the surface composited, so at the moment the popups close Qt
    // may see no treatable visible top-level window at all. With the default
    // quitOnLastWindowClosed() == true that posts a QEvent::Quit and the
    // process exits cleanly (code 0): no crash, no coredump, no trace.
    app.setQuitOnLastWindowClosed(false);

    QPixmapCache::setCacheLimit(2048);

    QCommandLineParser parser;
    QCommandLineOption showOption(QStringLiteral("show"), "Show Launcher");
    parser.addOption(showOption);
    // QCommandLineOption hideOption(QStringLiteral("hide"), "Hide Launcher");
    // parser.addOption(hideOption);
    // QCommandLineOption toggleOption(QStringLiteral("toggle"), "Toggle Launcher");
    // parser.addOption(toggleOption);
    parser.process(app.arguments());

    QDBusConnection dbus = QDBusConnection::sessionBus();
    if (!dbus.registerService(DBUS_NAME)) {
        QDBusInterface iface(DBUS_NAME, DBUS_PATH, DBUS_INTERFACE, dbus);
        iface.call("toggle");
        return -1;
    }

    QLocale locale;
    // Look for the locale file in every XDG data directory (in a private
    // ~/.local install the compiled translations live under
    // ~/.local/share/cutefish-launcher/translations, not /usr/share).
    const QString localeFile = QStringLiteral("%1.qm").arg(locale.name());
    const QStringList dirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("cutefish-launcher/translations"),
        QStandardPaths::LocateDirectory);

    for (const QString &dir : dirs) {
        const QString qmFilePath = dir + QLatin1Char('/') + localeFile;
        if (!QFile::exists(qmFilePath))
            continue;

        QTranslator *translator = new QTranslator(app.instance());
        if (translator->load(qmFilePath)) {
            app.installTranslator(translator);
        } else {
            translator->deleteLater();
        }
        break;
    }

    bool firstShow = parser.isSet(showOption);
    Launcher launcher(firstShow);

    if (!dbus.registerObject(DBUS_PATH, DBUS_INTERFACE, &launcher))
        return -1;

    return app.exec();
}
