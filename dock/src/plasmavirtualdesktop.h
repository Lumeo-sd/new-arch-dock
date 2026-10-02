#ifndef PLASMAVIRTUALDESKTOP_H
#define PLASMAVIRTUALDESKTOP_H

#include <QHash>
#include <QObject>
#include <QString>

namespace KWayland {
namespace Client {
class PlasmaVirtualDesktop;
class PlasmaVirtualDesktopManagement;
class Registry;
}
}

// Thin wrapper over org_kde_plasma_virtual_desktop_management.
//
// The dock has to know which desktop is active on the output it lives on: a
// layer-shell surface belongs to the desktop it was mapped on, so following a
// desktop switch means unmapping and mapping the window again.
//
// Plasma reports activity per output, which is what a panel needs - with two
// monitors showing two different desktops, "the current desktop" is ambiguous.
class PlasmaVirtualDesktop : public QObject
{
    Q_OBJECT
public:
    static PlasmaVirtualDesktop *self();

    // Active desktop id for the given output, or an empty string when the
    // interface is unavailable or the output has no activity assigned.
    QString currentDesktop(const QString &outputName = QString()) const;

    bool isAvailable() const;

Q_SIGNALS:
    // The active desktop of some output changed, or the interface just became
    // available. Connectors re-read currentDesktop().
    void currentDesktopChanged();

    // A desktop went away. Its id is given so a listener can notice if the one
    // it was on disappeared.
    void desktopRemoved(const QString &id);

private:
    explicit PlasmaVirtualDesktop(QObject *parent = nullptr);

    void setupWaylandConnection();
    // (Re)builds the id -> desktop map and hooks up the per-desktop signals.
    void bindDesktops();

    KWayland::Client::Registry *m_registry = nullptr;
    KWayland::Client::PlasmaVirtualDesktopManagement *m_management = nullptr;
    QHash<QString, KWayland::Client::PlasmaVirtualDesktop *> m_desktops;
};

#endif // PLASMAVIRTUALDESKTOP_H
