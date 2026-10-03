#ifndef OVERVIEWMANAGER_H
#define OVERVIEWMANAGER_H

#include <QObject>
#include <QString>

// Opens the Plasma Activities overview - the desktop overview where windows can
// be moved between desktops and new desktops added.
//
// Plasma has no direct API for it: KWin 6 has no /Overview object and the KWin
// scripting API never mentions it. What does exist is the global accelerator
// daemon, which can replay any shortcut a component registered under a unique
// name:
//
//   org.kde.kglobalaccel /component/kwin
//       org.kde.kglobalaccel.Component.invokeShortcut("Overview")
//
// Asking kglobalaccel rather than sending key events matters because it
// resolves the name against the live binding: reassigning Overview in KDE
// System Settings keeps working, with no config file to parse and no keycode
// translation to get wrong. It also needs no helper daemon, no /dev/uinput and
// no privileged access.
class OverviewManager : public QObject
{
    Q_OBJECT
    // True when pressing the button would do something.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // The button's tooltip: what it opens, or why it cannot.
    Q_PROPERTY(QString status READ status NOTIFY availableChanged)

public:
    explicit OverviewManager(QObject *parent = nullptr);

    Q_INVOKABLE void showOverview();

    bool available() const;
    QString status() const;

Q_SIGNALS:
    void availableChanged();

private:
    // Whether kglobalaccel owns a component object for kwin on the session bus.
    static bool serviceRegistered();

    // Re-checks the service. kglobalaccel is registered by the session, so it
    // can well appear after the dock does.
    void refreshAvailability();

    bool m_available = false;
    QString m_reason;
};

#endif // OVERVIEWMANAGER_H
