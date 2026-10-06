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

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <QGuiApplication>
#include <QQuickView>
#include <QTimer>

#include <QDBusInterface>

class Launcher : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(QRect screenRect READ screenRect NOTIFY screenRectChanged)
    Q_PROPERTY(bool showed READ showed NOTIFY showedChanged)
    Q_PROPERTY(int leftMargin READ leftMargin NOTIFY marginsChanged)
    Q_PROPERTY(int rightMargin READ rightMargin NOTIFY marginsChanged)
    Q_PROPERTY(int bottomMargin READ bottomMargin NOTIFY marginsChanged)

public:
    Launcher(bool firstShow = false, QQuickView *w = nullptr);

    int leftMargin() const;
    int rightMargin() const;
    int bottomMargin() const;

    bool showed();

    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void hideWindow();
    Q_INVOKABLE void toggle();

    // The auto-generated D-Bus adaptor (launcheradaptor.cpp) maps the
    // com.cutefish.Launcher "show"/"hide"/"toggle" methods to
    // parent()->show() / parent()->hide() / parent()->toggle(). QWindow
    // already provides show()/hide(), so without these overrides the dock's
    // D-Bus calls would hit the raw QWindow functions: m_showed would never
    // be updated (breaking the toggle) and on Wayland the fullscreen surface
    // would stay composited even when Qt considers the window hidden. Route
    // them through the state-managing window functions instead.
    Q_INVOKABLE void show() { showWindow(); }
    Q_INVOKABLE void hide() { hideWindow(); }

    // Guard against the Wayland xdg-popup (context menu) stealing keyboard
    // focus from the launcher window, which would fire onActiveChanged and
    // hide the launcher before the menu can appear.
    Q_INVOKABLE void setContextMenuOpen(bool open) { m_contextMenuOpen = open; }

    Q_INVOKABLE bool dockAvailable();
    Q_INVOKABLE bool isPinedDock(const QString &desktop);

    Q_INVOKABLE void clearPixmapCache();

    QRect screenRect();

signals:
    void screenRectChanged();
    void showedChanged();
    void marginsChanged();

private slots:
    void updateMargins();
    void updateSize();
    void onGeometryChanged();

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void onActiveChanged();

private:
    QDBusInterface m_dockInterface;
    QRect m_screenRect;
    QTimer *m_hideTimer;
    bool m_showed;

    // Set when the window is hidden because it lost focus (a click on another
    // window, the desktop or the dock). toggle() uses this to avoid re-showing
    // the launcher after the very click that hid it (the dock click both
    // deactivates the launcher and spawns a --show toggle; without this the
    // button would never be able to hide an open launcher).
    bool m_hiddenByFocusLoss;
    qint64 m_hiddenByFocusLossTime;

    bool m_contextMenuOpen = false;

    int m_leftMargin;
    int m_rightMargin;
    int m_bottomMargin;
};

#endif // LAUNCHER_H
