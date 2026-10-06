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

#include "launcher.h"
#include "launcheradaptor.h"
#include "iconthemeimageprovider.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QPixmapCache>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>

Launcher::Launcher(bool firstShow, QQuickView *w)
    : QQuickView(w)
    , m_dockInterface("com.cutefish.Dock",
                    "/Dock",
                    "com.cutefish.Dock", QDBusConnection::sessionBus())
    , m_hideTimer(new QTimer)
    , m_showed(false)
    , m_hiddenByFocusLoss(false)
    , m_hiddenByFocusLossTime(0)
    , m_leftMargin(0)
    , m_rightMargin(0)
    , m_bottomMargin(0)
{
    new LauncherAdaptor(this);

    engine()->rootContext()->setContextProperty("launcher", this);

    // QML modules (FishUI, Cutefish.Appearance) are installed next to a
    // private ~/.local prefix, not the system Qt qml directory, and Qt6 does
    // not read QT_QML_IMPORT_PATH. Explicitly add the standard import paths
    // relative to the binary, mirroring the dock (mainwindow.cpp).
    engine()->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../lib64/qt6/qml"));
    engine()->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/qt6/qml"));

    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint);
    setResizeMode(QQuickView::SizeRootObjectToView);
    onGeometryChanged();

    // Size the window BEFORE the QML loads. On X11 setGeometry() placed the
    // window so the root item sized itself correctly at load; on Wayland a
    // toplevel is sized by the compositor, so request the maximized state up
    // front - the launcher is an overlay that must leave the top panel and
    // the dock visible (a fullscreen surface covers them, and on Wayland a
    // fullscreen surface also never visually unmaps on hide).
    showMaximized();

    setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    setTitle(tr("Launcher"));

    if (firstShow) {
        // A cold "cutefish-launcher --show" start (the dock button) displays
        // immediately; record that as the shown state so the first dock
        // toggle hides it again. On Wayland QWindow::isVisible() does not
        // reliably reflect a shown fullscreen surface (the compositor maps it
        // asynchronously and a previous hide leaves the flag stale), which is
        // why toggle() below keys off m_showed.
        m_showed = true;
        m_hiddenByFocusLoss = false;
        emit showedChanged();
    } else {
        setVisible(false);
    }

    // Let the animation in qml be hidden after the execution is complete
    m_hideTimer->setInterval(200);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [=] { setVisible(false); });

    if (m_dockInterface.isValid() && !m_dockInterface.lastError().isValid()) {
        updateMargins();
        connect(&m_dockInterface, SIGNAL(primaryGeometryChanged()), this, SLOT(updateMargins()));
        connect(&m_dockInterface, SIGNAL(directionChanged()), this, SLOT(updateMargins()));
    } else {
        QDBusServiceWatcher *watcher = new QDBusServiceWatcher("com.cutefish.Dock",
                                                               QDBusConnection::sessionBus(),
                                                               QDBusServiceWatcher::WatchForUnregistration,
                                                               this);
        connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [=] {
            updateMargins();
            connect(&m_dockInterface, SIGNAL(primaryGeometryChanged()), this, SLOT(updateMargins()));
            connect(&m_dockInterface, SIGNAL(directionChanged()), this, SLOT(updateMargins()));
        });
    }

    connect(qApp, &QApplication::primaryScreenChanged, this, [=] { onGeometryChanged(); });
    connect(this, &QQuickView::activeChanged, this, &Launcher::onActiveChanged);
}

int Launcher::leftMargin() const
{
    return m_leftMargin;
}

int Launcher::rightMargin() const
{
    return m_rightMargin;
}

int Launcher::bottomMargin() const
{
    return m_bottomMargin;
}

bool Launcher::showed()
{
    return m_showed;
}

void Launcher::showWindow()
{
    m_showed = true;
    m_hiddenByFocusLoss = false;
    emit showedChanged();

    // A Wayland hide is a compositor-side minimize (see hideWindow()); clear
    // the minimized flag first so the fullscreen request below is not
    // swallowed by the minimized state.
    if (windowState() & Qt::WindowMinimized)
        showNormal();

    // Maximized overlay: the compositor keeps the top panel and the dock
    // visible (layer-shell struts are subtracted from the work area), so the
    // launcher reads as a "page" instead of covering the whole screen.
    showMaximized();

    // Request activation explicitly. On Wayland a programmatic show gets no
    // input focus by itself (the compositor maps the window but keeps the
    // current active window), and onActiveChanged() treats "shown but not
    // active" as an outside click and hides the launcher again immediately.
    // The dock's spawn+toggle path normally arrives right after a real click
    // so the compositor grants activation; asking anyway covers D-Bus shows
    // without a preceding input event.
    requestActivate();
}

void Launcher::hideWindow()
{
    // Plain hide. It is safe on Wayland because the launcher is a maximized
    // xdg-toplevel (not a fullscreen shell surface): KWin unmaps maximized
    // windows on hide, whereas a fullscreen surface stays composited even
    // after Qt flips its visibility flag to false.
    setVisible(false);

    m_showed = false;
    emit showedChanged();
}

void Launcher::toggle()
{
    // Key the toggle off our own flag, not QWindow::isVisible(): on Wayland
    // the fullscreen surface can be rendered while isVisible() is stale
    // (async map, hide-unmap), which would make the dock button toggle
    // always "show" and never hide.
    if (m_showed) {
        // Deliberate hide: discard any pending "hidden by focus loss" marker
        // so a subsequent click can re-show immediately.
        m_hiddenByFocusLoss = false;
        Launcher::hideWindow();
        return;
    }

    const qint64 sinceFocusHide = QDateTime::currentMSecsSinceEpoch()
                                  - m_hiddenByFocusLossTime;

    if (m_hiddenByFocusLoss && sinceFocusHide < 400) {
        // The dock button was clicked while the launcher was visible: the
        // click deactivated the window (hiding it) and this toggle is the
        // dock's response to that same click. Re-showing would make the
        // button unable to ever close an open launcher, so keep it hidden.
        // The 400ms window covers the whole dock round trip (press -> spawn
        // -> D-Bus toggle, measured ~260ms) and is far shorter than any
        // human "click desktop, then click launcher icon" sequence.
        m_hiddenByFocusLoss = false;
        return;
    }

    Launcher::showWindow();
}

bool Launcher::dockAvailable()
{
    return m_dockInterface.isValid();
}

bool Launcher::isPinedDock(const QString &desktop)
{
    QDBusInterface iface("com.cutefish.Dock",
                         "/Dock",
                         "com.cutefish.Dock",
                         QDBusConnection::sessionBus());

    if (!iface.isValid())
        return false;

    return iface.call("pinned", desktop).arguments().first().toBool();
}

void Launcher::clearPixmapCache()
{
    QPixmapCache::clear();
}

QRect Launcher::screenRect()
{
    return m_screenRect;
}

void Launcher::updateMargins()
{
    QRect dockGeometry = m_dockInterface.property("primaryGeometry").toRect();
    int dockDirection = m_dockInterface.property("direction").toInt();

    m_leftMargin = 0;
    m_rightMargin = 0;
    m_bottomMargin = 0;

    if (dockDirection == 0) {
        m_leftMargin = dockGeometry.width();
    } else if (dockDirection == 1) {
        m_bottomMargin = dockGeometry.height();
    } else if (dockDirection == 2) {
        m_rightMargin = dockGeometry.width();
    }

    emit marginsChanged();
}

void Launcher::updateSize()
{
    if (m_screenRect != qApp->primaryScreen()->geometry()) {
        m_screenRect = qApp->primaryScreen()->geometry();
        if (isVisible())
            showMaximized();
        emit screenRectChanged();
    }
}

void Launcher::onGeometryChanged()
{
    disconnect(screen());

    setScreen(qApp->primaryScreen());
    updateSize();

    connect(screen(), &QScreen::virtualGeometryChanged, this, &Launcher::updateSize);
    connect(screen(), &QScreen::geometryChanged, this, &Launcher::updateSize);
}

void Launcher::resizeEvent(QResizeEvent *e)
{
    // Let Qt6's SizeRootObjectToView resize the QML root item to match the
    // view. On Wayland the fullscreen scene is sized reactively: the
    // compositor replies to our xdg-toplevel fullscreen request with a
    // configure(1920,1080) that arrives after the QML has already loaded, so
    // without passing the event on the root item stays at its initial size
    // and content is crammed into a small region at the top-left. (The X11
    // WM forced the size synchronously, which is what the old ignore was for.)
    QQuickView::resizeEvent(e);
    e->ignore();
}

void Launcher::onActiveChanged()
{
    // Only treat this as an outside-click hide while the launcher is actually
    // shown and no context menu is open. When a dock toggle already hid us
    // (toggle wins the race over the compositor's late activeChanged), m_showed
    // is already false and this focus loss must not arm the same-click marker -
    // otherwise the very next dock click would be misread as "hide" and could
    // never re-open. A Wayland xdg-popup (the right-click DesktopMenu) steals
    // keyboard focus from our window, so an activeChanged during an open menu
    // must not hide the launcher either.
    if (!isActive() && m_showed && !m_contextMenuOpen) {
        m_hiddenByFocusLoss = true;
        m_hiddenByFocusLossTime = QDateTime::currentMSecsSinceEpoch();
        Launcher::hideWindow();
    }
}
