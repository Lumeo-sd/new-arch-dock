#ifndef OVERVIEWMANAGER_H
#define OVERVIEWMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

// Opens the Plasma Activities overview - the desktop overview where windows can
// be moved between desktops and new desktops added.
//
// Plasma exposes no API for it. KWin 6 has no /Overview object, the KWin
// scripting API never mentions it, and org.kde.kglobalaccel has no
// invokeShortcut, so a client cannot ask KWin to open it and cannot ask
// kglobalaccel to replay the shortcut either.
//
// The only thing that does work is sending the key the user configured for it
// ([kwin] Overview in kglobalshortcutsrc, "Meta+W" by default). ydotool injects
// that through /dev/uinput, which is below the compositor, so KWin receives it
// exactly as if the user had pressed it - and unlike xdotool this works on
// Wayland, because nothing X11 is involved.
//
// When ydotool is not installed the button stays in place and says why in its
// tooltip instead of failing silently. Run tools/enable-overview-button.sh.
class OverviewManager : public QObject
{
    Q_OBJECT
    // True when pressing the button would do something.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // The button's tooltip: the shortcut it will send, or what is missing.
    Q_PROPERTY(QString status READ status NOTIFY availableChanged)

public:
    explicit OverviewManager(QObject *parent = nullptr);

    Q_INVOKABLE void showOverview();

    bool available() const;
    QString status() const;

Q_SIGNALS:
    void availableChanged();

private:
    // What [kwin] Overview is bound to, e.g. "Meta+W". Empty when unset.
    QString configuredShortcut() const;
    // "Meta+W" -> the ydotool keycode tokens for it. Empty for a key we cannot
    // map. See the implementation for why keycodes and not names.
    static QStringList toYdotoolEvents(const QString &shortcut);
    // Missing tool or missing /dev/uinput - the reason the button is dead.
    QString unavailableReason() const;

    // ydotool 1.x needs its daemon; without it every call fails silently. The socket
    // is the hidden ".ydotool_socket", the name ydotool itself reports.
    static bool daemonRunning();

    bool m_uinput = false;
};

#endif // OVERVIEWMANAGER_H
