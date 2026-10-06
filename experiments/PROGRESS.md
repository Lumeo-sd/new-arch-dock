# Прогрес порту cutefish-dock → Plasma 6 Wayland

> Журнал стану великої міграції. Оновлюється після кожного коміту.
> Ключові рішення/збірка/нюанси — у `CLAUDE.md` (стабільний контекст),
> цей файл — лише змінний статус.

## Стан (12.09.2026 — робоча сесія завершена)

- Dock працює в Plasma 6.7.5 Wayland: `visibility=0` (AlwaysShow),
  `primaryGeometry 0 0 464 58`, темна тема, запуск БЕЗ `LD_LIBRARY_PATH`.
- Автостарт налаштовано (`~/.config/autostart/cutefish-dock.desktop`).
- Drag&drop `.desktop` у dock реалізований.
- **Pinning підтверджений користувачем**: opencode (user-dir .desktop, id за StartupWMClass)
  і Obsidian (flatpak AppImage, StartupWMClass `md.obsidian.Obsidian`) додані через
  Pin-меню прямо в GUI (PID 78053, 17:23). Всі 7 DesktopPath валідні.

## cutefish-framework (libcutefish) — гілка `fork-qt6-wayland`

- [x] Аудит Qt5-специфічних місць (крок 0: `dock/STEP0-AUDIT.md`)
- [x] CMake → Qt6/KF6
- [x] C++ порт (appearance: daemon + локальний fallback `darkMode`)
- [x] Збірка проходить
- [ ] Запушено в remote (поки локально)

## fishui — гілка `fork-qt6-wayland`

- [x] Аудит використовуваних dock-ом компонентів
- [x] CMake → Qt6/KF6
- [x] QML import-версії / сумісність
- [x] Збірка проходить
- [x] Темна тема у dock (через `FishUI.Theme.darkMode`)
- [x] `INSTALL_RPATH=$ORIGIN/../../..` на `libFishUI.so` — запуск без
      `LD_LIBRARY_PATH` (потрібно для автостарту)
- [ ] Запушено в remote (поки локально)

## dock — гілка `fork-qt6-wayland`

- [x] Аудит X11-логіки (strut/NET::Dock)
- [x] CMake → Qt6/KF6
- [x] Заміна позиціювання на LayerShellQt (layer top, exclusive zone)
- [x] Трекінг вікон через KWayland `org_kde_plasma_window_management`
- [x] Збірка й запуск у реальній Wayland-сесії
- [x] Fix: мінімізація кліком (`KeyboardInteractivityNone`)
- [x] Fix: плавний show/hide (fade→resize, стрип ~2px замість unmapping)
- [x] За замовчуванням не ховається (`visibility=0`)
- [x] Темна тема dock
- [x] Автостарт при вході
- [x] Добавлення додатків drag&drop (`.desktop` URL → `addDesktopFile`)
- [x] Pinning усіх додатків: SystemAppMonitor сканує всі XDG-каталоги
      (+flatpak), матчинг спершу за appId (незалежно від pid)
- [x] Коміти: `9c0ccd4`, `c5680f5`, `20da50f` (локально, не запушено)
- [x] Priority 2 (STEP0): slideWindow верифіковано (KF6 Wayland — org_kde_kwin_slide;
      ефект ніколи не стріляє при map-forever стрипі → видалено як мертвий);
      fakewindow.cpp/.h видалено; compat/QX11Info нема; PkgConfig::XCB нема;
      KF6::WindowSystem (KWindowEffects slide+enableBlurBehind) прибрано з лінку
- [ ] Запушено в remote (поки локально)

## Відомі блокери / TODO

- **Візуальна перевірка drag&drop і кліків — потрібна реальна миша користувача.**
  Під сесією немає керування курсором (нема ydotool/xdotool/wtype; KWin
  scripting курсором не керує). Тест: перетягнути додаток з kіckoff на dock;
  перевірити Pin/Unpin у контекстному меню для програних додатків.
- ~~Кнопка launcher (`cutefish-launcher`) нічого не відкриває~~ → вирішено: launcher
  портовано на Qt6/KF6 Wayland (гілка `fork-qt6-wayland`, `launcher/STEP2-LAUNCHER-WAYLAND.md`),
  встановлено в `~/.local/bin/cutefish-launcher`, кнопка в dock запускає
  `cutefish-launcher --show` (холодний spawn) і тоглить відкрито/закрито повторним
  кліком; launcher відкривається максимізованим (top-panel + dock видимі).
- ~~Свайп/колесо у launcher: «нічого не сталось» або «цикл по колу»~~ → вирішено
  і **підтверджено юзером на живому девайсі**: строгий пейджер «одна сторінка за
  жест» (z:10 wheel-MouseArea, `interactive:false` + `contentX=currentIndex*width`,
  `highlightFollowsCurrentItem:false`, burst-guard 500 мс; деталі — знахідка 11
  STEP2-LAUNCHER-WAYLAND.md). Тачпад: обидва напрямки плавно, рівно 1 сторінка за
  свайп, межі тримаються. Колесо миші: кожне клацання (±120, нотч) гортає одразу,
  без пауз burst-таймера; швидке крутіння = по сторінці за ноток.
- Нема follow-up-запуску через `systemd --user` (зараз звичайний XDG-autostart).
- Опційно: `OnlyShowIn` автозапуску або делей до підняття kwin (вже є
  `X-KDE-autostart-after=plasma-desktop.service`).

## Коміти (fork-qt6-wayland, локально)

| Репо | Коміт | Що |
|---|---|---|
| dock | `b29ab37` | порт Qt6/Wayland (LayerShellQt+KWayland) |
| dock | `9c0ccd4` | focus fix + двофазний show/hide + конфіги |
| dock | `20da50f` | Priority-2: slide/blur KWindowEffects видалено, KF6::WindowSystem прибрано, fakewindow видалено |
| dock | `c5680f5` | multi-dir scan, appId-first, drag-pin |
| fishui | `8baaef3` | порт Qt6/Wayland (dock-компоненти) |
| fishui | `dd96285` | INSTALL_RPATH на FishUI plugin |
| framework | `bb744d1` | appearance fallback darkMode |
| launcher | `ccc7865`, `790c593` | Wayland port: maximized до setSource + корінь QML до реального розміру |
| launcher | `e4506b7`, `5ba7069`, `bc6d668` | живий процес, надійний toggle кнопки dock, docs |
| launcher | `e15e033`, `17f3e7b` | контекстне меню (lazy Loader + onVisibleChanged + prepare) |
| launcher | `4445bec` | «одна сторінка за жест»: пейджер `contentX=currentIndex*width`, `requestActivate()` у showWindow, docs STEP2 |
| launcher | `e2cdc9a` | плавні обидва напрямки: `highlightFollowsCurrentItem:false`, zero-delta skip, dead-zone 90, нотч-байпас колеса миші |