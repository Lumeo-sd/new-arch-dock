#include "overviewmanager.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QTimer>

namespace {

const QString kService = QStringLiteral("org.kde.kglobalaccel");
const QString kPath = QStringLiteral("/component/kwin");
const QString kInterface = QStringLiteral("org.kde.kglobalaccel.Component");
// kwin registers its shortcuts under this unique name, and "Overview" is the
// entry Plasma binds to Activities. Asking for it by name is what makes a
// reassigned shortcut keep working without the dock reading anything.
const QString kShortcut = QStringLiteral("Overview");

} // namespace

OverviewManager::OverviewManager(QObject *parent)
    : QObject(parent)
{
    refreshAvailability();
    // kglobalaccel is registered by the session and may not be up when the dock
    // starts, so keep a cheap check running rather than deciding once.
    auto *timer = new QTimer(this);
    timer->setInterval(4000);
    connect(timer, &QTimer::timeout, this, &OverviewManager::refreshAvailability);
    timer->start();
}

bool OverviewManager::serviceRegistered()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusConnectionInterface *iface = bus.interface();
    return iface && iface->isServiceRegistered(kService);
}

void OverviewManager::refreshAvailability()
{
    const bool ok = serviceRegistered();

    if (ok == m_available)
        return;

    m_available = ok;
    m_reason = ok ? QString() : tr("kglobalaccel is not available");
    emit availableChanged();
}

bool OverviewManager::available() const
{
    return m_available;
}

QString OverviewManager::status() const
{
    return m_available ? tr("Activities overview") : m_reason;
}

void OverviewManager::showOverview()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!serviceRegistered()) {
        refreshAvailability();
        qWarning() << "overview:" << m_reason;
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kPath, kInterface, QStringLiteral("invokeShortcut"));
    call << QVariant::fromValue(kShortcut);

    // Async: a synchronous call would block the GUI thread on the session bus
    // for the length of the round trip, on a click.
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher] {
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "overview: invokeShortcut failed:" << reply.errorMessage();
        } else {
            // One line per press. The call succeeds silently, and without this
            // there is no way to tell a delivered click from one that never
            // reached the button - which is exactly the ambiguity that made the
            // ydotool version hard to diagnose.
            qInfo() << "overview: invoked" << kShortcut;
        }
        watcher->deleteLater();
    });
}
