# Step 2: Dock — Qt6/KF6 Wayland port (Plasma 6)

Branch: `fork-qt6-wayland` (commit `9c0ccd4`; interactive-test fixes)
Framework: `cutefish-framework` branch `fork-qt6-wayland` (commit `bb744d1`, dark-mode fallback)
Upstream base: `6df50a5` (Qt6/KF6-compatible dock, X11-only at runtime)

## What changed

| Area | X11 original | Wayland port |
|---|---|---|
| Window tracking | `KX11Extras` / `NETWM` / `KWindowInfo` | KWayland `PlasmaWindowManagement` (`org_kde_plasma_window_management`) |
| Screen reservation | `NET::Dock` type + extended struts | LayerShellQt exclusive zone (floating, bottom-center, layer top) |
| Auto-hide | unmapped a fake X11 window | shrink to a ~2px edge strip + QML `opacity` fade (`dockHidden`) |
| Launch apps | `com.cutefish.Session` DBus daemon | `QProcess::startDetached` |
| Trash | `cutefish-filemanager --move-to-trash/-e` | `kioclient6` / `QFile::moveToTrash` / `rm` fallback |
| Launcher check | `NET::WM2WindowClass == "cutefish-launcher"` | `appId() == "cutefish-launcher"` |
| KWin permissions | — | `cutefish-dock.desktop` with `X-KDE-Wayland-Interfaces=org_kde_plasma_window_management` |

The QML (`qml/`), item model, settings, and D-Bus surface are otherwise
unchanged from upstream; GPL-3.0 + author attribution preserved.

## Verified on Plasma 6.7.5 (Fedora 44, Wayland)

- Window tracking: launch/close kwrite → dock width grows/shrinks (icon appears/disappears).
- Exclusive zone (AlwaysShow): maximized window stops at the dock edge.
- IntellHide auto-hide: maximize → strip `348×2` + maximized window takes the full
  height; unmaximize → dock expands back to full size.
- LayerShell anchors/margins: floating panel with the original edge margin.

## Key findings

1. **DockSettings::Visibility enum**: `AlwaysShow=0`, `AlwaysHide=1`, `IntellHide=2`.
   A config value of `1` (AlwaysHide) makes the dock hide unconditionally — the
   earlier "IntellHide stuck hidden" symptom was this misconfiguration, not a bug.
2. **KWin permission gate**: Plasma 6 keeps `org_kde_plasma_window_management` in
   `interfacesBlackList` by default. A client only receives the global if its
   desktop file (matched by canonical Exec path via `KApplicationTrader`)
   declares `X-KDE-Wayland-Interfaces=org_kde_plasma_window_management`, and if
   the compositor was started with `KWIN_WAYLAND_NO_PERMISSION_CHECKS` unset.
3. **QML import path on Qt6**: `QT_QML_IMPORT_PATH` is not honored at runtime;
   the private `~/.local/lib64/qt6/qml` prefix (FishUI) is added in
   `MainWindow`'s constructor via `engine()->addImportPath(...)`.
4. **KWayland connection**: use `ConnectionThread::fromApplication()` +
   `Registry::setup()` without a dedicated `EventQueue` — QtWayland only
   dispatches the default queue.

## Interactive-test fixes (commits `9c0ccd4`, `bb744d1`)

1. **Minimize-on-click did nothing** (regression found in manual testing). Root
   cause: the panel used `KeyboardInteractivityOnDemand`, so the moment the user
   clicked an icon the layer-shell surface became KWin's *active* window
   (`workspace.activeWindow` == `cutefish-dock`, verified via kwin scripting).
   Then `applicationModel.clicked()` compared `activeWindow()` against the
   clicked app's window id, never matched, and always fell through to
   `forceActiveWindow()` — the app was re-activated instead of minimized.
   Fix: `KeyboardInteractivityNone` (pointer events unaffected, like the
   original X11 dock). Verified with an env-gated self-test that drove the real
   `clicked()` path: after the fix `activeWindow()` resolves to the app and the
   window gets `minimized=true` in KWin.
2. **Auto-hide artifacts / rough reveal**. The old code resized the layer
   surface (full ↔ 2px strip) at the same time as the QML opacity fade, so the
   compositor showed artifacts on hide and the reveal snapped. Now the two
   phases are sequenced: hide fades out at full size and shrinks to the strip
   only after the alpha hit zero (260 ms timer); reveal grows back to full size
   first, then fades in. `resizeWindow()` also keeps the strip while hidden so
   geometry refreshes (new app/icon size) do not re-expand an invisible panel.
3. **Dark theme** (`cutefish-framework` `bb744d1`). On Plasma the CutefishOS
   appearance daemon never registers, so `Appearance::darkMode()` always
   returned `false` (light panel). It now falls back to
   `~/.config/cutefishos/appearance.conf`:
   ```ini
   [General]
   darkMode=true
   ```
   The panel background then uses the original dark palette (`#666666`).
   Verified with a probe linking the library: `darkMode=true`.

## Priority-2 audit follow-ups (commit `20da50f`)

Open items from STEP0-AUDIT that were not previously reported; now explicitly
checked and closed.

1. **`KWindowEffects::slideWindow` on Wayland — verified, then removed.**
   - The call WAS reachable: `initSlideWindow()` ran at startup and on every
     direction change (constructor + `onPositionChanged()`), translating the
     dock direction into `SlideFromLocation` and calling
     `KWindowEffects::slideWindow(this, location)`.
   - It is NOT silently dropped by KF6 on Wayland: the platform plugin
     (`src/platforms/wayland/windoweffects.cpp` in kf6-kwindowsystem)
     implements it via the `org_kde_kwin_slide` protocol
     (`SlideManager`/`Slide`, `wl_surface`-based, with `set_location` +
     `set_offset` + `commit`). The API-level call is genuine.
   - It can, however, never produce a visible animation here: KWin uses the
     slide hint to animate a surface sliding in/out **at map/unmap time**, and
     the ported dock deliberately never unmaps the panel — auto-hide shrinks a
     permanently mapped layer-shell surface to a ~2px strip and fades the QML
     visuals instead (`setDockHidden`, `m_shrinkTimer`). The slide hint had no
     event to act on, so show/hide is exactly fade (200 ms) + resize (260 ms),
     with no slide component.
   - Resolution: `initSlideWindow()`, its call sites and the
     `#include <KWindowEffects>` were removed — behavior-neutral, but the tree
     no longer implies a slide effect that cannot fire. Should auto-hide ever
     switch to an unmap-based design, re-adding the slide hint would be the
     option for a real slide animation.
2. **`fakewindow.cpp`, `compat/QX11Info`, `PkgConfig::XCB` — build graph clean.**
   - `src/fakewindow.cpp/.h` (X11-only helper window + `KX11Extras::setState`)
     were already absent from `CMakeLists.txt` SRCS (excluded, not merely
     unused at runtime); the dead files are now deleted from the tree.
   - No `compat/` directory exists, no `PkgConfig`/`XCB` in the CMake files.
   - While at it, the only remaining `KF6::WindowSystem` consumers
     (`KWindowEffects::slideWindow` and the call-less
     `XWindowInterface::enableBlurBehind`, whose final blur is done in QML by
     `FishUI.WindowBlur` over `org_kde_kwin_blur`) were removed, so the
     `KF6WindowSystem` `find_package` + `KF6::WindowSystem` link are gone from
     the Wayland build. `ldd` confirms no `libKF6WindowSystem` dependency.

## Configuration files

| File | Purpose | Keys |
|---|---|---|
| `~/.config/cutefishos/dock.conf` | Dock settings (QSettings `cutefishos`/`dock`) | `Direction` 0/1/2, `EdgeMargins`, `IconSize`, `RoundedWindow`, `Style` 0/1, `Visibility` 0=AlwaysShow, 1=AlwaysHide, 2=IntellHide |
| `~/.config/cutefishos/dock_pinned.conf` | Pinned apps on the dock | app ids |
| `~/.config/cutefishos/appearance.conf` | Appearance override used when the CutefishOS daemon is absent (Plasma) | `[General] darkMode=true/false` |

Current state after the fixes: `Visibility=0` (AlwaysShow — the dock stays
visible and reserves the screen edge), dark theme on. The code default is also
`AlwaysShow` (`DockSettings` ctor), so a fresh config behaves the same.

## Build / install / run (private prefix)

```sh
cmake -S dock -B build-dock -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local \
      -DCMAKE_PREFIX_PATH=$HOME/.local/lib64/cmake \
      -DCUTEFISH_FRAMEWORK_SOURCE_DIR=$HOME/Documents/cutefish/cutefish-framework
cmake --build build-dock -j
cp build-dock/cutefish-dock $HOME/.local/bin/
cp dock/cutefish-dock.desktop $HOME/.local/share/applications/
```

Run:

```sh
QT_FORCE_STDERR_LOGGING=1 LD_LIBRARY_PATH=$HOME/.local/lib64 \
  $HOME/.local/bin/cutefish-dock
pkill -x cutefish-dock   # stop
```

D-Bus control:

```
busctl --user call com.cutefish.Dock /Dock com.cutefish.Dock setVisibility i 0|1|2
# 0 AlwaysShow, 1 AlwaysHide, 2 IntellHide
```

## Remaining manual tests (need real mouse input)

- Hover-reveal in IntellHide: dock strips, mouse crosses the bottom edge → dock fades in (two-phase).
- Click a dock icon: activate/minimize the matching window (minimize verified via headless self-test; a second interactive click is recommended).