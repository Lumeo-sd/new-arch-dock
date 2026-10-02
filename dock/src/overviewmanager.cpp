#include "overviewmanager.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

#include <iterator>

namespace {

// Evdev keycodes, which is what ydotool 1.x speaks: it documents
// "<keycode>:<pressed>" and resolves no key names, so "super+w" is silently
// ignored while 125:1 17:1 works. Modifiers use the left-hand key.
struct Keycode {
    const char *name;
    int code;
};

const Keycode kModifiers[] = {
    { "meta", 125 },   // KEY_LEFTMETA, which is what KDE calls Meta/Super
    { "super", 125 },
    { "ctrl", 29 },
    { "control", 29 },
    { "alt", 56 },
    { "shift", 42 },
};

const Keycode kNamed[] = {
    { "space", 57 },      { "tab", 15 },   { "esc", 1 },      { "escape", 1 }, { "enter", 28 },
    { "return", 28 },     { "insert", 110 }, { "delete", 111 }, { "home", 102 }, { "end", 107 },
    { "pageup", 104 },    { "page_up", 104 }, { "pagedown", 109 }, { "page_down", 109 },
};

int lookup(const Keycode *table, std::size_t count, const QString &name)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (name == QLatin1String(table[i].name))
            return table[i].code;
    }
    return 0;
}

} // namespace

OverviewManager::OverviewManager(QObject *parent)
    : QObject(parent)
    , m_uinput(QFileInfo(QStringLiteral("/dev/uinput")).isWritable())
{
}

bool OverviewManager::daemonRunning()
{
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        return false;
    return QFileInfo(runtime + QStringLiteral("/.ydotool_socket")).exists();
}

bool OverviewManager::available() const
{
    return unavailableReason().isEmpty();
}

QString OverviewManager::unavailableReason() const
{
    if (QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty())
        return tr("Activities overview needs ydotool - run tools/enable-overview-button.sh");
    if (!m_uinput)
        return tr("Activities overview needs a writable /dev/uinput");
    if (!daemonRunning())
        return tr("Activities overview needs the ydotool daemon - run tools/enable-overview-button.sh");
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

    if (toYdotoolEvents(shortcut).isEmpty())
        return tr("%1 cannot be sent - bind a simpler Overview shortcut").arg(shortcut);

    return tr("Activities overview (%1)").arg(shortcut);
}

QString OverviewManager::configuredShortcut() const
{
    // Deliberately not QSettings. kglobalaccel writes descriptions that end in a
    // percent sign - "Decrease Volume by 1%" - and QSettings' INI parser treats
    // "%" as an escape, so it hits a FormatError on the first of those and stops
    // reading. Everything after it, including [kwin] Overview, comes back empty
    // with no error of its own. The format is trivial, so read it directly.
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + QStringLiteral("/kglobalshortcutsrc"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    bool inKwin = false;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inKwin = line.mid(1, line.size() - 2) == QLatin1String("kwin");
            continue;
        }
        if (!inKwin)
            continue;

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0 || line.left(equals).trimmed() != QLatin1String("Overview"))
            continue;

        // "Meta+W,Meta+W,Toggle Overview" - the first field is the binding.
        return line.mid(equals + 1).section(QLatin1Char(','), 0, 0).trimmed();
    }

    return QString();
}

QStringList OverviewManager::toYdotoolEvents(const QString &shortcut)
{
    const QStringList parts = shortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};

    // Evdev codes are positional, so this sends the physical key rather than the
    // symbol on the active layout. That is what KWin matches against, and it is
    // what happens when the shortcut is typed.
    static const QHash<QChar, int> letters = {
        { QLatin1Char('q'), 16 }, { QLatin1Char('w'), 17 }, { QLatin1Char('e'), 18 }, { QLatin1Char('r'), 19 },
        { QLatin1Char('t'), 20 }, { QLatin1Char('y'), 21 }, { QLatin1Char('u'), 22 }, { QLatin1Char('i'), 23 },
        { QLatin1Char('o'), 24 }, { QLatin1Char('p'), 25 }, { QLatin1Char('a'), 30 }, { QLatin1Char('s'), 31 },
        { QLatin1Char('d'), 32 }, { QLatin1Char('f'), 33 }, { QLatin1Char('g'), 34 }, { QLatin1Char('h'), 35 },
        { QLatin1Char('j'), 36 }, { QLatin1Char('k'), 37 }, { QLatin1Char('l'), 38 }, { QLatin1Char('z'), 44 },
        { QLatin1Char('x'), 45 }, { QLatin1Char('c'), 46 }, { QLatin1Char('v'), 47 }, { QLatin1Char('b'), 48 },
        { QLatin1Char('n'), 49 }, { QLatin1Char('m'), 50 },
    };
    static const QHash<QChar, int> digits = {
        { QLatin1Char('1'), 2 }, { QLatin1Char('2'), 3 }, { QLatin1Char('3'), 4 }, { QLatin1Char('4'), 5 },
        { QLatin1Char('5'), 6 }, { QLatin1Char('6'), 7 }, { QLatin1Char('7'), 8 }, { QLatin1Char('8'), 9 },
        { QLatin1Char('9'), 10 }, { QLatin1Char('0'), 11 },
    };

    QList<int> codes;
    const int last = parts.size() - 1;
    for (int i = 0; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed().toLower();
        int code = 0;
        if (i != last) {
            code = lookup(kModifiers, std::size(kModifiers), part);
        } else if (part.size() == 1) {
            const QChar ch = part.at(0);
            code = letters.value(ch, digits.value(ch, 0));
        } else {
            code = lookup(kNamed, std::size(kNamed), part);
            if (code == 0 && part.size() >= 2 && part.at(0) == QLatin1Char('f')) {
                bool ok = false;
                const int n = part.mid(1).toInt(&ok);
                // F1-F10 run 59-68; F11 and F12 break the sequence at 87 and 88.
                if (ok && n >= 1 && n <= 10)
                    code = 58 + n;
                else if (ok && n == 11)
                    code = 87;
                else if (ok && n == 12)
                    code = 88;
            }
        }

        if (code == 0)
            return {};
        codes.append(code);
    }

    QStringList events;
    for (int code : std::as_const(codes))
        events.append(QStringLiteral("%1:1").arg(code));
    // Release in reverse, so a chord unwinds the way a keyboard would.
    for (auto it = codes.crbegin(); it != codes.crend(); ++it)
        events.append(QStringLiteral("%1:0").arg(*it));
    return events;
}

QStringList OverviewManager::translatedEvents(const QString &shortcut)
{
    return toYdotoolEvents(shortcut);
}

void OverviewManager::showOverview()
{
    // One line per press: this button is the only part of the dock whose failure
    // is silent, and there is no API to report it to anyone else.
    const QString reason = unavailableReason();
    if (!reason.isEmpty()) {
        qInfo() << "overview: unavailable -" << reason;
        return;
    }

    const QStringList events = toYdotoolEvents(configuredShortcut());
    if (events.isEmpty()) {
        qInfo() << "overview: cannot send the bound shortcut" << configuredShortcut();
        return;
    }

    QStringList args{ QStringLiteral("key"), QStringLiteral("--key-delay=20") };
    args += events;
    QProcess::startDetached(QStandardPaths::findExecutable(QStringLiteral("ydotool")), args);
    qInfo() << "overview: sent" << events.join(QLatin1Char(' '));
}
