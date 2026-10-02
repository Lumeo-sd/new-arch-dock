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

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickView>
#include <QStandardPaths>
#include <QTranslator>
#include <QLocale>
#include <QDBusConnection>
#include <QLoggingCategory>

#include "applicationmodel.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // Qt enables the debug level of custom logging categories by default, which
    // would make the dock trace every mouse enter/leave. Silence just this one,
    // and only when the user has not asked for specific rules.
    if (!qEnvironmentVariableIsSet("QT_LOGGING_RULES"))
        QLoggingCategory::setFilterRules("cutefish.dock.lifecycle.debug=false");

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QApplication app(argc, argv);

    if (!QDBusConnection::sessionBus().registerService("com.cutefish.Dock")) {
        return -1;
    }

    qmlRegisterType<DockSettings>("Cutefish.Dock", 1, 0, "DockSettings");

    QString qmFilePath;
    const QStringList translationDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, "cutefish-dock/translations");
    for (const QString &dir : translationDirs) {
        const QString candidate = QString("%1/%2.qm").arg(dir, QLocale::system().name());
        if (QFile::exists(candidate)) {
            qmFilePath = candidate;
            break;
        }
    }
    if (QFile::exists(qmFilePath)) {
        QTranslator *translator = new QTranslator(QApplication::instance());
        if (translator->load(qmFilePath)) {
            QGuiApplication::installTranslator(translator);
        } else {
            translator->deleteLater();
        }
    }

    MainWindow w;

    if (!QDBusConnection::sessionBus().registerObject("/Dock", &w)) {
        return -1;
    }

    return app.exec();
}
