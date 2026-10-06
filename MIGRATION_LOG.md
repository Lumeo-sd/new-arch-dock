# Журнал міграції/збірки

- 2026-10-06: консолідація. Репозиторій `cutefish-dock-kde` об'єднує:
  - повну історію `Lumeo-sd/new-arch-dock` (зумії клону з GitHub, remote `origin` збережено):
    `dock/`, `launcher/` (додано), `fishui/`, `cutefish-framework/`, `tools/`, `.github/`, CI;
  - архів лабораторії та журналів: `scripts/` (піксельні лабораторні, Lingmo-панель),
    `experiments/PROGRESS.md` (стан міграції 2026-09), `docs/README-monolith.md`
    (старий README монорепо `Lumeo-sd/kde-cutefish-dock`);
  - док-довідку порту: `docs/STEP0-AUDIT-dock.md`, `docs/STEP2-DOCK-WAYLAND.md`
    (скопійовано з `cutefish-statusbar-kde/docs/`).
- 2026-10-06: додано `launcher/` з `cutefish-statusbar-kde/src/launcher`
  (портовано на Qt6/KF6, перевірено в сесії 2026-09) — кнопка Launcher у dock
  більше не б'є по порожнечі; до інскрипта `tools/install-user.sh` додано
  збірку launcher.
- 2026-10-06: виправлено `applicationmodel.cpp`: шлях до
  `cutefish-dock-list.conf` тепер з `CMAKE_INSTALL_FULL_SYSCONFDIR`
  (раніше хардкод `/etc/...` не спрацьовував при приватному префіксі).
- 2026-10-06: виправлено запуск launcher кнопкою dock: тепер
  `cutefish-launcher --show` (перший клік показує сітку; повторний — D-Bus toggle).

Старі (застарілі, не оновлюються):
- `~/.hermes/cache/scratch/kcd/` — клон монорепо станом на 2026-09-14,
  без Oct-робіт (overview, activity-following, CI).
- `~/Documents/GitHub/cutefish-statusbar-kde/src/{dock,fishui,cutefish-framework,launcher,statusbar}` —
  копія того ж стану 2026-09-14; джерело правди тепер тут.
- Lingmo-панель — окремий проєкт: `~/Documents/lingmo-panel` +
  `~/Documents/GitHub/cutefish-statusbar-kde/references/lingmo-*`.
