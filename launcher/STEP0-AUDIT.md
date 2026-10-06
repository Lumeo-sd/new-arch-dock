# Крок 0 — Аудит: порт CutefishOS launcher на Qt6/KF6 + Plasma 6 Wayland

Дата: 2026-09-13
Система: Fedora 44 (KDE Plasma Desktop), Plasma 6.7.5, Qt 6.11.2, KF6 (kf6-kwindowsystem 6.30.0),
сесія — real Wayland (`XDG_SESSION_TYPE=wayland`, `WAYLAND_DISPLAY=wayland-0`), KWin 6.7.5.
Джерело: https://github.com/cutefishos/launcher (архівовано 30.08.2026, read-only, GPL-3.0).
Локальний форк: `launcher/`, гілка `fork-qt6-wayland` (upstream = cutefishos/launcher, як у
dock/fishui/cutefish-framework; поки локально, не запушено).

---

## 0.0 Важлива передумова: стан репозиторію upstream (Qt6-порт уже зроблений?)

**Відповідь: ТАК — upstream уже портований на Qt6 на рівні збірки, збірка проходить.**
Але X11-only частина рантайму залишилась (дві точки, див. 0.1), плюс є одна відсутня
QML-залежність (`Cutefish.System`, див. 0.5), якої на Plasma взагалі не існує.

| Коміт | Що зроблено |
|---|---|
| `bedff0a` "fix(launcher): port launcher to Qt6" | CMake: `Qt5*`→`Qt6*`, `qt5_add_*`→`qt6_add_*` (dbus-адаптор, переклади), `KF5WindowSystem`→`KF6WindowSystem`; `launcher.cpp`: `#include <KWindowSystem>`→`#include <KX11Extras>` (див. 0.1); дрібні Qt6-правки `iconitem.*`, `main.cpp` (QFile). |
| `30e14ef` "prevent startup crash on zero-size view" | `AllAppsView.qml`: `Math.max(1, ...)` для rows/columns/pageCount — краш на нульовому розмірі при створенні вікна. |
| `6eba496` "bound list view cache buffer" | `cacheBuffer` обмежено шириною вікна. |
| `c781345`, `723e2f9` | debian: пакування під Qt6/KF6; CI на Ubuntu 26.04. |

**Перевірено збіркою на цій системі:** `cmake` + `cmake --build` проходять чисто (бінарник
збирається; `KF6WindowSystem` і X11-заголовки на системі є). Тобто «Qt5→Qt6» як таке вже зроблено
upstream — аналог коміту `6df50a5` у dock, тільки тут порт ще й **збирається**.

Але runtime на Wayland сьогодні не працює:
- повноекранне позиціювання через `setGeometry(...)` на Wayland не дає повноекранного вікна
  (toplevel не можна довільно розмістити/розтягнути),
- `KX11Extras::setState(...)` — no-op на Wayland,
- `import Cutefish.System 1.0` в `main.qml` — модуля немає → QML не завантажиться взагалі.

---

## 0.1 Як launcher показує/ховає себе (повноекранне/оверлейне вікно) (ПИТАННЯ 1)

**Відповідь: майже все крос-платформне; X11-only — лише «SkipTaskbar/SkipPager через
KX11Extras» і «повноекранність через setGeometry». Always-on-top НЕ використовується.**

Ланцюг:

1. `Launcher : QQuickView` (`src/launcher.h`) — `Qt::FramelessWindowHint`, прозорий колір,
   `setResizeMode(QQuickView::SizeRootObjectToView)` (launcher.cpp:50-52).
2. Геометрія: `onGeometryChanged()` → `updateSize()` → `setGeometry(qApp->primaryScreen()->geometry())`
   (launcher.cpp:172-190) — вікно розміром на весь primary screen. **X11-стиль**: на Wayland
   розмір/позицію toplevel так не задати.
3. `showWindow()/hideWindow()/toggle()` (104-122) — звичайний `setVisible`, платформонезалежний.
4. `showEvent` → `KX11Extras::setState(winId(), NET::SkipTaskbar | NET::SkipPager)`
   (192-197) — **X11-only**. Це єдине використання KF6/KX11Extras у всьому коді.
5. Замість always-on-top — стек: launcher = звичайне (підняте) вікно; dock на X11
   (NET::Dock) завжди зверху. QML-мажори `leftMargin/rightMargin/bottomMargin` беруться з
   D-Bus-геометрії dock (`updateMargins()`, 152-170; `direction==1` → bottomMargin) і тримають
   контент поза панеллю.
6. `resizeEvent` — `e->ignore()` (199-203): розмір нав'язує WM.

| Файл | X11-конструкція | Заміна на Wayland |
|---|---|---|
| `src/launcher.cpp` `showEvent` (192-197) | `KX11Extras::setState(winId(), NET::SkipTaskbar \| NET::SkipPager)` | **прибрати цілком**. Wayland-еквівалента Sk*State для звичайного toplevel немає; шкоди не буде: launcher ховається по focus loss, а у dock-моделі його вікно і так виключається (див. 0.7). |
| `src/launcher.cpp` `updateSize` (172-179) | `setGeometry(primaryScreen->geometry())` — «вікно на весь екран» | `showFullScreen()` (справжній xdg-shell fullscreen) — вкриває output; прозорість/фрейм-less/мажори не міняються. |
| `CMakeLists.txt` | `find_package(KF6WindowSystem)` + `KF6::WindowSystem` (лише заради KX11Extras) | прибрати з лінкування (більше нічого з KF не використовується). |

Стак на Wayland зберігається 1:1: launcher = звичайний (повноекранний) toplevel; dock =
layer-shell `LayerTop` → dock лишається НАД launcher, контент розмежований тими самими мажорами.

---

## 0.2 Single-instance / toggle-логіка (ПИТАННЯ 2)

**Відповідь: чистий D-Bus-механізм — платформонезалежний, переноситься без змін.**

- `main.cpp:72-77`: `QDBusConnection::registerService("com.cutefish.Launcher")`; якщо ім'я вже
  зайняте → виклик `toggle()` на наявному екземплярі (`/Launcher`) і вихід (`return -1`).
- `--show` → `firstShow=true` → вікно видиме одразу; без `--show` процес стартує прихованим
  (`setVisible(firstShow)`, launcher.cpp:56-57).
- `Launcher::toggle()/showWindow()/hideWindow()` + D-Bus-адаптор `com.cutefish.Launcher`
  (методи show/hide/toggle, `com.cutefish.Launcher.xml`) — платформонезалежні.
- X11-специфічного (на кшталт NET::WM2Focus, зміни active window) для цього НЕ використовується.

**Нюанс інтеграції для кроку 2 (кнопка dock):** dock запускає процес БЕЗ `--show` → перший клік
при незапущеному launcher стартує **прихований** процес (видимий ефект — жоден), і лише наступний
клік спрацьовує як toggle. Коректна поведінка кнопки — dock має запускати `"cutefish-launcher --show"`,
або викликати D-Bus `toggle()` коли сервіс уже зареєстровано (див. 0.7).

---

## 0.3 Як launcher отримує список застосунків (ПИТАННЯ 3)

**Відповідь: власний `LauncherModel : QAbstractListModel` + `AppItem` — це не «клон»
SystemAppMonitor з dock, і копіювати dock-клас сюди не треба.**

| Аспект | dock `SystemAppMonitor` | launcher `LauncherModel` |
|---|---|---|
| Джерела .desktop | усі XDG data dirs (`QStandardPaths::locateAll(...)`) | **лише `/usr/share/applications`** (`launchermodel.cpp:227`; watcher на нього — 64) |
| Парсинг | власний | власний `DesktopProperties` (не QSettings: той псує `;` як коментар) |
| Фільтр OnlyShowIn | — | `XDG_CURRENT_DESKTOP` (37-45) — для Plasma збігається (KDE) ✓ |
| Сортування/збереження | — | `QSettings("cutefishos","launcher-applist")` + `delaySave()` |
| Пошук | — | `search(key)` — фільтр того самого списку за name/id (CaseInsensitive) |

**Проблема для роботи на цьому моніторі:** у `/usr/share/applications` НЕМАЄ користувацьких
додатків (opencode, obsidian, helium тощо — вони в `~/.local/share/applications`) → launcher їх
не покаже. Це не X11-обмеження, а функціональний геп (той самий клас проблеми, яку dock вирішує
багатокаталоговим сканом). Рекомендація: перелік усіх `applications/`-каталогів XDG data dirs
(такий самий підхід, що в SystemAppMonitor; міняється кількома рядками в `LauncherModel::refresh()`).

---

## 0.4 QML-імпорти FishUI (ПИТАННЯ 4)

### 4.1 З FishUI (`import FishUI 1.0`) — звірено з Qt6-форком

| Тип FishUI | Де використовується в launcher | Стан у fishui-форку (Qt6) | Потрібна зміна? |
|---|---|---|---|
| `FishUI.Units` | `Units.largeSpacing`, `smallSpacing` (main.qml, AllAppsView.qml, GridItemDelegate.qml) | Є (`src/controls/Units.qml`) | Ні |
| `FishUI.Theme` | `Theme.highlightColor` (GridItemDelegate.qml:213) | Є (`Theme.qml:46`) | Ні |
| `FishUI.DesktopMenu` | GridItemDelegate.qml:112 (контекстне меню) | Є (`src/controls/DesktopMenu.qml`) | Ні |

### 4.2 Інші QML-імпорти

| Імпорт | Де | Стан |
|---|---|---|
| `Qt5Compat.GraphicalEffects` | main.qml (FastBlur `wallpaperBlur`, ColorOverlay `wallpaperColor`), GridItemDelegate (ColorOverlay) | Системний `qt6-qt5compat` встановлено; dock уже так використовує ✓ |
| `Cutefish.Launcher 1.0` | власний модуль: LauncherModel, PageModel, IconItem, AppManager (реєстрація в `main.cpp:46-50`) | Лишити як є |
| `Cutefish.System 1.0` | main.qml:157 `System.Wallpaper` | **НЕМАЄ** — див. 0.5 |

---

## 0.5 Чи використовує launcher libcutefish/cutefish-framework напряму (ПИТАННЯ 5)

**Відповідь: C++ — НІ (жодного `#include <cutefish/...>`, CMake нічого не лінкує).
QML — так, але через модуль, який на Plasma не існує.**

- **`import Cutefish.System 1.0` + `System.Wallpaper`** (main.qml:157-159; використовуються
  `color`, `type`, `path`, `dimsWallpaper`) — тип із модуля **старого** cutefish-shell
  (в актуальному `cutefishos/shell` такого модуля вже немає). На Plasma його немає взагалі →
  без заміни `main.qml` не завантажиться. Варіанти:
  - **(a) `Cutefish.Appearance` з локального форку cutefish-framework.** У модулі є тип
    `Wallpaper` (`appearance/CMakeLists.txt:44-54` + `appearance/qmlregistration.h`,
    `QML_NAMED_ELEMENT(Wallpaper)`) з тими самими `color/path/type`, а `dimsWallpaper` — на
    `Appearance`. QML-поверхня збігається 1:1: міняється лише рядок імпорту та
    `System.Wallpaper` → `Appearance.Wallpaper`. **Нюанси:** QML-модуль зараз не встановлений у
    `~/.local` (є лише `libcutefish-framework-appearance.so`; модуль увімкнено умовою
    `PROJECT_NAME STREQUAL "CutefishFramework"`) — треба зібрати й встановити; а без daemon
    `appearance` віддає дефолти (`type=0`, `path=""`, `color=""`, `dimsWallpaper=false` —
    значення читаються лазі через D-Bus) → фон launcher буде фактично прозорим (FastBlur без
    джерела) → потрібен **фолбек на шпалеру Plasma** в `appearance` (патерн darkMode-fallback
    `bb744d1`) або локально.
  - **(b) мінімальний локальний бекенд у launcher**: читати конфіг шпалери Plasma
    (`plasma-org.kde.plasma.desktop-appletsrc`, `Containments/.../Wallpaper/org.kde.image/General/Image`)
    + колір; framework не чіпається зовсім.
  - *Рекомендація: (a)* — менше змін у launcher, патерн фолбека в framework уже випробуваний
    (`bb744d1`), QML лишається оригінальним.
- `AppManager::uninstall` — D-Bus `com.cutefish.Daemon /AppManager` (на Plasma немає; пункт
  «Uninstall» у меню показується лише `if (appManager.isCutefishOS())`, тобто на Plasma скритий) —
  лишити як є.
- `ProcessProvider` — той самий патерн, що в dock: `com.cutefish.Session` → фолбек
  `QProcess::startDetached` **уже є** (processprovider.cpp:31-45) — змін не потребує.

---

## 0.6 Механізм закриття (Escape, клік поза вікном, втрата фокусу) (ПИТАННЯ 6)

| Тригер | Де | На Wayland |
|---|---|---|
| Escape | `textField.Keys.onEscapePressed` (main.qml:304), appView `Keys.onPressed` (330-332) → `launcher.hideWindow()` | ✓ повноекранний toplevel тримає фокус і клавіатуру |
| Клік у порожнечу launchpad | `MouseArea { z: -1 }` (378-385) → `hideWindow()` | ✓ кліки по власній поверхні launcher |
| Запуск застосунку | `launcherModel.applicationLaunched` → `launcher.hideWindow()` (204-210) | ✓ |
| Втрата фокусу | `onActiveChanged` → `QWindow::hide` (launcher.cpp:205-209) | ✓ — активується інше вікно → launcher ховається |

Нюанс Wayland: клік по **dock** (KeyboardInteractivityNone) не знімає активність з launcher
(на X11 знімав — dock був звичайним вікном, що приймає фокус) → «клік по панелі закриває
launchpad» сам по собі не спрацює; зате клік по іконці dock запускає застосунок → той стає
активним → launcher ховається. Додаткових механізмів не вводимо (поза межами порту).

Дрібниці: `m_hideTimer` (200 мс, launcher.cpp:59-62) **ніколи не стартує** — мертвий код старої
«fade-out» ідеї; анімації в main.qml закоментовані ще upstream. Ховається launcher миттєво.

---

## 0.7 Як dock зараз запускає launcher (ПИТАННЯ 7)

- У dock є фіксований пункт `cutefish-launcher` (dock `applicationmodel.cpp:617-619`:
  `id`/`exec` = `"cutefish-launcher"`, іконка `qrc:/images/launcher.svg`, `fixed=true`).
- Клік → `openNewInstance("cutefish-launcher")` → `launchArguments(...)` → `ProcessProvider::startDetached`
  → `QProcess::startDetached("cutefish-launcher")` (PATH-пошук).
- **Зараз не працює — саме так, як і очікувано:** бінарника ніде немає (`startDetached` повертає
  false, dock нічого не логує). Після встановлення в `~/.local/bin/cutefish-launcher` запуск
  запрацює, але з нюансом холодного старту з 0.2 (перший клік — прихований процес).
- **Launchpad-детекція в dock:** `src/activity.cpp:63-64` —
  `m_windowClass = iface->activeWindowClass(); bool launchPad = m_windowClass == "cutefish-launcher";`
  — у Wayland-порті dock `activeWindowClass()` повертає `appId()` активного вікна → **appId
  launcher-вікна ОБОВ'ЯЗКОВО має бути `"cutefish-launcher"`**, інакше зламається
  launchpad-детекція (показ/IntellHide-логіка dock).
- dock також виключає вікно launcher зі своєї моделі: `applicationmodel.cpp:717`
  (`id == "cutefish-launcher"` → skip) — тож присутність вікна в трекінгу не завадить.
- **appId на Wayland:** `main.cpp:59` — `setApplicationName("cutefish-launcher")`; .desktop-файла
  в репо немає → `desktopFileName` порожній → QtWayland бере `applicationName` → runtime appId =
  `"cutefish-launcher"`. **Перевірити на рантаймі на кроці 2.**

---

## Підсумкова таблиця X11-only / зламаних точок

| Файл | Конструкція | Заміна на Wayland |
|---|---|---|
| `src/launcher.cpp` `showEvent` | `KX11Extras::setState(winId(), NET::SkipTaskbar \| NET::SkipPager)` | прибрати (еквівалента немає; шкоди немає — dock ігнорує вікно) |
| `src/launcher.cpp` `updateSize` | `setGeometry(primaryScreen geometry)` — «повноекранність» | `showFullScreen()` (xdg-shell fullscreen) |
| `CMakeLists.txt` | `KF6WindowSystem` + `KF6::WindowSystem` (тільки для KX11Extras) | прибрати зі збірки |
| `qml/main.qml` | `import Cutefish.System 1.0` + `System.Wallpaper` | `Cutefish.Appearance` (framework) — див. 0.5 |
| `src/main.cpp:80` | хардкод `/usr/share/cutefish-launcher/translations` | шлях відносно префікса (`QStandardPaths`) — при `~/.local`-інсталяції переклади зараз не знайдуться |

**Не-X11, але функціональні:** список застосунків лише з `/usr/share/applications` (0.3);
холодний старт кнопки dock (0.2/0.7).

**Мертвий код (опційно, на крок 1):** `src/applicationlistmodel.*` (включає неіснуючий
`launcheritem.h`; до SRCS не входить — просто сміття), `src/ucunits.*` (копія Ubuntu UI
Units, у QML не зареєстрована й не вживається; тільки компілюється), `m_hideTimer`,
закоментовані анімації в main.qml.

## HiDPI baseline (issue #19 — розмиті іконки)

Поточний монітор: eDP-1, **Scale: 1** (`kscreen-doctor`). Тобто масштабування HiDPI не
задіюється: `IconItem` рендерить іконки з `qApp->devicePixelRatio()` (=1.0) — розмиття на
цьому моніторі не відтворюється. Фіксується як baseline, «полагодити» без розуміння причини
не намагаємось (як і в завданні).

## План змін (Крок 1 + Крок 2)

1. **launcher** (крок 1, окремі коміти):
   - прибрати `KX11Extras` і `KF6::WindowSystem`; повноекранність через `showFullScreen()`;
   - QML: `Cutefish.System` → `Cutefish.Appearance` (варіант (a));
   - XDG-каталоги застосунків (0.3); шлях перекладів (0-таблиця);
   - збірка в `/tmp/opencode/build-launcher-dbg`, встановлення в
     `~/.local/bin/cutefish-launcher` (той самий prefix, що й dock/fishui).
2. **cutefish-framework** (крок 1): збірка/інсталяція QML-модуля `Cutefish.Appearance` у
   `~/.local`; фолбек шпалери Plasma в `appearance` (патерн `bb744d1`), якщо йдемо варіантом (a).
3. **dock** (крок 2): кнопка launcher — `--show`/D-Bus-toggle логіка (0.7); наскрізний тест кліком.
4. Тестування: **виключно в поточній Plasma 6 Wayland-сесії** — клік по кнопці dock відкриває
   launcher; список застосунків (включно з `~/.local`); appId == `"cutefish-launcher"`
   (launchpad-детекція в dock); закриття (Escape / клік / фокус); пошук; HiDPI baseline.