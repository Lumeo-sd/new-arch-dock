# cutefish-dock-kde

Панель завдань у стилі CutefishOS для KDE Plasma 6 на Wayland. Qt6/KF6, без
X11, без `NET::Dock` і без сторонніх залежностей поза Qt та KDE Frameworks.

Це мінімальний робочий порт: dock + launcher + fishui/appearance.
Statusbar із повного порту CutefishOS сюди не входить (він — окремий
проєкт `cutefish-statusbar-kde`, верхня панель як у Lingmo).

## Що вміє

- панель знизу / зліва / справа, два стилі: плаваючий (round) і прямий (straight)
- відстеження вікон через `org_kde_plasma_window_management` (KWin Wayland)
- закріплення застосунків, Drag & Drop у панель, контекстні меню
- інтелектуальне автоприховування (IntellHide)
- перемикання теми світла/темна разом з Plasma
- панель слідує за активним віртуальним десктопом (Activity) і переїжджає разом
  з ним, прив'язана до конкретного виходу
- кнопка Activities overview одразу за лаунчером: відкриває огляд робочого
  столу через `org.kde.kglobalaccel` (`invokeShortcut "Overview"`), тож працює
  і після перепризначення клавіші в KDE System Settings
- D-Bus API: сервіс `com.cutefish.Dock`, об'єкт `/Dock`

## Вимоги

Arch-подібні (перевірено на CachyOS, Plasma 6.7.5, Qt 6.11.2, KF6 6.7.5):

```bash
sudo pacman -S --needed cmake extra-cmake-modules \
  qt6-base qt6-declarative qt6-shadertools qt6-tools qt6-5compat qt6-tools \
  layer-shell-qt kwayland kwindowsystem
```

Скрипт перевіряє ці залежності сам і надрукує команду, якої не вистачає.

Кнопці Overview не потрібно нічого встановлювати: вона йде через
`org.kde.kglobalaccel`, який уже є в будь-якому Plasma. Жодного демона,
`/dev/uinput` чи читання `kglobalshortcutsrc`.

## Збірка та встановлення

Нічого не потребує root: префікс за замовчуванням `~/.local`.

```bash
git clone https://github.com/Lumeo-sd/new-arch-dock
cd new-arch-dock
./tools/install-user.sh
```

Скрипт збирає `fishui` (QML-фреймворк, з яким dock працює) та `dock`,
встановлює їх у приватний префікс і створює
`~/.config/autostart/cutefish-dock.desktop`.

Перевірити, що все на місці:

```bash
cutefish-dock &            # у Plasma Wayland
busctl --user get-property com.cutefish.Dock /Dock com.cutefish.Dock primaryGeometry
```

Системна інсталяція (потрібен root):

```bash
sudo PREFIX=/usr ./tools/install-user.sh
```

## Як влаштована збірка

| Каталог | Роль |
|---|---|
| `dock/` | сам dock: C++ + QML, LayerShellQt, KWayland |
| `launcher/` | сітка застосунків (D-Bus `com.cutefish.Launcher`, живе постійно, тоглиться кнопкою dock) |
| `fishui/` | QML-фреймворк Cutefish: теми, вікна, підказки; у ньому збирається `cutefish-framework/appearance` |
| `cutefish-framework/appearance/` | бібліотека запиту теми в Plasma |
| `tools/install-user.sh` | збірка + встановлення у приватний префікс |
| `docs/` | журнал міграції порту (`STEP0-AUDIT-dock.md`, `STEP2-DOCK-WAYLAND.md`), старий монорепо-README (`README-monolith.md`) |
| `experiments/` | PROGRESS.md — сесійний журнал великої міграції 2026-09 |
| `scripts/` | лабораторні скрипти піксельної верифікації (для Lingmo-панелі, див. README в каталозі) |

Ключовий момент розкладки: `MainWindow` реєструє `<bindir>/../lib64/qt6/qml`,
а RUNPATH `libFishUI.so` — `$ORIGIN/../../..`, тобто каталог бібліотек і
каталог QML мають бути на одному рівні. Скрипт це забезпечує через
`CMAKE_INSTALL_LIBDIR`; змінювати `LIB_SUBDIR` треба разом із `QML_SUBDIR`.

## Налаштування

`~/.config/cutefishos/dock.conf`:

| Ключ | Значення |
|---|---|
| `Screen` | `QScreen::name()` виходу, на якому живе панель. Порожньо або відсутньо — primary. Якщо названий вихід від'єднано, панель переїде на primary і напише про це в лог. |
| `Direction` | `0` ліворуч, `1` знизу, `2` праворуч |
| `Visibility` | `0` завжди, `1` інтелектуальне приховування, `2` завжди сховано |
| `IconSize` | розмір іконок у пікселях |
| `EdgeMargins` | відступи від країв екрана |
| `Style` | `0` плаваюча панель, `1` пряма |

## Межі

- **Другий монітор не перевірявся.** Логіка є (вибір виходу, підписка на
  `screenAdded`/`screenRemoved`, відкат на primary), але розроблялася на
  одноекранній машині — перевір на справжньому другому моніторі.
- **`X-KDE-Wayland-Interfaces`** у desktop-файлі — приватний runtime-контракт
  KWin. Працює на 6.7.5, але зламається при оновленні KWin. Правильний шлях —
  `PW::LibTaskManager`, як у Krema.
- **Анімація появи після Activities Overview.** Коли dock не задає namespace,
  KWin не вважає layer-поверхню доком і показує її за правилами звичайного
  вікна: панель з'являється зсувом ~20 px за ~0.07 с. Лікується одним
  викликом `m_layerShell->setScope("dock")` — без нього KWin не має підстави
  показувати поверхню як панель.
- **Statusbar** у цей порт не входить (окремий проєкт — верхня панель).

## Ліцензія та походження

GPL-3.0. Порт `cutefishos/dock`, `cutefishos/fishui` і `cutefishos/libcutefish`
(апстрім архівовано 2026-08-30); оригінальний вигляд і анімації збережені,
змінені лише X11-специфічні частини. Його автор — mutagen.
