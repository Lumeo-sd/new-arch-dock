# Крок 2: Launcher — Qt6/KF6 Wayland port (Plasma 6)

Гілка: `fork-qt6-wayland` (крок 1 — `735712b`…`ccc7865`; крок 2 — `790c593` фікс розміру кореня QML)
Framework: `cutefish-framework` гілка `fork-qt6-wayland` (коміти `bb744d1` darkMode-fallback, `b8b7468` Plasma-шпалера + селективна збірка, `143fe6a` KConfig-парсер + RPATH)
Dock: `fork-qt6-wayland` (робоче дерево + `--show` для кнопки launcher, не закомічено)
Upstream base: `bedff0a` (Qt6-порт launcher, X11-only на рантаймі)

## Що змінилось («було → стало»)

| Область | X11/upstream | Wayland-порт |
|---|---|---|
| Розмір/стан вікна | `setGeometry(primaryScreen geometry)` + `setVisible` | `showMaximized()` (xdg-shell maximized) + `QQuickView::resizeEvent(e)` у перевизначенні (фікс `790c593`). Повноекранний режим прибрано: top-panel (`0,0,1920,32`) і dock (`351,1017,1218×58`) лишаються видимими, лаунчер читається як «сторінка» в робочій області, а не фонове полотно |
| Skip taskbar/pager | `KX11Extras::setState(NET::SkipTaskbar\|NET::SkipPager)` у `showEvent` | прибрано (еквівалента немає; шкоди немає — dock ігнорує вікно, `applicationmodel.cpp:717`) |
| Збірка | `KF6WindowSystem` link (тільки для KX11Extras) | прибрано з `find_package`/`target_link_libraries`; додано `GNUInstallDirs` |
| Шпалера | `import Cutefish.System 1.0` + `System.Wallpaper` (модуля немає на Plasma) | `Cutefish.Appearance` (локальний framework, той самий API `color/path/type`) + фолбек шпалери Plasma в `appearance` (патерн darkMode-fallback; KConfig парситься рядковим парсером секцій, фікс `143fe6a`) |
| Список застосунків | лише `/usr/share/applications` | усі XDG-каталоги (user-first, flatpak exports, стандартні каталоги, дедуп; watcher на всі наявні) — патерн dock `SystemAppMonitor::applicationDirectories()` |
| Переклади | хардкод `/usr/share/cutefish-launcher/translations` | `QStandardPaths::locateAll(GenericDataLocation, ...)`; інсталяція в `${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/translations` → при `~/.local`-префіксі кладуться в `~/.local/share/cutefish-launcher/translations` |
| Розмір вікна на старті | X11 сам розміщував вікно через `setGeometry` до завантаження QML | **`showMaximized()` ДО `setSource()`** + `setVisible(false)` для cold-start — інакше QML грузиться в view 0×0 і вікно показує порожній шар (коміт `ccc7865`); **корінь QML розмірюється лише через базовий `QQuickView::resizeEvent`** — перевизначення тепер передає подію базі (коміт `790c593`), інакше reactive maximized `configure(1920,1080)` не доходить до кореня і весь контент (шпалера + сітка) лишається в маленькій top-left області |
| Кнопка launcher у dock | `exec = "cutefish-launcher"` → холодний старт = невидимий процес | `exec = "cutefish-launcher --show"` → перший клік відкриває; повторний клік тоглить через D-Bus single-instance. Гонка «focus-loss ↔ пізній toggle» (клік по відкритому launcher знову його показував) закрита в `toggle()` маркером same-click: якщо launcher прихований focus-loss і toggle прийшов протягом 400 мс — лишається прихованим (заміряний dock round-trip ≈260 мс); маркер скидається при свідомому hide/show. Ховання за фокусом — лише коли `m_showed==true`, щоб пізній `activeChanged` після toggle її не «отруїв» |
| Контекстне меню іконки (right-click) | `DesktopMenu` напряму в кожному делегаті → десятки `MenuPopupWindow` (Wayland xdg-popup поверхні) створюються на старті; сигнали `opened/closed` (Qt Popup API) на `MenuPopupWindow` відсутні — `onClosed` ламав QML, сітка не створювалась | меню в `Loader { active: false }` — popup-поверхня створюється лише при першому right-click; сторож `onVisibleChanged: launcher.setContextMenuOpen(visible)` (DesktopMenu = `MenuPopupWindow`, стандартних Popup-сигналів не має — працює через видимість); ініціалізація пунктів у функції `prepare()` всередині компонента (id у `Component` ззовні недоступні: `Loader.item.uninstallItem` = undefined) |
| Перегортання сторінок (тачпад-свайп / колесо миші) | нативний `ListView` (`SnapOneItem` + highlight + `scrollAnim` через `NumberAnimation`): Flickable сам крутив контент жестами — малий свайп робив snap-back («нічого не сталось»), сильний пролітав у порожнечу після останньої сторінки, `indexAt()==-1` у `onStopped` скидав `currentIndex=0` («цикл по колу») | строгий пейджер «одна сторінка за жест»: Flickable повністю вимкнено (`interactive:false`), `contentX` жорстко прив'язаний до `currentIndex` (`Math.max(0,currentIndex)*width` + `Behavior`), `StopAtBounds`, `highlightFollowsCurrentItem:false` (ListView не позиціонує контент сам при зміні currentIndex — інакше backward стрибав різко в обхід Behavior), wheel приймає фронтовий `MouseArea { z:10, acceptedButtons: Qt.NoButton }` (кліки проходять на іконки), напрямок — за домінантною віссю `angleDelta`; zero-дельта події (SmoothScroll transitions) ігноруються без торкання burst-таймера; dead-zone 90 відсікає jitter тачпада; burst-guard `Timer` (500 мс, `repeat:false`, рестарт на кожну подію) злипає пачку smooth-подій тракпаду в ОДИН жест, а механічне колесо (нотч = `angleDelta` кратне 120) оминає таймер повністю — кожне клацання гортає одразу, швидке крутіння = по сторінці за ноток; `onCountChanged` клампить `currentIndex` у `[0, count-1]` |

## Framework: збірка лише потрібного модуля

Повна збірка `cutefish-framework` на цій машині неможлива:
`bluetooth` → `KF6BluezQt`, `network` → `KF6NetworkManagerQt`+`KF6ModemManagerQt`,
`screen` → `KF6Screen` (пакети *-devel відсутні, sudo недоступний). Додано селектор
модулів (зворотно сумісно, за замовчуванням — усі):

```
cmake -S cutefish-framework -B /tmp/opencode/build-framework \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX:PATH=$HOME/.local \
  -DINSTALL_QMLDIR=$HOME/.local/lib64/qt6/qml \
  -DCUTEFISH_FRAMEWORK_BUILD_MODULES=appearance
cmake --build /tmp/opencode/build-framework -j$(nproc)
cmake --install /tmp/opencode/build-framework
```

Результат: `~/.local/lib64/qt6/qml/Cutefish/Appearance/` (qmldir, plugin `.so`,
`.qmltypes`, RPATH `$ORIGIN/../../../lib64`), `<prefix>/lib64/libcutefish-framework-appearance.so`,
заголовки в `<prefix>/include/Cutefish/`. Модуль не «бореться» з fishui-інсталяцією
(блок QML-модуля спрацьовує лише коли `PROJECT_NAME == "CutefishFramework"`).

## Перевірено на Plasma 6.7.5 (Fedora 44, Wayland)

- **appId на проводі** (WAYLAND_DEBUG): `xdg_toplevel.set_app_id("cutefish-launcher")`
  → runtime appId дорівнює `"cutefish-launcher"`, як вимагає launchpad-детекція dock
  (`activity.cpp:63-64`); `setApplicationName` + порожній `desktopFileName` — контракт з 0.7 виконано.
- **Максимізований режим (не fullscreen)**: три `showFullScreen()` → `showMaximized()`;
  `hideWindow()` → звичайний `setVisible(false)` (безпечно: maximized xdg-toplevel
  на Wayland коректно ховається, на відміну від залишеної на композиторі fullscreen
  surface). Top-panel і dock лишаються видимими над/під «сторінкою» launcher.
- **Рендер контенту** (фікс `ccc7865`): скриншот 1920×1080, 92 799 унікальних кольорів,
  у центрі — контент сітки; до фіксу вікно було порожнім чорним шаром.
- **Фон на робочу область** (фікс `790c593`): `spectacle -a` власної surface launcher —
  alpha>0 по всій робочій області, RGB = затемнена розмита шпалера + кольори іконок;
  до фіксу контент малювався лише в top-left ~500×540 і решта surface була прозора
  (`launcher-win.png`).
- **Свіжий toggle e2e** (один екземпляр, D-Bus hide/show, попарні скриншоти в ту ж
  хвилину): show-vs-hide 79.1% пікселів різняться (backdrop + сітка поверх робочого
  столу), hide показує чистий робочий стіл, show відновлює фон + сітку; процес живий
  5+ хв через hide/show цикли. (`hide-now7`/`show-now7`/`show-now8` в `/tmp/opencode/`.)
- **Кнопка dock → launcher, наскрізний e2e** (uinput-ін'єктор з абсолютними осями,
  відкалібрований 1:1 проти `wl_pointer#30.enter`): холодний клік при зупиненому
  процесі спавнить `cutefish-launcher --show` (новий pid, володіє D-Bus name,
  `activeChanged true`); далі цикл кліків по іконці (380,1046):
  закрити → відкрити → закрити, попарні скриншоти `diff ≥ 65%` пікселів робочої
  області після кожного кліку; процес живий увесь цикл. Пінг-понг (клік буцімто
  «відкриває» щойно закритий launcher) не відтворюється: focus-loss приходить від
  KWin ІЗ запізненням (~260 мс), toggle-маркер same-click тримає 400 мс.
- **Single-instance toggle**: перший `--show` — процес живий; другий `--show` — миттєвий
  вихід (код 255, D-Bus `registerService` не вдався → `toggle()` першому); перший лишається
  живим; після toggle скриншот = робочий стіл (вікно приховане).
- **QML-імпорти**: чистий stderr — `Cutefish.Appearance` резолвиться з `~/.local/lib64/qt6/qml`
  (done dock/fishui proof).
- **D-Bus**: `com.cutefish.Launcher` і `com.cutefish.Dock` реєструються в сесії.
- **Бінарники**: `~/.local/bin/cutefish-launcher` (з фіксами `ccc7865`+`790c593`),
  `~/.local/bin/cutefish-dock` (з `--show`); обидва в PATH.
- **Launcher-кнопка dock на Wayland** (класичний сценарій): фінальний e2e на
  Plasma 6.7.5 Wayland: холодний spawn → launcher миттєво на екрані (mean 55/sat 5.9%
  проти hidden 36/2.5 — тільки після вимкнення таймера/ембеддинг у QML кореневий
  об'єкт повноцінний); toggle OPEN→CLOSED→OPEN×3 через dock (293, 1046) чисто
  через `workspace.windows` ground truth; жодних TypeErrors чи QML-помилок у логу.
  `workspace.windowList()` НЕ бачить вікна launcher (KWin 6 баг/специфіка — launcher
  доступний через `workspace.windows` або `stackingOrder`); детектори яскравості/насиченості
  підлаштовані під темні шпалери та дрейф слайд-шоу (mean+sat 2-pixel heatmap).
- **Свайп e2e (фікс пейджера, uinput ABS-wheel + pageindicator-оракул, авторитетний
  DBG-лог)**: три батареї по 6-9 кроків (S1-S9 на debug-білді, C1-C6 і F1-F6 на чистому
  фінальному бінарнику без QT_LOGGING_RULES): малий (5 notches) і сильний (25 notches)
  свайп в обидва боки = **рівно 1 сторінка**; на останній/першій сторінці forward/backward
  **не циклять** («цикл по колу» зник: сильний свайп на останній сторінці лишає її);
  жодного snap-back («нічого не сталось» зник: 5-notches свайп завжди крокує); burst
  25 notches = 1 крок (500 мс + restart); вертикальні свайпи теж гортають (домінантна вісь);
  після всіх тестів лаунчер показаний (повна сітка 55/11.0/5.9), page indicator збігався
  з DBG-логом на кожному кроці.

## Ключові знахідки

1. **Порядок показу до QML-завантаження — критичний на Wayland, але недостатній.**
   QQuickView (`SizeRootObjectToView`) розмірює кореневий елемент за розміром *вікна*.
   На X11 `setGeometry(скрін)` викликався до `setSource`, тому корінь грузився вже
   правильним. Wayland не дозволяє розмістити toplevel — тільки maximized-стан (як і
   fullscreen — будь-який стан із розміром, який задає композитор), тому
   `showMaximized()` обов'язково ДО `setSource()` (`ccc7865`), інакше — прозоре/чорне
   вікно без контенту. Але навіть після цього **розмір кореня на Wayland
   не оновлювався**: maximized `configure(1920,1080)` приходить асинхронно ПІСЛЯ
   завантаження QML, а upstream-перевизначення `Launcher::resizeEvent` ковтало подію
   без виклику бази (`e->ignore()` тільки), тож `SizeRootObjectToView` не спрацьовував:
   уся сцена лишалась у початковому розмірі, контент (шпалера+сітка+розмиття) ютився
   в маленькій top-left області ~500×540, решта surface була прозора. Фікс `790c593`:
   першим рядком `QQuickView::resizeEvent(e)` — база оновлює корінь до реального
   розміру вікна. (X11-WM примусово задавав розмір синхронно — саме для цього був
   старий ignore.) <br>Визначення root-cause: `spectacle -a` власної surface launcher —
   об'єктивний ground truth проти забруднених композитів динамічного робочого столу.
2. **`--show` на кнопці dock = правильна toggle-семантика.** Холодний клік відкриває
   launcher (перший екземпляр показується сам), повторний клік спавнить другий
   екземпляр, який бачить зайнятий D-Bus name і тоглить перший — тож кнопка працює
   як відкрити/закрити без змін у launcher.
   <br>**Гонка focus-loss ↔ toggle** (виявлено при e2e): клік по відкритому launcher
   викликає і приховання за фокусом (`activeChanged`), і toggle від dock. Порядок
   недетермінований — focus-loss може прийти ДО toggle (значення ~270 мс у першому
   прогоні) або ПІСЛЯ нього (13 мс у свіжому). Якщо focus-loss виграє, toggle при
   `m_showed==false` «відкриває» щойно закритий launcher — пінг-понг, кнопка не змогла
   б його ніколи закрити. Фікс (`launcher.cpp`): маркер `m_hiddenByFocusLoss` +
   мітка часу в `onActiveChanged` (тільки коли launcher був показаний), у `toggle()`
   при `m_showed==false` і свіжому маркері (<400 мс — покриває заміряний dock
   round-trip ≈260 мс) — залишити прихованим і скинути маркер; свідомий hide у
   `toggle()` теж скидає маркер.
3. **XDG-скан потрібен, бо Qt доповнює каталоги** — `QStandardPaths` додає
   `/usr/local/share` і `/usr/share` як fallback навіть коли `XDG_DATA_DIRS` задано
   інше, а user-data завжди перший → пріоритет користувача зберігається.
4. **HiDPI (issue #19)**: монітор eDP-1 `Scale: 1` → масштабування не задіюється,
   розмитість не відтворюється; записано як baseline, «фікса» немає (як і в завданні).
   (На проводі `preferred_scale(120)` = fractional 1.2 ігнорується; buffer лишається
   1920×1080.)
5. **KConfig-парсер шпалер** (`143fe6a`): QSettings не вміє читати bracket-nested
   синтаксис KDE (`[Containments][1][Wallpaper][org.kde.image][General]` — кожна пара
   дужок = окрема група, `childGroups()` порожній). Рядковий парсер секцій натомість
   знаходить `Image=` під `[Wallpaper][org.kde.image][General]` (плагін image першим,
   color — фолбек) → `wallpaper=[шлях] backgroundType=0`.
6. **Мертвий код** (з аудиту, не прибрано): `src/applicationlistmodel.*` (порожній,
   не в SRCS), `src/ucunits.*` (компілюється, не вживається), `m_hideTimer` не
   стартується, закоментовані анімації main.qml — опційне прибирання.
7. **`MenuPopupWindow` не має сигналів `opened/closed`.** `DesktopMenu` у FishUI —
   це `MenuPopupWindow` (кастомний QQuickItem/Window-клас), а не стандартний Qt
   Popup: `onClosed` у `GridItemDelegate` був QML-помилкою «Cannot assign to
   non-existent property» → `AllAppsView` не інстанціювався взагалі → лаунчер
   працював з ПОРОЖНЬОЮ сіткою (порожній екран + артефакт), при цьому протокол
   Wayland виглядав здоровим (configure/attach/commit йдуть). Діагностика: spawn з
   захопленням stderr — помилка з'являлась миттєво.
8. **Eager per-delegate `DesktopMenu` = стоппер першого кадру.** Кожен
   `MenuPopupWindow` — окрема Wayland xdg-popup поверхня зі своїм scene-graph
   контекстом: ~40-80 таких на старті (видимі сторінки + cache-буфер) зупиняли
   рендер на десятки секунд — main/render потоки idle, головний кадр не мапився
   (процес живий, `QQuickWindow` каже exposed=true, а поверхні на композиторі
   немає). Фікс: `Loader { active: false }` навколо меню — popup створюється лише
   при першому right-click (одноразово на делегат). Після фіксу перший кадр на
   холодному spawnі ≈ 1 с (протокол: frameSwapped з перших секунд).
9. **Id у `Component` недоступні ззовні об'єкта.** Після перенесення меню в
   `Loader.sourceComponent`, звернення `menuLoader.item.uninstallItem.visible = ...`
   з делегата кидало «Value is undefined and could not be converted to an object»
   (пункти меню — у scope компонента, не назовні). Фікс: функція `prepare()` всередині
   меню виконує всю ініціалізацію у власному scope.
10. **Ground truth присутності launcher на екрані** — `workspace.windowList()`
    (KWin 6) launcher НЕ показує (див. перевірку вище), але `workspace.windows`/
    `stackingOrder` — показують; для автоматичних тестів використано останні +
    mean/sat 2px-детектор (шпалери темні та дрейфуючи, brightness-threshold сам по
    собі не розрізняє shown/hidden).
11. **Flickable-навігація несумісна з «одна сторінка за жест».** Upstream використовував
    нативний `ListView` (`SnapOneItem` + highlight + `scrollAnim`): Flickable отримує wheel
    і сам гортає контент, а навігація по сторінках наздоганяє анімацію. На Wayland (Plasma 6)
    тракпад-свайп = пачка wheel-подій, і поведінка розпадається на два дефекти: малий свайп
    не дотягує до порогу snap і повертається назад («нічого не сталось»), сильний пролітає
    ПОВЗ останню сторінку в порожнечу контенту, де `indexAt()` повертає -1 і fallback
    `currentIndex=0` зациклює перші-останні сторінки («цикл по колу»). Фікс: вимкнути
    Flickable повністю (`interactive:false`), `contentX` жорстко = `currentIndex*width`
    (пейджер, а не список), `StopAtBounds`, а wheel приймати фронтовим `MouseArea{z:10}`.
    Тоді «сторінка» — не результат анімації, а інваріант стану: свайп або крокує на 1, або
    нічого (межа). Нюанси QML: `acceptedButtons: 0` = runtime-помилка «Unknown enumeration» —
    потрібен `Qt.NoButton` (0 у QML-енумерації не резолвиться); пачка тракпаду — це SmoothScroll
    події (~12 angleDelta-одиниць на піксель, ніколи не кратні 120), burst-guard з `restart()`
    ковзає вікно разом з найдовшим жестом: сильний/повільний свайп (100+ подій) досі = один крок;
    механічне колесо шле дискретні нотатки `angleDelta` кратні 120 — такі події оминають burst-guard
    повністю (`notch`-перевірка), тож швидке крутіння гортає по сторінці за клацання, а тачпад
    лишається «1 жест = 1 сторінка»; dead-zone 90 відсікає jitter (реальний тачпад у спокої дає
    adx 20–61, свайп — adx 136–456); zero-дельта події (settle/stop transitions) не торкаються
    таймера — інакше перша (0,0) подія стартувала б guard і ковтала весь жест. Напрямок за
    домінантною віссю (`angleDelta.x` при горизональному свайпі, `y` — вертикальному, у т.ч.
    колесу), бо тракпади можуть давати обидві осі в діагональному жесті. Знаки: natural-scroll
    тачпад дає swipe-left = `angleDelta.x > 0` = next; вертикаль — `y < 0` (колесо вниз /
    два пальці вгору) = next.

## Як запустити/перевірити

- Збірка/встановлення launcher:
  ```
  cmake -S launcher -B /tmp/opencode/build-launcher-dbg \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX:PATH=$HOME/.local
  cmake --build /tmp/opencode/build-launcher-dbg -j$(nproc)
  cmake --install /tmp/opencode/build-launcher-dbg
  ```
- QML-модуль: див. блок «Framework» вище (щоразу після змін в `appearance`).
- Dock після збірки: `pkill -x cutefish-dock; sleep 1` → `cp` бінарника окремо
  (інакше "Text file busy") → автозапуск `~/.config/autostart/cutefish-dock.desktop`
  або ручний запуск.
- Далі: юзерський наскрізний тест (клік по кнопці dock → launcher; Escape/клік →
  закриття; пошук; запуск застосунку; launchpad-детекція dock).

## Що лишилось

- Launcher-кнопка dock: наскрізний e2e виконано (холодний spawn + закрити/відкрити/
  закрити, диффи ≥65%, процес живий; після фіксу lazy-меню — повторно верифіковано).
- **Контекстне меню** (цієї сесії): eager `DesktopMenu` → `Loader { active:false }`
  (фікс стоппера першого кадру), `onOpened/onClosed` → `onVisibleChanged`
  (фікс QML-помилки і порожньої сітки), `prepare()` для ініціалізації в scope
  компонента. E2E: right-click → xdg_popup під курсором, launcher лишається
  відкритим, клік назовні закриває меню (launcher відкритий). Потребує юзер-тесту.
- Dock working-tree зміни (drag&drop + `qInfo()` debug-логи та `clicked()`
  minimize/activate) — розібрати й закомітити окремим комітом.
- **Свайп на реальному тачпаді — підтверджено юзером** (21:0x, живий девайс):
  свайп 2 пальцями по/вниз = рівно 1 сторінка за жест, обидва напрямки плавно,
  на межах зупинка; колесо миші — швидке крутіння гортає по сторінці за клацання
  без пауз (нотч ±120 оминає burst-guard). Автоматизовано покрито (S/C/F батареї,
  uinput-ін'єктор, див. знахідку 11).
- Наскрізний юзер-тест кліком у GUI (фінальний чекпоінт цього кроку).
- Крок 3: docs — CLAUDE.md/PROGRESS.md оновлено (коміт нижче, одним комітом).