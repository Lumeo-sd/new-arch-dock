// Standalone check for the overview shortcut translation, so it can be verified
// without clicking the dock: the dock hides itself whenever a window is
// maximised, which leaves the button untestable by hand at times, and this
// translation is the part that breaks quietly.
//
// Uses fprintf rather than qInfo so the output cannot be swallowed by Qt logging
// configuration.
//
// Run: tools/test-overview-parser.sh

#include "overviewmanager.h"

#include <QCoreApplication>

#include <cstdio>

namespace {

int failures = 0;

void check(const QString &input, const QStringList &expected)
{
    const QStringList got = OverviewManager::translatedEvents(input);
    const bool ok = got == expected;
    if (!ok)
        ++failures;

    std::printf("  %s  %-20s -> %s", ok ? "ok  " : "FAIL", qPrintable(input), qPrintable(got.join(QLatin1Char(' '))));
    if (!ok)
        std::printf("   (expected: %s)", qPrintable(expected.join(QLatin1Char(' '))));
    std::printf("\n");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    OverviewManager manager;
    const QString shortcut = manager.configuredShortcut();

    std::printf("reading the real config:\n");
    std::printf("  [kwin] Overview = \"%s\"\n", qPrintable(shortcut));
    std::printf("  available       = %s\n", manager.available() ? "true" : "false");
    std::printf("  status          = %s\n", qPrintable(manager.status()));

    if (shortcut.isEmpty()) {
        std::printf("  FAIL the real config yielded no shortcut\n");
        ++failures;
    }

    std::printf("shortcut -> keycodes:\n");
    check(QStringLiteral("Meta+W"),
          { QStringLiteral("125:1"), QStringLiteral("17:1"), QStringLiteral("17:0"), QStringLiteral("125:0") });
    check(QStringLiteral("Ctrl+Alt+T"),
          { QStringLiteral("29:1"), QStringLiteral("56:1"), QStringLiteral("20:1"), QStringLiteral("20:0"),
            QStringLiteral("56:0"), QStringLiteral("29:0") });
    check(QStringLiteral("Meta+F1"),
          { QStringLiteral("125:1"), QStringLiteral("59:1"), QStringLiteral("59:0"), QStringLiteral("125:0") });
    check(QStringLiteral("Meta+F11"),
          { QStringLiteral("125:1"), QStringLiteral("87:1"), QStringLiteral("87:0"), QStringLiteral("125:0") });
    check(QStringLiteral("Meta+Shift+A"),
          { QStringLiteral("125:1"), QStringLiteral("42:1"), QStringLiteral("30:1"), QStringLiteral("30:0"),
            QStringLiteral("42:0"), QStringLiteral("125:0") });
    check(QStringLiteral("Meta+Space"),
          { QStringLiteral("125:1"), QStringLiteral("57:1"), QStringLiteral("57:0"), QStringLiteral("125:0") });

    std::printf("unsupported input yields nothing rather than a wrong key:\n");
    check(QStringLiteral("W"), {});
    check(QString(), {});
    check(QStringLiteral("Meta+XF86AudioPlay"), {});
    check(QStringLiteral("Hyper+W"), {});

    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
