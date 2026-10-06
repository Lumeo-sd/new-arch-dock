# Крок 0 — Аудит: порт CutefishOS dock на Qt6/KF6 + Plasma 6 Wayland

Дата: 2026-09-12
Система: Fedora 44 (KDE Plasma Desktop), Plasma 6.7.5, Qt 6.11.2, сесія — real Wayland
(`XDG_SESSION_TYPE=wayland`, `WAYLAND_DISPLAY=wayland-0`), KWin 6.7.5.

---

## 0.0 Важлива передумова: стан репозиторіїв upstream

Усі три репозиторії заархівовані, але їх `main`-гілки вже містять **частковий/повний Qt6-порт**,
зроблений авторами перед архівацією:

| Репо | Стан upstream |
|---|---|
| `cutefishos/dock` | Комміт `6df50a5 "fix(dock): port dock to Qt6"` — CMake переведено на Qt6/KF6, додано compat-шим `compat/QX11Info`, QML-імпорти переведено на `Qt5Compat.GraphicalEffects`. **Але весь X11-специфічний C++ залишився неторканим** (KX11Extras, NETWM, XWindowInterface). |
| `cutefishos/fishui` | Вже повністю Qt6/KF6: Qt6, KF6WindowSystem, KWayland::Client, власні **Wayland-реалізації blur (`org_kde_kwin_blur`) і shadow (`KWindowShadow` + `org_kde_kwin_shadow`)**. Видалено X11-бекенди (коміти `6c7e5f3`, `394028f`). |
| `cutefishos/libcutefish` | Вже Qt6 (project `CutefishFramework`, мінімум Qt 6.0.0), модулі перенесені на `qt_add_qml_module`. |

Тобто завдання фактично зводиться не до "Qt5→Qt6" як такого (він переважно виконаний upstream),
а до **доведення dock до реальної роботи на Plasma 6 Wayland**: заміна X11-only логіки
(трекінг вікон, "бути панеллю", стрaти, фейкове вікно) на Wayland-еквіваленти
(plasma-window-management / KWindowSystem, LayerShellQt, exclusive zone) та
виправлення розбіжностей API fishui ↔ dock (QML-властивості, що upstream видалив).

---

## 0.1 Як dock отримує список вікон / запущених застосунків (ПИТАННЯ 1)

**Відповідь: ЧЕРЕЗ ПРЯМІ X11-виклики (не кросплатформний KWindowSystem). Потребує переписування.**

Ланцюг:

1. `ApplicationModel` (композиційна модель dock) підписується на сигнали `XWindowInterface`
   (`windowAdded`, `windowRemoved`, `activeChanged`) — `src/applicationmodel.cpp:31-33`.
2. `XWindowInterface` (`src/xwindowinterface.cpp`) — увесь трекінг побудовано на X11:
   - `KX11Extras::self()` сигнали `windowAdded`, `windowRemoved`, `activeWindowChanged` (X11-only),
   - `KX11Extras::windows()` / `KX11Extras::activeWindow()` — перелік вікон,
   - `KWindowInfo` + `NET::WM*`-маски (`NET::WMWindowType`, `NET::WMState`, `NET::WM2WindowClass`,
     `NET::WM2DesktopFileName`…) — стан/властивості вікон,
   - прямі X11-виклики через `QX11Info::connection()`:
     - `NETRootInfo(...).closeWindowRequest(id)` — закриття вікна,
     - `NETWinInfo(...)` для PID i `desktopFileName`,
     - `NETWinInfo::setIconGeometry()` — геометрія іконки для taskbar-підсвітки.
   - `isAcceptableWindow()` — фільтрація типів вікон через `NET::WindowTypeMask`
     (Desktop/Dock/Splash/Toolbar/Menu/PopupMenu/Notification виключаються) — X11-only семантика.
3. `Activity` (`src/activity.cpp`) — визначення "є розгорнуте вікно" (для IntellHide)
   і "відкритий launchpad":
   - `KX11Extras::activeWindow()`, `KX11Extras::windows()`,
   - `KWindowInfo(...).hasState(NET::MaxVert | NET::MaxHoriz)`, `NET::SkipTaskbar`, `isMinimized()`,
   - `info.windowClassClass() == "cutefish-launcher"` — детект запускача.
4. Зіставлення вікна з .desktop-файлом (`Utils::desktopPathFromMetadata`, `src/utils.cpp`)
   йде через PID (`/proc/<pid>/cmdline`) + WM_CLASS евристики — сам /proc-парсинг кросплатформний,
   але вхідні дані (pid, class) беруться з X11-джерел (`NETWinInfo`, `KWindowInfo`).

**Під Wayland: `KX11Extras` не працює взагалі** (його Wayland-аналог у KWindowSystem KF6 —
плагін `KF6WindowSystemKWaylandPlugin`, який ходить у KWin через протокол
`org_kde_plasma_window_management`). Переписування: класи `PlasmaWindowManagement` /
`PlasmaWindow` (`<KWindowSystem/…>`) + сигнали `KWindowSystem`.

---

## 0.2 Як dock оголошує себе панеллю і резервує місце (ПИТАННЯ 2)

**Відповідь: 100% X11-only — йде на заміну (NET::Dock + _NET_WM_STRUT_PARTIAL).**

- `src/mainwindow.cpp:57`: `KX11Extras::setType(winId(), NET::Dock)` — тип вікна Dock.
- `XWindowInterface::setViewStruts()` (`src/xwindowinterface.cpp:146-188`):
  `KX11Extras::setExtendedStrut(...)` — `_NET_WM_STRUT_PARTIAL` аналог, обчислює ширину/старт/енд
  по краю з урахуванням `edgeMargins` і Round/Straight стилю.
- `clearViewStruts()` — обнулення стрyт.
- `src/fakewindow.cpp` — окреме невидиме "фейкове" вікно 5px біля краю екрана,
  яке ловить hover для показу прихованого dock (для AlwaysHide/IntellHide):
  `KX11Extras::setState(winId(), NET::SkipTaskbar | NET::SkipPager | NET::SkipSwitcher)` (X11-only).
  На Wayland таке вікно фізично неможливе (не можна розмістити X11-стиль вікно у фіксованій
  позиції поза поверхнею) — замінюється на "прихований стрип" самої layer-shell поверхні (як у
  Plasma auto-hide панелей).
- `KWindowEffects::slideWindow(...)` + `KWindowEffects::enableBlurBehind(...)` — KF6 має
  Wayland-реалізації через KDE-протоколи (`org_kde_kwin_slide`, `org_kde_kwin_blur`);
  blur для dock забезпечує `FishUI.WindowBlur` (власний KWayland-бекенд fishui).

Заміна (Крок 2): LayerShellQt (`LayerShellQt::Shell`/`Window`, наявний на системі:
бібліотека + QPA-плагін `wayland-shell-integration/liblayer-shell.so` + QML-модуль
`org.kde.layershell`):
- top-level вікно dock = layer-shell surface, `LayerTop`,
- анкори за напрямком (Bottom/Left/Right), плаваюча центрована панель (без Left+Right стрейчу
  для Round-стилю),
- `exclusive_zone` = товщина dock, коли видимий; `0`, коли прихований (авто-хайд/резервування),
- `KeyboardInteractivityNone`.

---

## 0.3 Що dock реально імпортує/викликає з fishui і libcutefish (ПИТАННЯ 3)

### 3.1 З FishUI (QML, `import FishUI 1.0`)

| Тип FishUI | Де використовується в dock | Стан upstream | Потрібна зміна? |
|---|---|---|---|
| `FishUI.WindowHelper` | `main.qml` — `windowHelper.compositing` | upstream **видалив** `compositing` (коміт `394028f`) | **ТАК** — повернути властивість `compositing` (щоб не чіпати QML dock) |
| `FishUI.WindowShadow` | `main.qml` — `view`, `geometry`, `strength`, `radius` | Є `view/strength/radius/enabled`, **немає `geometry`** | Додати no-op `geometry` (інакше QML-попередження) |
| `FishUI.WindowBlur` | `main.qml` — `view`, `geometry`, `windowRadius`, `enabled` | Є `view/enabled/windowRadius`, **немає `geometry`** | Додати no-op `geometry` |
| `FishUI.PopupTips` | `main.qml` + `DockItem.qml` | Є, вже адаптовано під Wayland | Ні |
| `FishUI.DesktopMenu` | `main.qml`, `AppItem.qml` (контекстне меню) | Є | Ні |
| `FishUI.IconItem` | `DockItem.qml` — `source` | Є (C++ тип, `source`/`color`) | Ні |
| `FishUI.Theme` | `Theme.darkMode`, `Theme.textColor` | Є (singleton QML над `FishUI.Core.ThemeManager`) | Ні |
| `FishUI.Units` | `Units.devicePixelRatio`, `smallSpacing`, `largeSpacing` | Є | Ні |

### 3.2 З libcutefish

- **Сам dock напряму libcutefish НЕ використовує** (жодного `#include` / посилання в CMake;
  QML — тільки власний модуль `Cutefish.Dock` + FishUI).
- **fishui залежить від libcutefish**: модуль `appearance` (`Cutefish::Appearance`) —
  `ThemeManager` (FishUI.Core singleton) читає `darkMode`, `accentColor`, `blurEnabled`,
  шрифти через `Appearance`. Тож libcutefish потрібен лише модулем `appearance`
  (достатньо `add_subdirectory(appearance)`, повна збірка всіх модулів не потрібна).
  `appearance` ходить у DBus `com.cutefish.Services` (немає на Plasma → працює з дефолтами,
  не падає; це окремо задокументовано).

### 3.3 Інші не-X11, але CutefishOS-специфічні залежності (ламають роботу на Plasma)

- `ProcessProvider::startDetached` — DBus `com.cutefish.Session` (немає на Plasma → запуск
  застосунків з dock не працює). **Потрібен fallback на `QProcess::startDetached`.**
- `TrashManager` — викликає `cutefish-filemanager` (немає на Plasma) → фолбек на `kioclient`
  (`kioclient5 exec trash:/` / `emptyTrash`), інакше кошик мертвий.
- Модуль `Cutefish.Dock` — власний (`DockSettings`, реєструється в `main.cpp`) — лишити як є.
- DBus-адаптор `com.cutefish.Dock` (add/remove/pinned + налаштування) — лишити як є
  (це ABI CutefishOS, працює й на Plasma).

### 3.4 Те, що НЕ треба чіпати (вже кросплатформне)

- Уся QML-логіка анімацій/вигляду (DockItem: магнітний zoom відсутній — там статичні
  делегати + activate-dot, анімації `moveDisplaced`, `Behavior on color`, дескриптор
  `ColorOverlay`; AppItem drag&drop; меню) — залишається оригінальною.
- `ApplicationModel` бізнес-логіка (піни, впорядкування, `openNewInstance`, `closeAllByAppId`).
- `DockSettings`, `SystemAppMonitor`, `TrashManager` (крім фолбеків), `/proc`-парсинг у `Utils`.

---

## 0.4 Повний перелік X11-only коду, що піде на заміну (підсумок)

| Файл | X11-only конструкції | Заміна на Wayland |
|---|---|---|
| `src/xwindowinterface.cpp/.h` | `KX11Extras` (windows/activeWindow/сигнали), `KWindowInfo`/`NET::*`, `NETRootInfo`, `NETWinInfo` (pid, icon geometry), `QX11Info` | `PlasmaWindowManagement`/`PlasmaWindow` (KF6, плагін KWayland), `KWindowSystem` сигнали |
| `src/activity.cpp/.h` | `KX11Extras::windows()/activeWindow()`, `NET::MaxVert/MaxHoriz/SkipTaskbar`, WM_CLASS | `PlasmaWindow::isMaximized()/appId()/skipTaskbar()`, `KWindowSystem::activeWindowChanged` |
| `src/mainwindow.cpp` `NET::Dock` | `KX11Extras::setType(winId(), NET::Dock)` | скасувати (роль панелі дає layer-shell) |
| `src/mainwindow.cpp` стрaти | `XWindowInterface::setViewStruts/clearViewStruts` | LayerShellQt `exclusive_zone` |
| `src/mainwindow.cpp` slide | `KWindowEffects::slideWindow` | KF6 KWindowEffects (Wayland через KDE-протокол) — перевірити; інакше пропуск на Wayland |
| `src/fakewindow.cpp/.h` | окреме вікно + `KX11Extras::setState(NET::Skip*)` | прихований стрип (скорочення layer-shell поверхні при хайді); fakewindow виключається зі збірки |
| `CMakeLists.txt` | `PkgConfig::XCB`, `compat/QX11Info` шим | прибрати; додати `find_package(LayerShellQt)` |

---

## 0.5 План змін (Крок 1 + Крок 2) — https://github.com/cutefishos/* локальні форки

1. **libcutefish** (`cutefish-framework/appearance`) — порт вже є; лише збірка/інсталяція.
   Мінімальних змін коду не потребує (лише перевірка збірки Qt6, може знадобитись
   `INSTALL_QMLDIR` override).
2. **fishui**:
   - повернути `WindowHelper::compositing` (dock QML),
   - додати no-op `geometry` для `WindowShadow`/`WindowBlur` (dock QML),
   - CMake: параметризувати `INSTALL_QMLDIR`; перевірити KWayland/LayerShell-звʼязки.
3. **dock**:
   - C++: переписати `XWindowInterface` на `PlasmaWindowManagement`/`PlasmaWindow`
     (зберегти сигнатуру API та бізнес-логіку `ApplicationModel` недоторканою),
   - `Activity` — на `KWindowSystem`/`PlasmaWindow` (максимізація, launchpad),
   - `MainWindow` — LayerShellQt (тип/анкори/exclusive zone/стрип для авто-хайду),
   - `ProcessProvider`/`TrashManager` — фолбеки для Plasma,
   - прибрати `compat/QX11Info`, `xcb`, `fakewindow.cpp` зі збірки.
4. Тестування: збірка + запуск **виключно в поточній Plasma 6 Wayland-сесії**.