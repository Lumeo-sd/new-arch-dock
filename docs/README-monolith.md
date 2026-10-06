# kde-cutefish-dock (monolith fork)

> **The entire CutefishOS desktop shell, ported to Qt6 / KDE Frameworks 6 and
> running natively under **Plasma 6 Wayland**.  Single branch (`main`),
> one subfolder per component — no X11 (no KX11Extras, no XCB, no XWayland).
> Many components now talk to the Wayland compositor (LayerShell, KWayland)
> the way KWin's own plasma-shell components do.**

One repo, five components, five git tags:

| Component (subfolder) | Tag | What it is |
|---|---|---|
| `cutefish-framework` | `cutefish-framework-qt6-wayland-v1` | D-Bus + appearance (darkMode) service |
| `dock` | `dock-qt6-wayland-v1` | Always-on-top dock (LayerShell, KWayland window track) |
| `fishui` | `fishui-qt6-wayland-v1` | Qt Quick UI kit (FishUI 1.0 QML plugin) |
| `launcher` | `launcher-qt6-wayland-v1` | Launcher (application grid) |
| `statusbar` | `statusbar-qt6-wayland-v1` | Top status bar (wifi / bluetooth / layout / tray) |

Why tags, not branches: a monorepo keeps one `main` as the single source of
truth; a tag is a immutable snapshot of exactly one component's release,
without forking five divergent branches you'd have to keep in sync.  To grab
a single component from any commit:

```bash
git clone --depth 1 --branch <cutefish-framework|dock|fishui|launcher|statusbar>-qt6-wayland-v1 \
    git@github.com:Lumeo-sd/kde-cutefish-dock.git <component>
```

---

## Install each component separately — Fedora 42+ (tested)

All components install **privately** under `~/.local` (no root, no system
packages beyond the build deps installed once).

### 0. Build dependencies (install once)

```bash
sudo dnf install -y \
    gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
    qt6-qt5compat-devel qt6-qttools-devel \
    extra-cmake-modules \
    kf6-kwindowsystem-devel \
    kwayland-devel \
    layer-shell-qt-devel
```

### 1. cutefish-framework

```bash
cmake -S cutefish-framework -B build-framework \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-framework -j$(nproc)
cmake --install build-framework
```

### 2. fishui (Qt Quick UI kit)

```bash
cmake -S fishui -B build-fishui \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-fishui -j$(nproc)
cmake --install build-fishui

# fishui's install step skips the appearance .so — copy it manually:
cp build-fishui/cutefish-framework-appearance-build/libcutefish-framework-appearance.so \
   "$HOME/.local/lib64/"
```

### 3. dock

```bash
cmake -S dock -B build-dock \
      -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-dock -j$(nproc)
cmake --install build-dock

# Wayland grant: KWin only sends window-management to apps that declare it.
cp dock/cutefish-dock.desktop "$HOME/.local/share/applications/"
# Optional: pin to the dock from a file manager
cp dock/cutefish-dock.desktop "$HOME/.local/share/applications/"  # (drag-drop target)

# Autostart on login (replace $USER with your shell user)
cat > "$HOME/.config/autostart/cutefish-dock.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Cutefish Dock
Exec=$HOME/.local/bin/cutefish-dock
Terminal=false
X-KDE-autostart-after=plasma-desktop.service
