#ifndef WINDOWHELPER_H
#define WINDOWHELPER_H

#include <QObject>
#include <QWindow>

class WindowHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool wayland READ isWayland CONSTANT)
    // The CutefishOS dock still binds its background styling to this property.
    // The Qt6/Wayland port dropped the X11 compositor probing: a Wayland
    // session runs under a compositor by definition (KWin), so it is always
    // true there. It is kept as a plain bool so the callers that used to
    // check "am I composited?" do not have to change.
    Q_PROPERTY(bool compositing READ compositing CONSTANT)

public:
    explicit WindowHelper(QObject *parent = nullptr);

    bool isWayland() const;
    bool compositing() const;

    Q_INVOKABLE void startSystemMove(QWindow *w);
    Q_INVOKABLE void startSystemResize(QWindow *w, Qt::Edges edges);

    Q_INVOKABLE void minimizeWindow(QWindow *w);

    // Whether one of this application's menus is on screen. A menu owns the
    // interaction while it is up, so the panels underneath it must not answer
    // hover with a tooltip.
    Q_INVOKABLE bool popupMenuVisible() const;

};

#endif // WINDOWHELPER_H
