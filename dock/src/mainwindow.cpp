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

#include "mainwindow.h"

#include "plasmavirtualdesktop.h"
#include "processprovider.h"
#include "xwindowinterface.h"
#include "dockadaptor.h"

#include <QGuiApplication>
#include <QScreen>
#include <QCoreApplication>
#include <QFile>
#include <QUrl>

#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QMetaEnum>

#include <LayerShellQt/window.h>

MainWindow::MainWindow(QQuickView *parent)
    : QQuickView(parent)
    , m_activity(Activity::self())
    , m_settings(DockSettings::self())
    , m_appModel(new ApplicationModel)
    , m_trashManager(new TrashManager)
    , m_layerShell(nullptr)
    , m_hideBlocked(false)
    , m_dockHidden(false)
    , m_showTimer(new QTimer(this))
    , m_hideTimer(new QTimer(this))
    , m_shrinkTimer(new QTimer(this))
{
    new DockAdaptor(this);

    installEventFilter(this);

    // Smooth surface growth/shrink when a drop slot opens/closes, so the icons
    // never overflow behind the trash and the panel visually expands. Created
    // FIRST: resizeWindow() (possibly reached from a QML binding while the
    // engine loads) animates through this object, so it must exist beforehand.
    m_resizeAnimation = new QVariantAnimation(this);
    m_resizeAnimation->setDuration(250);
    m_resizeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_resizeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        setGeometry(v.toRect());
    });
    connect(m_resizeAnimation, &QVariantAnimation::finished, this, [this]() {
        qInfo() << "resize finished" << geometry().width() << "x" << geometry().height();
        updateLayerShell();
        XWindowInterface::instance()->setPanelWindow(this);
    });

    // With the private ~/.local prefix the FishUI module lives outside Qt's
    // default import paths. Qt6 does not honor QT_QML_IMPORT_PATH at runtime,
    // so register the prefix path explicitly; for a system install this simply
    // resolves back to the default Qt QML directory.
    engine()->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../lib64/qt6/qml"));
    engine()->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/qt6/qml"));

    // Wayland needs an alpha channel for the rounded translucent panel; the
    // original forced a matching X11 visual with setDefaultAlphaBuffer(false).
    QQuickWindow::setDefaultAlphaBuffer(true);
    setColor(Qt::transparent);

    setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);

    // The dock is a floating layer-shell surface anchored to the screen edge.
    // The exclusive zone replaces the X11 NET::Dock type and its extended
    // struts: maximized windows stop right at the dock's edge and auto-hide
    // modes release the space by setting the zone to zero.
    XWindowInterface::instance()->setPanelWindow(this);
    m_layerShell = LayerShellQt::Window::get(this);
    // Never take keyboard focus. With KeyboardInteractivityOnDemand a click on
    // the panel makes the layer surface the *active* window in KWin, which
    // breaks the dock's "click the active app's icon to minimize it" logic
    // (activeWindow() would resolve to the panel, not the app). Pointer events
    // are unaffected by this setting.
    m_layerShell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layerShell->setLayer(LayerShellQt::Window::LayerTop);

    engine()->rootContext()->setContextProperty("appModel", m_appModel);
    engine()->rootContext()->setContextProperty("process", new ProcessProvider(this));
    engine()->rootContext()->setContextProperty("Settings", m_settings);
    engine()->rootContext()->setContextProperty("mainWindow", this);
    engine()->rootContext()->setContextProperty("trash", m_trashManager);

    setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    setScreen(qApp->primaryScreen());
    setResizeMode(QQuickView::SizeRootObjectToView);
    initScreens();

    resizeWindow();
    onVisibilityChanged();

    m_showTimer->setSingleShot(true);
    m_showTimer->setInterval(200);
    connect(m_showTimer, &QTimer::timeout, this, [=] { setDockHidden(false); });

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(500);
    connect(m_hideTimer, &QTimer::timeout, this, &MainWindow::onHideTimeout);

    // Slightly longer than the QML opacity Behaviour (200 ms) so the panel is
    // fully transparent by the time the surface shrinks to the edge strip.
    m_shrinkTimer->setSingleShot(true);
    m_shrinkTimer->setInterval(260);
    connect(m_shrinkTimer, &QTimer::timeout, this, &MainWindow::onShrinkTimeout);

    // When the current window changes.
    connect(m_activity, &Activity::launchPadChanged, this, &MainWindow::onVisibilityChanged);
    connect(m_activity, &Activity::existsWindowMaximizedChanged, this, &MainWindow::onVisibilityChanged);

    // Screen change.
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &MainWindow::onPrimaryScreenChanged);
    // Adding or unplugging an output has to be handled too: the configured
    // screen may be the one that just disappeared.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &MainWindow::onScreensChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &MainWindow::onScreensChanged);
    bindScreenSignals();

    // Activity: the panel follows the current desktop. Independent of the
    // IntellHide state, which shrinks the surface instead of unmapping it.
    connect(PlasmaVirtualDesktop::self(), &PlasmaVirtualDesktop::currentDesktopChanged,
            this, &MainWindow::onVirtualDesktopChanged);
    connect(PlasmaVirtualDesktop::self(), &PlasmaVirtualDesktop::desktopRemoved,
            this, &MainWindow::onVirtualDesktopRemoved);
    onVirtualDesktopChanged();

    connect(m_appModel, &ApplicationModel::countChanged, this, &MainWindow::resizeWindow);
    connect(m_settings, &DockSettings::directionChanged, this, &MainWindow::onPositionChanged);
    connect(m_settings, &DockSettings::iconSizeChanged, this, &MainWindow::onIconSizeChanged);
    connect(m_settings, &DockSettings::visibilityChanged, this, &MainWindow::onVisibilityChanged);
    connect(m_settings, &DockSettings::styleChanged, this, &MainWindow::resizeWindow);
}

MainWindow::~MainWindow()
{
}

void MainWindow::add(const QString &desktop)
{
    m_appModel->addItem(desktop);
}

bool MainWindow::addDesktopFile(const QString &desktop)
{
    const QString path = desktop.startsWith("file://")
                             ? QUrl(desktop).toLocalFile()
                             : desktop;

    if (!path.endsWith(".desktop", Qt::CaseInsensitive) || !QFile::exists(path))
        return false;

    m_appModel->addItem(path);
    return true;
}

bool MainWindow::addDesktopFileAt(const QString &desktop, int index)
{
    const QString path = desktop.startsWith("file://")
                             ? QUrl(desktop).toLocalFile()
                             : desktop;

    if (!path.endsWith(".desktop", Qt::CaseInsensitive) || !QFile::exists(path))
        return false;

    qInfo() << "addDesktopFileAt" << path << "index=" << index;
    m_appModel->insertItem(path, index);
    return true;
}

void MainWindow::remove(const QString &desktop)
{
    m_appModel->removeItem(desktop);
}

bool MainWindow::pinned(const QString &desktop)
{
    return m_appModel->isDesktopPinned(desktop);
}

QRect MainWindow::primaryGeometry() const
{
    return geometry();
}

int MainWindow::direction() const
{
    return DockSettings::self()->direction();
}

int MainWindow::visibility() const
{
    return DockSettings::self()->visibility();
}

void MainWindow::setDirection(int direction)
{
    DockSettings::self()->setDirection(static_cast<DockSettings::Direction>(direction));
}

void MainWindow::setIconSize(int iconSize)
{
    DockSettings::self()->setIconSize(iconSize);
}

void MainWindow::setVisibility(int visibility)
{
    DockSettings::self()->setVisibility(static_cast<DockSettings::Visibility>(visibility));
}

int MainWindow::style() const
{
    return DockSettings::self()->style();
}

void MainWindow::setStyle(int style)
{
    DockSettings::self()->setStyle(static_cast<DockSettings::Style>(style));
}

void MainWindow::updateSize()
{
    resizeWindow();
}

void MainWindow::resizeToContent(int cellOffset)
{
    // The surface is whatever the current state needs: the full panel, or the
    // thin edge strip while auto-hidden. The drop-slot flow grows/shrinks the
    // panel by one cell through cellOffset.
    QRect end = m_dockHidden ? stripRect() : windowRect(cellOffset);

    qInfo() << "resizeToContent offset=" << cellOffset
            << "dir=" << m_settings->direction()
            << "style=" << m_settings->style()
            << "screen=" << screen()->geometry()
            << "rows=" << m_appModel->rowCount()
            << "winRect=" << windowRect(cellOffset)
            << "end=" << end
            << "geom=" << geometry();
    m_resizeAnimation->stop();
    m_resizeAnimation->setStartValue(geometry());
    m_resizeAnimation->setEndValue(end);
    m_resizeAnimation->start();
}

QRect MainWindow::windowRect(int cellOffset) const
{
    const QRect screenGeometry = screen()->geometry();
    const QRect availableGeometry = screen()->availableGeometry();

    bool isHorizontal = m_settings->direction() == DockSettings::Bottom;
    bool compositing = false;
    QQuickItem *item = qobject_cast<QQuickItem *>(rootObject());

    if (item) {
        compositing = item->property("compositing").toBool();
    }

    QSize newSize(0, 0);
    QPoint position(0, 0);
    int maxLength = isHorizontal ? screenGeometry.width() - m_settings->edgeMargins()
                                 : availableGeometry.height() - m_settings->edgeMargins();;

    // Add trash item. cellOffset additionally accounts for the row that is
    // visually collapsed while a reorder drag is held outside (one cell less)
    // or for the drop slot that opens during an external drag (+1 is the
    // regular trash cell).
    int appCount = m_appModel->rowCount() + 1 + cellOffset;
    int iconSize = m_settings->iconSize();
    iconSize += iconSize * 0.1;
    int length = appCount * iconSize;
    int margins = compositing ? DockSettings::self()->edgeMargins() / 2 : 0;

    if (length >= maxLength) {
        iconSize = (maxLength - (maxLength % appCount)) / appCount;
        length = appCount * iconSize;
    }

    switch (m_settings->style()) {
    case DockSettings::Round: {
        switch (m_settings->direction()) {
        case DockSettings::Left:
            newSize = QSize(iconSize, length);
            position.setX(screenGeometry.x() + margins);
            // Handle the top statusbar.
            position.setY(availableGeometry.y() + (availableGeometry.height() - newSize.height()) / 2);
            break;
        case DockSettings::Bottom:
            newSize = QSize(length, iconSize);
            position.setX(screenGeometry.x() + (screenGeometry.width() - newSize.width()) / 2);
            position.setY(screenGeometry.y() + screenGeometry.height() - newSize.height() - margins);
            break;
        case DockSettings::Right:
            newSize = QSize(iconSize, length);
            position.setX(screenGeometry.x() + screenGeometry.width() - newSize.width() - margins);
            position.setY(availableGeometry.y() + (availableGeometry.height() - newSize.height()) / 2);
            break;
        default:
            break;
        }

        break;
    }
    case DockSettings::Straight: {
        switch (m_settings->direction()) {
        case DockSettings::Left:
            newSize = QSize(iconSize, screenGeometry.height());
            position.setX(screenGeometry.x());
            position.setY(screenGeometry.y());
            break;
        case DockSettings::Bottom:
            newSize = QSize(screenGeometry.width(), iconSize);
            position.setX(screenGeometry.x());
            position.setY(screenGeometry.y() + screenGeometry.height() - newSize.height());
            break;
        case DockSettings::Right:
            newSize = QSize(iconSize, screenGeometry.height());
            position.setX(screenGeometry.x() + screenGeometry.width() - newSize.width());
            position.setY(screenGeometry.y() + screenGeometry.height() - newSize.height());
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }

    return QRect(position, newSize);
}

QRect MainWindow::stripRect() const
{
    QRect rect = windowRect();
    const bool horizontal = m_settings->direction() == DockSettings::Bottom;

    if (horizontal)
        rect.setHeight(2);
    else
        rect.setWidth(2);

    return rect;
}

void MainWindow::resizeWindow()
{
    // Keep the edge strip while the panel is hidden: a geometry refresh (new
    // app, icon size change, ...) must not re-expand an invisible panel.
    qInfo() << "resizeWindow() hidden=" << m_dockHidden
            << "cur=" << geometry();
    QRect end = m_dockHidden ? stripRect() : windowRect();

    // Animate instead of snapping so e.g. an unpin re-centering glides instead
    // of jumping; only the very first geometry (empty) is applied directly.
    if (m_resizeAnimation)
        m_resizeAnimation->stop();
    if (!m_resizeAnimation || geometry().isEmpty()) {
        setGeometry(end);
    } else {
        m_resizeAnimation->setStartValue(geometry());
        m_resizeAnimation->setEndValue(end);
        m_resizeAnimation->start();
    }

    updateLayerShell();

    // The panel surface is re-created by Qt whenever the window is hidden and
    // re-exposed; refresh the anchor the taskbar entries attach to.
    XWindowInterface::instance()->setPanelWindow(this);
}

void MainWindow::initScreens()
{
    // An empty Screen setting means "whatever Plasma calls primary", which is
    // also the fallback when the configured output is not connected: a dock
    // pinned to a monitor that is gone should not disappear.
    const QString wanted = m_settings->screenName();
    QScreen *target = nullptr;

    if (!wanted.isEmpty()) {
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (QScreen *candidate : screens) {
            if (candidate->name() == wanted) {
                target = candidate;
                break;
            }
        }
        if (!target)
            qWarning() << "MainWindow: output" << wanted << "is not connected, using the primary screen.";
    }

    setScreen(target ? target : qGuiApp->primaryScreen());
}

void MainWindow::onScreensChanged()
{
    initScreens();
    bindScreenSignals();
    resizeWindow();
}

void MainWindow::onVirtualDesktopChanged()
{
    QScreen *output = screen();
    const QString current = PlasmaVirtualDesktop::self()->currentDesktop(output ? output->name() : QString());

    // Empty means Plasma did not tell us (interface missing, or not announced
    // yet). Staying put is better than hiding the panel on a wrong guess.
    if (current.isEmpty() || current == m_desktop)
        return;

    m_desktop = current;

    qInfo() << "MainWindow: following desktop" << current
            << "on output" << (output ? output->name() : QStringLiteral("(none)"));

    // Unmap and map again: the layer surface is pinned to the desktop it was
    // first mapped on, and re-mapping puts it on the current one. Deferred by an
    // event loop turn so the unmap is not undone before it happens.
    setVisible(false);
    QTimer::singleShot(0, this, [this] {
        setVisible(true);
        resizeWindow();
    });
}

void MainWindow::onVirtualDesktopRemoved(const QString &id)
{
    // The desktop the panel sits on is gone. Forget it so the next activation
    // re-adopts whatever is current.
    if (id == m_desktop) {
        m_desktop.clear();
        onVirtualDesktopChanged();
    }
}

void MainWindow::updateLayerShell()
{
    if (!m_layerShell)
        return;

    bool compositing = false;
    QQuickItem *item = qobject_cast<QQuickItem *>(rootObject());

    if (item) {
        compositing = item->property("compositing").toBool();
    }

    const QRect rect = windowRect();
    const bool round = m_settings->style() == DockSettings::Round;
    // Floating panels keep a small gap to the screen edge, like the original
    // X11 position math did; straight panels hug the edge everywhere.
    const int gap = compositing && round ? m_settings->edgeMargins() / 2 : 0;

    LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::AnchorNone;
    QMargins margins;

    switch (m_settings->direction()) {
    case DockSettings::Left:
        anchors |= LayerShellQt::Window::AnchorLeft;
        margins.setLeft(gap);
        if (!round) {
            anchors.setFlag(LayerShellQt::Window::AnchorTop);
            anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        }
        break;
    case DockSettings::Bottom:
        anchors |= LayerShellQt::Window::AnchorBottom;
        margins.setBottom(gap);
        if (!round) {
            anchors.setFlag(LayerShellQt::Window::AnchorLeft);
            anchors.setFlag(LayerShellQt::Window::AnchorRight);
        }
        break;
    case DockSettings::Right:
        anchors |= LayerShellQt::Window::AnchorRight;
        margins.setRight(gap);
        if (!round) {
            anchors.setFlag(LayerShellQt::Window::AnchorTop);
            anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        }
        break;
    default:
        break;
    }

    m_layerShell->setAnchors(anchors);
    m_layerShell->setMargins(margins);

    // Reserve the screen edge like the old extended struts: only when the
    // dock is supposed to stay visible. Auto-hide modes hand the space back,
    // leaving a thin strip the mouse can cross to reveal the dock.
    if (m_settings->visibility() == DockSettings::AlwaysShow || m_activity->launchPad()) {
        LayerShellQt::Window::Anchor edge = LayerShellQt::Window::AnchorNone;
        int zone = 0;

        // The surface already floats `gap` pixels off the screen edge. KWin
        // measures the exclusive zone from the screen edge and subtracts that
        // margin as well, so a padding of exactly `gap` leaves the same space
        // above the dock as below it - doubling it would double the top gap.
        const int zonePadding = gap;

        switch (m_settings->direction()) {
        case DockSettings::Left:
            edge = LayerShellQt::Window::AnchorLeft;
            zone = rect.width() + zonePadding;
            break;
        case DockSettings::Bottom:
            edge = LayerShellQt::Window::AnchorBottom;
            zone = rect.height() + zonePadding;
            break;
        case DockSettings::Right:
            edge = LayerShellQt::Window::AnchorRight;
            zone = rect.width() + zonePadding;
            break;
        default:
            break;
        }

        m_layerShell->setExclusiveEdge(edge);
        m_layerShell->setExclusiveZone(zone);
    } else {
        m_layerShell->setExclusiveEdge(LayerShellQt::Window::AnchorNone);
        m_layerShell->setExclusiveZone(0);
    }
}

void MainWindow::setDockHidden(bool hidden)
{
    if (m_dockHidden == hidden)
        return;

    m_dockHidden = hidden;
    qInfo() << "dock hidden ->" << hidden;

    if (hidden) {
        // Auto-hide: fade the QML layer out first (opacity is bound to
        // dockHidden) and only then shrink to the ~2px edge strip. Resizing a
        // layer surface while its content is still semi-visible makes the
        // compositor show artifacts; a fully transparent shrink does not. The
        // mouse can still cross the strip to reveal the panel.
        m_shrinkTimer->start();
    } else {
        // Grow back to the full panel before the fade-in starts, so the
        // content fades in over the complete surface instead of snapping.
        m_shrinkTimer->stop();
        setGeometry(windowRect());
    }

    XWindowInterface::instance()->setPanelWindow(this);

    emit dockHiddenChanged();
}

void MainWindow::onShrinkTimeout()
{
    if (!m_dockHidden)
        return;

    setGeometry(stripRect());
}

void MainWindow::onPrimaryScreenChanged(QScreen *screen)
{
    initScreens();
    setScreen(screen);
    bindScreenSignals();
    resizeWindow();
}

void MainWindow::bindScreenSignals()
{
    // QQuickView follows the primary screen by itself, so the signals of the
    // output being left behind have to be dropped explicitly - otherwise the
    // dock keeps listening to a screen that no longer exists.
    if (m_boundScreen)
        m_boundScreen->disconnect(this);

    m_boundScreen = screen();
    if (!m_boundScreen)
        return;

    connect(m_boundScreen, &QScreen::virtualGeometryChanged, this, &MainWindow::resizeWindow);
    connect(m_boundScreen, &QScreen::geometryChanged, this, &MainWindow::resizeWindow);
}

void MainWindow::onPositionChanged()
{
    initScreens();
    updateLayerShell();
    resizeWindow();

    emit directionChanged();
    onVisibilityChanged();
}

void MainWindow::onIconSizeChanged()
{
    setGeometry(windowRect());
    updateLayerShell();

    emit iconSizeChanged();
}

void MainWindow::onVisibilityChanged()
{
    emit visibilityChanged();

    if (m_activity->launchPad()) {
        m_hideTimer->stop();
        updateLayerShell();
        setDockHidden(false);
        setVisible(true);
        return;
    }

    // Always show
    // Must remain displayed when launchpad is opened.
    if (m_settings->visibility() == DockSettings::AlwaysShow) {
        m_hideTimer->stop();

        updateLayerShell();
        setDockHidden(false);
        setVisible(true);
    }

    if (m_settings->visibility() == DockSettings::IntellHide) {
        updateLayerShell();
        setVisible(true);

        if (m_activity->existsWindowMaximized() && !m_hideBlocked) {
            setDockHidden(true);
        } else {
            setDockHidden(false);
        }
    }

    // Always hide
    if (m_settings->visibility() == DockSettings::AlwaysHide) {
        updateLayerShell();
        setVisible(true);
        setDockHidden(!m_hideBlocked);
    }
}

void MainWindow::onHideTimeout()
{
    if (m_activity->launchPad())
        return;

    if (m_settings->visibility() == DockSettings::AlwaysShow)
        return;

    if (m_settings->visibility() == DockSettings::IntellHide
            && !m_activity->existsWindowMaximized()) {
        return;
    }

    setDockHidden(true);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    switch (e->type()) {
    case QEvent::MouseButtonPress:
        qInfo() << "evfilter: press hidden=" << m_dockHidden;
        break;
    case QEvent::Enter:
        qInfo() << "evfilter: enter hidden=" << m_dockHidden;
        m_hideTimer->stop();
        m_hideBlocked = true;

        // The mouse crossed the edge strip, reveal the panel.
        if (m_dockHidden && !m_showTimer->isActive())
            m_showTimer->start();
        break;
    case QEvent::Leave:
        qInfo() << "evfilter: leave hidden=" << m_dockHidden;
        m_hideBlocked = false;

        // The auto-hide timer only applies to the hiding visibilities; the
        // original guarded this through the fake window, which only existed in
        // those modes.
        if (!m_dockHidden
                && (m_settings->visibility() == DockSettings::AlwaysHide
                    || m_settings->visibility() == DockSettings::IntellHide)) {
            m_hideTimer->start();
        }
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove:
        m_hideTimer->stop();
        if (m_dockHidden)
            setDockHidden(false);
        break;
    case QEvent::DragLeave:
    case QEvent::Drop:
        m_hideTimer->stop();
        break;
    default:
        break;
    }

    return QQuickView::eventFilter(obj, e);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    emit primaryGeometryChanged();

    QQuickView::resizeEvent(e);
}
