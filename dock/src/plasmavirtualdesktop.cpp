#include "plasmavirtualdesktop.h"

#include <QDebug>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/plasmavirtualdesktop.h>
#include <KWayland/Client/registry.h>

// No using-directive here: this class is called PlasmaVirtualDesktop too, and
// KWayland::Client exports one under the same name.

static PlasmaVirtualDesktop *INSTANCE = nullptr;

PlasmaVirtualDesktop *PlasmaVirtualDesktop::self()
{
    if (!INSTANCE)
        INSTANCE = new PlasmaVirtualDesktop();

    return INSTANCE;
}

PlasmaVirtualDesktop::PlasmaVirtualDesktop(QObject *parent)
    : QObject(parent)
{
    setupWaylandConnection();
}

bool PlasmaVirtualDesktop::isAvailable() const
{
    return m_management && m_management->isValid();
}

QString PlasmaVirtualDesktop::currentDesktop(const QString &outputName) const
{
    if (!isAvailable())
        return QString();

    // Per output when we know it: with several monitors each can show a
    // different activity, and a panel on output B should follow output B.
    if (!outputName.isEmpty()) {
        KWayland::Client::PlasmaVirtualDesktop *desktop =
            m_management->currentDesktopByOutputName(outputName.toUtf8());
        return desktop ? desktop->id() : QString();
    }

    const QList<KWayland::Client::PlasmaVirtualDesktop *> desktops = m_management->desktops();
    for (KWayland::Client::PlasmaVirtualDesktop *desktop : desktops) {
        if (desktop->isActive())
            return desktop->id();
    }

    return QString();
}

void PlasmaVirtualDesktop::setupWaylandConnection()
{
    // Same QtWayland connection XWindowInterface uses; fromApplication()
    // returns nullptr off Wayland, which just disables the feature.
    KWayland::Client::ConnectionThread *connection =
        KWayland::Client::ConnectionThread::fromApplication(this);
    if (!connection) {
        qWarning() << "PlasmaVirtualDesktop: not running on Wayland, "
                      "the dock will not follow activities.";
        return;
    }

    m_registry = new KWayland::Client::Registry(this);
    m_registry->create(connection);
    m_registry->setup();

    connect(m_registry, &KWayland::Client::Registry::interfaceAnnounced, this,
            [this](QByteArray interface, quint32 name, quint32 version) {
                if (interface != QByteArrayLiteral("org_kde_plasma_virtual_desktop_management"))
                    return;

                m_management = m_registry->createPlasmaVirtualDesktopManagement(name, version, this);

                // Connected once, here: bindDesktops() runs again on every
                // change, and connecting inside it would stack up duplicates.
                connect(m_management, &KWayland::Client::PlasmaVirtualDesktopManagement::desktopCreated,
                        this, [this](const QString &id, quint32 position) {
                            Q_UNUSED(id)
                            Q_UNUSED(position)
                            bindDesktops();
                        });

                connect(m_management, &KWayland::Client::PlasmaVirtualDesktopManagement::desktopRemoved,
                        this, [this](const QString &id) {
                            bindDesktops();
                            emit desktopRemoved(id);
                        });

                bindDesktops();
            });
}

void PlasmaVirtualDesktop::bindDesktops()
{
    // The desktop objects belong to the management object, so only drop our
    // references - deleting them here would free them twice.
    m_desktops.clear();

    if (!isAvailable())
        return;

    const QList<KWayland::Client::PlasmaVirtualDesktop *> desktops = m_management->desktops();
    for (KWayland::Client::PlasmaVirtualDesktop *desktop : desktops) {
        m_desktops.insert(desktop->id(), desktop);

        // An activation means the current desktop of some output changed. The
        // signal carries no output name, so listeners re-read.
        connect(desktop, &KWayland::Client::PlasmaVirtualDesktop::activated,
                this, &PlasmaVirtualDesktop::currentDesktopChanged);

        // Which desktop an output shows arrives after binding, not with the
        // desktop list - without this a client that starts up has to wait for
        // the first switch before it learns where it is.
        connect(desktop, &KWayland::Client::PlasmaVirtualDesktop::outputEntered,
                this, [this](const QString &outputName) {
                    Q_UNUSED(outputName)
                    emit currentDesktopChanged();
                });
    }

    // Announced and set up: let the dock find out where it is.
    emit currentDesktopChanged();
}
