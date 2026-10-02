#include "appearance.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QTimer>

namespace
{
constexpr auto Service = "com.cutefish.Services";
constexpr auto ObjectPath = "/com/cutefish/Services/Appearance";
constexpr auto Interface = "com.cutefish.Services.Appearance";
}

Appearance::Appearance(QObject *parent)
    : QObject(parent)
    , m_interface(nullptr)
{
    auto *watcher = new QDBusServiceWatcher(QString::fromLatin1(Service),
                                             QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForRegistration,
                                             this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Appearance::init);

    // Plasma rewrites its config when the scheme changes; there is no signal
    // for that over D-Bus, so poll. Cheap: two config reads, and only while no
    // CutefishOS daemon owns the appearance.
    auto *poll = new QTimer(this);
    poll->setInterval(2000);
    connect(poll, &QTimer::timeout, this, [this] {
        if (m_interface && m_interface->isValid())
            return;
        if (m_lastDarkMode == plasmaDarkMode())
            return;
        m_lastDarkMode = plasmaDarkMode();
        emit darkModeChanged();
    });
    m_lastDarkMode = plasmaDarkMode();
    poll->start();

    init();
}

void Appearance::init()
{
    delete m_interface;
    m_interface = new QDBusInterface(QString::fromLatin1(Service),
                                     QString::fromLatin1(ObjectPath),
                                     QString::fromLatin1(Interface),
                                     QDBusConnection::sessionBus(),
                                     this);

    if (!m_interface->isValid())
        return;

    connect(m_interface, SIGNAL(darkModeChanged(bool)), this, SLOT(onDarkModeChanged(bool)));
    connect(m_interface, SIGNAL(wallpaperChanged(QString)), this, SLOT(onWallpaperChanged(QString)));
    connect(m_interface, SIGNAL(accentColorChanged(int)), this, SLOT(onAccentColorChanged(int)));
    connect(m_interface, SIGNAL(darkModeDimsWallpaerChanged()), this, SLOT(onDarkModeDimsWallpaperChanged()));
    connect(m_interface, SIGNAL(blurEnabledChanged()), this, SLOT(onBlurEnabledChanged()));
    connect(m_interface, SIGNAL(systemFontPointSizeChanged()), this, SLOT(onFontPointSizeChanged()));
    connect(m_interface, SIGNAL(systemFontChanged()), this, SLOT(onFontFamilyChanged()));
    connect(m_interface, SIGNAL(backgroundTypeChanged()), this, SLOT(onBackgroundTypeChanged()));
    connect(m_interface, SIGNAL(backgroundColorChanged()), this, SLOT(onBackgroundColorChanged()));
    connect(m_interface, SIGNAL(backgroundVisibleChanged()), this, SLOT(onBackgroundVisibleChanged()));

    emit darkModeChanged();
    emit wallpaperChanged();
    emit accentColorIndexChanged();
    emit dimsWallpaperChanged();
    emit blurEnabledChanged();
    emit fontPointSizeChanged();
    emit fontFamilyChanged();
    emit fixedFontFamilyChanged();
    emit backgroundTypeChanged();
    emit backgroundColorChanged();
    emit backgroundVisibleChanged();
}

QVariant Appearance::serviceProperty(const char *name) const
{
    return m_interface && m_interface->isValid() ? m_interface->property(name) : QVariant();
}

void Appearance::callService(const char *method)
{
    if (m_interface && m_interface->isValid())
        m_interface->call(method);
}

void Appearance::callService(const char *method, const QVariant &value)
{
    if (m_interface && m_interface->isValid())
        m_interface->call(method, value);
}

bool Appearance::darkMode() const
{
    if (m_interface && m_interface->isValid())
        return serviceProperty("isDarkMode").toBool();

    // There is no CutefishOS appearance daemon on Plasma, so Plasma is the
    // authority. The old fallback read ~/.config/cutefishos/appearance.conf,
    // a key nothing ever writes, so the answer was permanently false: a dark
    // Plasma desktop still got light panel colours and light-mode text.
    return plasmaDarkMode();
}

bool Appearance::plasmaDarkMode() const
{
    // Plasma stores the effective flag here when the user picks a colour
    // scheme.
    QSettings plasmarc(QStringLiteral("plasma-org.kde.plasma-desktop"),
                       QStringLiteral("plasma-plasmarc"));
    plasmarc.beginGroup(QStringLiteral("Colors:General"));
    const QVariant dark = plasmarc.value(QStringLiteral("darkMode"));
    if (dark.isValid())
        return dark.toBool();

    // Not every setup writes that key; the global theme still names the
    // palette in use (org.kde.breezedark.desktop, BreezeDark, ...).
    QSettings kdeglobals(QStringLiteral("kdeglobals"));
    const QString globalTheme =
        kdeglobals.value(QStringLiteral("KDE/LookAndFeelPackage")).toString();
    if (!globalTheme.isEmpty())
        return globalTheme.contains(QLatin1String("dark"), Qt::CaseInsensitive);

    // Last resort: the platform theme plugin mirrors the scheme into the
    // application palette.
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

void Appearance::switchDarkMode(bool darkMode)
{
    callService("setDarkMode", darkMode);
}

bool Appearance::dimsWallpaper() const
{
    return serviceProperty("darkModeDimsWallpaer").toBool();
}

void Appearance::setDimsWallpaper(bool value)
{
    callService("setDarkModeDimsWallpaer", value);
}

bool Appearance::blurEnabled() const
{
    return serviceProperty("blurEnabled").toBool();
}

void Appearance::setBlurEnabled(bool value)
{
    callService("setBlurEnabled", value);
}

int Appearance::accentColorIndex() const
{
    return serviceProperty("accentColor").toInt();
}

void Appearance::setAccentColor(int accentColor)
{
    callService("setAccentColor", accentColor);
}

qreal Appearance::fontPointSize() const
{
    return serviceProperty("systemFontPointSize").toReal();
}

void Appearance::setFontPointSize(qreal fontPointSize)
{
    callService("setSystemFontPointSize", fontPointSize);
}

QString Appearance::fontFamily() const
{
    return serviceProperty("systemFont").toString();
}

void Appearance::setFontFamily(const QString &name)
{
    if (!name.isEmpty())
        callService("setSystemFont", name);
}

QString Appearance::fixedFontFamily() const
{
    return serviceProperty("systemFixedFont").toString();
}

// com.cutefish.Services.Appearance has no systemFixedFontChanged signal, so
// the change is announced here.
void Appearance::setFixedFontFamily(const QString &name)
{
    if (!name.isEmpty()) {
        callService("setSystemFixedFont", name);
        emit fixedFontFamilyChanged();
    }
}

QString Appearance::wallpaper() const
{
    return serviceProperty("wallpaper").toString();
}

void Appearance::setWallpaper(const QString &path)
{
    if (!path.isEmpty())
        callService("setWallpaper", path);
}

int Appearance::backgroundType() const
{
    return serviceProperty("backgroundType").toInt();
}

void Appearance::setBackgroundType(int type)
{
    callService("setBackgroundType", type);
}

QString Appearance::backgroundColor() const
{
    return serviceProperty("backgroundColor").toString();
}

void Appearance::setBackgroundColor(const QString &color)
{
    callService("setBackgroundColor", color);
}

bool Appearance::backgroundVisible() const
{
    return serviceProperty("backgroundVisible").toBool();
}

void Appearance::setCursorTheme(const QString &theme)
{
    if (!theme.isEmpty())
        callService("setCursorTheme", theme);
}

void Appearance::applyFontSettings()
{
    callService("applyFontSettings");
}

void Appearance::onDarkModeChanged(bool darkMode)
{
    Q_UNUSED(darkMode);
    emit darkModeChanged();
}

void Appearance::onWallpaperChanged(const QString &path)
{
    Q_UNUSED(path);
    emit wallpaperChanged();
}

void Appearance::onAccentColorChanged(int accentColor)
{
    Q_UNUSED(accentColor);
    emit accentColorIndexChanged();
}

void Appearance::onDarkModeDimsWallpaperChanged()
{
    emit dimsWallpaperChanged();
}

void Appearance::onBlurEnabledChanged()
{
    emit blurEnabledChanged();
}

void Appearance::onFontPointSizeChanged()
{
    emit fontPointSizeChanged();
}

void Appearance::onFontFamilyChanged()
{
    emit fontFamilyChanged();
}

void Appearance::onBackgroundTypeChanged()
{
    emit backgroundTypeChanged();
}

void Appearance::onBackgroundColorChanged()
{
    emit backgroundColorChanged();
}

void Appearance::onBackgroundVisibleChanged()
{
    emit backgroundVisibleChanged();
}
