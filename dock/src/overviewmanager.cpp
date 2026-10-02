#include "overviewmanager.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

OverviewManager::OverviewManager(QObject *parent)
    : QObject(parent)
    , m_uinput(QFileInfo(QStringLiteral("/dev/uinput")).isWritable())
{
}

bool OverviewManager::available() const
{
    return unavailableReason().isEmpty();
}

QString OverviewManager::unavailableReason() const
{
    if (QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty()) {
        return tr("Activities overview needs ydotool - run tools/enable-overview-button.sh");
    }
    if (!m_uinput) {
        return tr("Activities overview needs a writable /dev/uinput");
    }
    return QString();
}

QString OverviewManager::status() const
{
    const QString reason = unavailableReason();
    if (!reason.isEmpty())
        return reason;

    const QString shortcut = configuredShortcut();
    if (shortcut.isEmpty())
        return tr("Activities overview has no shortcut bound");

    return tr("Activities overview (%1)").arg(shortcut);
}

QString OverviewManager::configuredShortcut() const
{
    QSettings shortcuts(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                            + QStringLiteral("/kglobalshortcutsrc"),
                        QSettings::IniFormat);
    shortcuts.beginGroup(QStringLiteral("kwin"));
    // "Meta+W,Meta+W,Toggle Overview" - the first field is the active binding.
    return shortcuts.value(QStringLiteral("Overview")).toString().section(QLatin1Char(','), 0, 0).trimmed();
}

QString OverviewManager::toYdotoolKeys(const QString &shortcut)
{
    const QStringList parts = shortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return QString();

    // ydotool names the modifiers after the kernel, where Meta is the Super key.
    static const QHash<QString, QString> modifiers = {
        { QStringLiteral("meta"), QStringLiteral("super") },
        { QStringLiteral("super"), QStringLiteral("super") },
        { QStringLiteral("ctrl"), QStringLiteral("ctrl") },
        { QStringLiteral("control"), QStringLiteral("ctrl") },
        { QStringLiteral("alt"), QStringLiteral("alt") },
        { QStringLiteral("shift"), QStringLiteral("shift") },
    };
    static const QHash<QString, QString> namedKeys = {
        { QStringLiteral("space"), QStringLiteral("space") },
        { QStringLiteral("tab"), QStringLiteral("tab") },
        { QStringLiteral("esc"), QStringLiteral("esc") },
        { QStringLiteral("escape"), QStringLiteral("esc") },
        { QStringLiteral("enter"), QStringLiteral("enter") },
        { QStringLiteral("return"), QStringLiteral("enter") },
        { QStringLiteral("insert"), QStringLiteral("insert") },
        { QStringLiteral("delete"), QStringLiteral("delete") },
        { QStringLiteral("home"), QStringLiteral("home") },
        { QStringLiteral("end"), QStringLiteral("end") },
        { QStringLiteral("pageup"), QStringLiteral("pgup") },
        { QStringLiteral("page_up"), QStringLiteral("pgup") },
        { QStringLiteral("pagedown"), QStringLiteral("pgdn") },
        { QStringLiteral("page_down"), QStringLiteral("pgdn") },
    };

    QStringList keys;
    const int last = parts.size() - 1;
    for (int i = 0; i < parts.size(); ++i) {
        QString part = parts.at(i).trimmed().toLower();
        if (i != last) {
            if (!modifiers.contains(part))
                return QString();
            part = modifiers.value(part);
        } else if (part.size() == 1) {
            if (!part.at(0).isLetterOrNumber())
                return QString();
        } else if (namedKeys.contains(part)) {
            part = namedKeys.value(part);
        } else if (!QRegularExpression(QStringLiteral("^f([1-9]|1[0-9]|2[0-4])$")).match(part).hasMatch()) {
            // Something like "Meta+XF86AudioPlay" that ydotool may not name.
            return QString();
        }
        keys.append(part);
    }

    return keys.join(QLatin1Char('+'));
}

void OverviewManager::showOverview()
{
    if (!available())
        return;

    const QString keys = toYdotoolKeys(configuredShortcut());
    if (keys.isEmpty())
        return;

    QProcess::startDetached(QStandardPaths::findExecutable(QStringLiteral("ydotool")),
                            { QStringLiteral("key"), QStringLiteral("--delay"), QStringLiteral("20"), keys });
}
