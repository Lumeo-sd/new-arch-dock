#include "windowhelper.h"

#include <QGuiApplication>

WindowHelper::WindowHelper(QObject *parent)
    : QObject(parent)
{
}

bool WindowHelper::isWayland() const
{
    return true;
}

// The CutefishOS dock still reads this property to decide whether the rounded
// background, translucency and blur make sense. The X11 compositor probe went
// away with the X11 backends: a Wayland session is always composited (KWin),
// and X11 is outside the scope of this port, so report true either way.
bool WindowHelper::compositing() const
{
    const QByteArray platform = QGuiApplication::platformName().toLower().toLatin1();
    return platform == QByteArrayLiteral("wayland")
            || platform == QByteArrayLiteral("xcb");
}

void WindowHelper::startSystemMove(QWindow *w)
{
    if (w)
        w->startSystemMove();
}

void WindowHelper::startSystemResize(QWindow *w, Qt::Edges edges)
{
    if (w)
        w->startSystemResize(edges);
}

void WindowHelper::minimizeWindow(QWindow *w)
{
    if (w)
        w->showMinimized();
}

bool WindowHelper::popupMenuVisible() const
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (const QWindow *window : windows) {
        if (window->isVisible() && (window->flags() & Qt::Popup) == Qt::Popup)
            return true;
    }

    return false;
}
