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

## 2026-10-06: hover-превʼю вікон (жива стрічка над доком)

Наведення на іконку апки з `windowCount > 1` показує layer-shell стрічку
з живими мініатюрами вікон (PipeWire/KWin screencasting): клік — активувати,
`×` — закрити вікно. Згорнуті вікна показують заморожений останній кадр + дим;
якщо стріму нема взагалі — справжню іконку апки. Референс реалізації — Krema
(`~/Documents/New Folder/krema/src/krema-0.9.0`, док для Plasma 6 з такими ж
превʼю; розібрано `PreviewPopup.qml`, `PreviewThumbnail.qml`,
`shell/previewcontroller.cpp`, `main.qml`). COSMIC-перевірка:
`cosmic-app-list` живих мініатюр не має (там лише drag-preview), за зразок
не брався.

Файли: `dock/src/previewcontroller.{h,cpp}` (поверхня, позиція, маска, таймер
ховання 200мс), `dock/qml/PreviewPopup.qml` (striп, делегати, retry стрімів),
`dock/qml/AppItem.qml` (ховер → `showPreview`, гасіння тултіпа, ховання при
правому кліку/відкритому меню), `dock/src/applicationmodel.{h,cpp}`
(`windowInfos`/`activateWindowForApp`/`closeWindowForApp`),
`dock/src/xwindowinterface.cpp` (`uuid`+`minimized` у `requestInfo`),
`dock/src/mainwindow.{h,cpp}` (`appModel()`, `dockRect()`, контекст `preview`),
`dock/cutefish-dock.desktop.in` (дозвіл інтерфейсу, див. нижче),
`dock/CMakeLists.txt` + `dock/resources.qrc` (нові файли в збірці).

Уроки KWin/Wayland (все підтверджено логами і попіксельними скріншотами):
- `AppItem.mapToGlobal()` на layer-shell повертає window-local координати
  (вікно думає що воно в 0,0) — контролер додає `dockRect().topLeft()`.
- Без `X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1` у `.desktop`
  KWin мовчки не видає screencast-інтерфейс (`nodeId` завжди 0). Симптом
  у лозі: `Remember requesting the interface on your desktop file...`.
- `ScreencastingRequest.uuid` — голий id БЕЗ фігурних дужок (`{...}` KWin
  не знаходить: у Krema в журналі `error creating screencast "Could not
  find window id {...}"`).
- KWin садить overlay-поверхню вище exclusive-зони дока (~67px на
  1920x1080/eDP-1, виміряно построковою яскравістю скріншотів). Тому поверхня
  копіює плейсмент самого дока (ті самі anchors + відступ від краю) і
  тягнеться поверх дока, а попап малюється впритул (зазор 4px);
  компенсація — `exclusiveShift()` у `previewcontroller.cpp`.
- Anchors виставляються один раз в `initialize()` (зміна після configure
  ненадійна); margins/size — можна переписувати.
- `setMask(QRegion(0,0,1,1))` = блок вводу; ПОРОЖНІЙ QRegion = маска
  знімається і вся поверхня приймає ввід (навпаки від інтуїції).
- ListModel оновлюється інкрементально (`set`/`append`/`trim` по uuid),
  інакше `clear()` вбиває делегати і PipeWire-стріми не встигають
  прогрітись. Делегати з `nodeId==0` переозброюються (`uuid=""` → назад,
  до 6 спроб з інтервалом 1.5с).
- Тултіп іконки гаситься поки стрічка відкрита (`popupText=""` +
  `popupTips.hide()` — картки вже несуть заголовки вікон, а вікно тултіпа
  лежить на шляху курсора в стрічку). Правий клік ховає стрічку одразу;
  показ гейтиться на `!contextMenu.visible` (інакше re-enter під відкритим
  меню малює стрічку поверх нього).
- `rebuild()` слухає `appModel.dataChanged/rowsInserted/rowsRemoved`:
  закрита через `×` картка зникає одразу, решта звужуються і перецентровуються
  (`setContentSize → layout`); лишилось <2 вікон — модель чиститься,
  розмір нулиться, стрічка ховається (раніше без цього чищення стрічка
  «зʼїжджала» в x=0 і висіла).
- Debug-логи (`qInfo preview.*`, `windowInfos entry`, `nodeId`/`pipewire
  state` у QML) лишено свідомо — це діагностика стрімів, зносити коли
  PipeWire-шлях стане нудно-стабільним.

QA: синтетична миша через `ydotool` з увімкненим accel дає нестабільну
абсолютну позицію (крос через іконку → одразу leave) — валідація робилась
утриманням фізичного курсора + серією `spectacle -b -n` і построковим
заміром яскравості (`magick ... -resize 1xH! gray:-`). Очікувані сигнатури
в лозі: `hover showPreview`, `windowInfos ... wids: N`, `rebuild ...
windows= N`, `preview.show`, `nodeId: <id>`, `pipewire state: 4`
(= Streaming). Обмеження: у згорнутих вікон живого кадру нема за
конструкцією KWin (нема буфера) — показується останній доступний.
