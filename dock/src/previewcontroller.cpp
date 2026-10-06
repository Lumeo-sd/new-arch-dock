#include "previewcontroller.h"

#include "applicationmodel.h"
#include "docksettings.h"
#include "mainwindow.h"

#include <LayerShellQt/window.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QDebug>

PreviewController::PreviewController(MainWindow *view)
    : QObject(view)
    , m_dock(view)
{
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(200);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] { hidePreview(); });
}

// Pixel-measured on 1920x1080/eDP-1: KWin parks a bottom-anchored overlay
// surface above where screen-edge margins alone say it should be - the dock's
// exclusive zone (rect.height() + gap, set by MainWindow) is carved out of
// the placement area of later layer surfaces. The preview margin compensates
// so the popup lands at a tooltip-like height above the dock (bottom ~1005
// with the dock top at 1017): measured carve 67px, target gap folded in.
// Verified by row-brightness measurement of screenshots.
static int exclusiveShift()
{
    return 59;
}

PreviewController::~PreviewController() = default;

void PreviewController::initialize()
{
    // Share the dock's QQmlEngine so engine-level context properties
    // (appModel, Settings, mainWindow, ...) are visible to the popup too.
    m_view = new QQuickView(m_dock->engine(), nullptr);
    m_view->setColor(Qt::transparent);
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

    m_dock->engine()->rootContext()->setContextProperty(QStringLiteral("preview"), this);

    m_layerShell = LayerShellQt::Window::get(m_view);
    m_layerShell->setLayer(LayerShellQt::Window::LayerOverlay);
    m_layerShell->setScope(QStringLiteral("cutefish-dock-preview"));
    m_layerShell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layerShell->setExclusiveZone(0);
    m_layerShell->setCloseOnDismissed(false);

    if (QScreen *screen = m_dock->screen()) {
        m_layerShell->setScreen(screen);
        m_view->setScreen(screen);
    }

    m_view->setWidth(640);
    m_view->setHeight(240);

    // Anchor the surface immediately, before first show: after the compositor
    // configures a layer surface it does not reliably honour a later anchors
    // change, so the anchor set is chosen up front.
    //
    // Placement strategy: the preview surface copies the dock's own placement
    // (same screen-edge anchors, same screen-edge margins) and is sized to
    // extend OVER the dock by the popup depth. The popup is then drawn flush
    // against the dock (popup bottom == dock top for a bottom dock). This
    // keeps the strip glued to the dock no matter how the compositor stacks
    // the dock's exclusive zone: an earlier scheme positioned the surface
    // purely from screen-edge margins and KWin parked it ~70px too high,
    // above the reserved zone.
    const QRect screen = m_dock->screen()->geometry();
    const QRect dockRect = m_dock->dockRect();
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;
    QSize size;

    if (m_dock->direction() == DockSettings::Bottom) {
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorLeft;
        anchors |= LayerShellQt::Window::AnchorRight;
        margins.setBottom(screen.height() - (dockRect.y() + dockRect.height()) - exclusiveShift());
        size = QSize(screen.width(), dockRect.height());
    } else if (m_dock->direction() == DockSettings::Left) {
        anchors |= LayerShellQt::Window::AnchorLeft;
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorBottom;
        margins.setLeft(dockRect.x());
        size = QSize(dockRect.width(), screen.height());
    } else {
        anchors |= LayerShellQt::Window::AnchorRight;
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorBottom;
        margins.setRight(screen.width() - (dockRect.x() + dockRect.width()));
        size = QSize(dockRect.width(), screen.height());
    }

    m_layerShell->setAnchors(anchors);
    m_layerShell->setMargins(margins);
    m_layerShell->setDesiredSize(size);
    m_view->resize(size);

    m_view->setSource(QUrl(QStringLiteral("qrc:/qml/PreviewPopup.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const QQmlError &err : m_view->errors()) {
            qWarning() << "PreviewPopup QML error:" << err.toString();
        }
    }

    // Swallowed everything except the popup rect: block input across the
    // whole surface until the user hovers something multi-windowed.
    m_view->setMask(QRegion(0, 0, 1, 1));
    m_view->show();
}

void PreviewController::showPreview(const QString &appId, qreal iconCenterX, qreal iconCenterY)
{
    if (appId.isEmpty()) {
        hidePreviewDelayed();
        return;
    }

    const bool sameApp = (m_appId == appId);
    // AppItem passes appItem.mapToGlobal(), but on a layer-shell surface Qt
    // never learns the compositor position, so that is window-local (the
    // dock window believes it is at 0,0). Translate to screen space with the
    // dock rect, which layout() assumes throughout.
    const QRect dockRect = m_dock->dockRect();
    m_appId = appId;
    m_iconCenterX = iconCenterX + dockRect.x();
    m_iconCenterY = iconCenterY + dockRect.y();
    qInfo() << "preview.showPreview app=" << appId << "at" << iconCenterX << iconCenterY;

    if (!sameApp) {
        // Popup QML listens on appIdChanged to rebuild its ListModel and then
        // reports the exact content size back through setContentSize().
        emit appIdChanged();
    }

    if (!m_visible) {
        m_visible = true;
        emit visibleChanged(true);
    }

    qInfo() << "preview.show app=" << m_appId << "iconCenter=" << m_iconCenterX
            << m_iconCenterY << "viewGeom=" << m_view->geometry();

    layout();
    cancelHide();
}

void PreviewController::hidePreview()
{
    if (!m_visible && m_appId.isEmpty()) {
        clear();
        return;
    }

    m_visible = false;
    m_hovered = false;
    m_appId.clear();
    m_contentX = m_contentY = 0;
    m_iconCenterX = m_iconCenterY = 0;

    m_view->setMask(QRegion(0, 0, 1, 1));

    emit visibleChanged(false);
    emit appIdChanged();
    emit layoutChanged();
}

void PreviewController::hidePreviewDelayed()
{
    m_hideTimer.start();
}

void PreviewController::cancelHide()
{
    m_hideTimer.stop();
}

void PreviewController::setHovered(bool hovered)
{
    m_hovered = hovered;

    if (hovered) {
        cancelHide();
    } else {
        hidePreviewDelayed();
    }
}

void PreviewController::setContentSize(qreal width, qreal height)
{
    if (qFuzzyCompare(m_contentW, width) && qFuzzyCompare(m_contentH, height)) {
        applyInputRegion();
        return;
    }

    m_contentW = width;
    m_contentH = height;

    layout();
}

void PreviewController::activate(int windowIndex)
{
    if (ApplicationModel *model = m_dock->appModel())
        model->activateWindowForApp(m_appId, windowIndex);

    hidePreview();
}

void PreviewController::closeWindow(int windowIndex)
{
    if (ApplicationModel *model = m_dock->appModel())
        model->closeWindowForApp(m_appId, windowIndex);

    // Keep the popup: closing one of several windows should shrink the list;
    // the window model update and next setContentSize() will reflow it.
    if (m_appId.isEmpty())
        hidePreview();
}

void PreviewController::layout()
{
    if (!m_view || !m_layerShell) {
        return;
    }

    const QRect screen = m_dock->screen()->geometry();
    const QRect dockRect = m_dock->dockRect();
    const int dir = m_dock->direction();
    const qreal pad = 8;
    // Breathing room between the popup and the dock edge.
    const int gap = 4;

    LayerShellQt::Window::Anchors anchors;
    QMargins margins;
    QSize size;
    qint32 exclusion = 0;

    if (dir == DockSettings::Bottom) {
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorLeft;
        anchors |= LayerShellQt::Window::AnchorRight;
        // Same screen-edge margin as the dock itself: the surface bottom
        // lands exactly on the dock bottom, and the surface grows upward
        // over the dock by dock height + gap + popup height. The popup sits
        // at the surface top, so its bottom == dock top - gap.
        margins.setBottom(screen.height() - (dockRect.y() + dockRect.height()) - exclusiveShift());
        size = QSize(screen.width(),
                     dockRect.height() + gap + (m_contentH > 0 ? qRound(m_contentH) : 0));
        m_contentX = qBound(pad, m_iconCenterX - m_contentW / 2, screen.width() - m_contentW - pad);
        m_contentY = 0;
    } else if (dir == DockSettings::Left) {
        anchors |= LayerShellQt::Window::AnchorLeft;
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorBottom;
        margins.setLeft(dockRect.x());
        size = QSize(dockRect.width() + gap + (m_contentW > 0 ? qRound(m_contentW) : 0),
                     screen.height());
        m_contentX = dockRect.width() + gap;
        m_contentY = qBound(pad, m_iconCenterY - m_contentH / 2, screen.height() - m_contentH - pad);
    } else { // Right
        anchors |= LayerShellQt::Window::AnchorRight;
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorBottom;
        margins.setRight(screen.width() - (dockRect.x() + dockRect.width()));
        size = QSize(dockRect.width() + gap + (m_contentW > 0 ? qRound(m_contentW) : 0),
                     screen.height());
        m_contentX = dockRect.width() + gap;
        m_contentY = qBound(pad, m_iconCenterY - m_contentH / 2, screen.height() - m_contentH - pad);
    }

    // Anchors are chosen once in initialize(); swapping them after configure
    // is unreliable on some compositors.  Margins publish the exact band the
    // strip occupies and may change on resize, so they are safe to rewrite.
    m_layerShell->setMargins(margins);
    m_layerShell->setDesiredSize(size);
    m_view->resize(size);
    m_layerShell->setExclusiveZone(exclusion);

    qInfo() << "preview.layout dir=" << dir << "dockRect=" << dockRect
            << "size=" << size
            << "contentX=" << m_contentX << "contentY=" << m_contentY
            << "contentW=" << m_contentW << "contentH=" << m_contentH;

    applyInputRegion();
    emit layoutChanged();
}

void PreviewController::applyInputRegion()
{
    if (!m_view) {
        return;
    }

    qInfo() << "preview.applyMask visible=" << m_visible << "appId=" << m_appId
            << "w=" << m_contentW << "h=" << m_contentH << "maskY=" << m_contentY;
    if (!m_visible || m_appId.isEmpty() || m_contentW <= 0 || m_contentH <= 0) {
        // Hidden: block all meaningful input with a 1x1 region in the corner.
        // NOTE: an empty QRegion clears the mask, which makes the whole
        // surface accept input - the opposite of what is wanted here.
        m_view->setMask(QRegion(0, 0, 1, 1));
        return;
    }

    // Visible: the popup rect plus a margin on the sides AWAY from the dock.
    // The margin towards the dock side is deliberately zero: the surface
    // extends over the dock there (transparently), and claiming input in
    // that band would steal clicks from the dock icons. Travelling from an
    // icon into the popup only crosses a few pixels of unclaimed surface,
    // covered by the hide delay. Mirrors Krema's updateInputRegion().
    const int sideMargin = 40;
    const int dir = m_dock->direction();
    const int cx = qRound(m_contentX);
    const int cy = qRound(m_contentY);
    const int cw = qRound(m_contentW);
    const int ch = qRound(m_contentH);

    QRegion region;
    if (dir == DockSettings::Bottom) {
        // Popup at the surface top; below it is the dock (no mask there).
        const int x = qMax(0, cx - sideMargin);
        const int right = cx + cw + sideMargin;
        const int y = qMax(0, cy - sideMargin);
        region = QRegion(x, y, right - x, cy + ch - y);
    } else {
        // Vertical docks: popup spans the dock-facing edge; extend along the
        // dock axis and away from the dock, never over it.
        const int y = qMax(0, cy - sideMargin);
        const int bottom = cy + ch + sideMargin;
        if (dir == DockSettings::Left)
            region = QRegion(cx, y, cw + sideMargin, bottom - y);
        else // Right dock: the dock sits at the surface right edge.
            region = QRegion(cx - sideMargin, y, cw + sideMargin, bottom - y);
    }
    m_view->setMask(region);
    qInfo() << "preview.maskRegion=" << region.boundingRect();
}

void PreviewController::clear()
{
    applyInputRegion();
}
