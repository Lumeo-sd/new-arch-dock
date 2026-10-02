#!/usr/bin/env bash
# Build and install the Cutefish components into a private prefix.
#
# Nothing here needs root: the prefix defaults to ~/.local, and every install
# destination is derived from CMAKE_INSTALL_PREFIX. The one privileged step is
# the list of build dependencies, which the script prints when they are
# missing - run it yourself, it needs your password.
#
#   ./tools/install-user.sh              # build + install into ~/.local
#   PREFIX=/usr ./tools/install-user.sh  # system-wide (needs root)
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
# MainWindow registers <bindir>/../lib64/qt6/qml and <bindir>/../lib/qt6/qml,
# so either layout loads. Keep the library directory and the QML directory at
# the same depth: the FishUI plugin's RUNPATH is $ORIGIN/../../.., which lands
# on <prefix>/<LIB_SUBDIR> only when the QML tree sits next to it.
LIB_SUBDIR="${LIB_SUBDIR:-lib64}"
QML_SUBDIR="${QML_SUBDIR:-$LIB_SUBDIR/qt6/qml}"

PACKAGES=(
    cmake extra-cmake-modules
    qt6-base qt6-declarative qt6-shadertools qt6-tools qt6-5compat
    layer-shell-qt kwayland kwindowsystem
)

missing=()
for p in "${PACKAGES[@]}"; do
    pacman -Qq "$p" &>/dev/null || missing+=("$p")
done

# The QML modules the dock and FishUI import live in qt6-declarative /
# qt6-5compat on Arch, but their package names differ across distributions -
# so probe for the import directories themselves. They do not sit in the same
# place either: Arch uses /usr/lib/qt6/qml, Debian and Ubuntu add the
# multiarch triplet.
QML_ROOTS=("/usr/lib/qt6/qml" "/usr/lib64/qt6/qml" "/usr/local/lib/qt6/qml")
if command -v gcc >/dev/null 2>&1; then
    triplet="$(gcc -print-multiarch 2>/dev/null || true)"
    [ -n "$triplet" ] && QML_ROOTS+=("/usr/lib/$triplet/qt6/qml")
fi

QML_IMPORTS=(
    QtQuick QtQuick/Controls QtQuick/Layouts QtQuick/Shapes QtQuick/Window
    Qt5Compat/GraphicalEffects
)
missing_qml=()
for m in "${QML_IMPORTS[@]}"; do
    found=0
    for root in "${QML_ROOTS[@]}"; do
        [ -d "$root/$m" ] && { found=1; break; }
    done
    [ "$found" -eq 1 ] || missing_qml+=("$m")
done

if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing build dependencies:"
    printf '  %s\n' "${missing[@]}"
    echo
    echo "Install them with (needs your password):"
    echo "  sudo pacman -S --needed ${missing[*]}"
    exit 1
fi

if [ ${#missing_qml[@]} -gt 0 ]; then
    echo "Missing QML import directories (the packages that provide them differ"
    echo "per distribution; on Arch they are qt6-declarative and qt6-5compat):"
    printf '  /usr/lib/qt6/qml/%s\n' "${missing_qml[@]}"
    exit 1
fi

echo "==> prefix      $PREFIX"
echo "==> build type  $BUILD_TYPE"
echo "==> lib dir     $LIB_SUBDIR"
echo "==> qml modules $QML_SUBDIR"

# 1. fishui - the QML kit the dock imports. It also builds the appearance
#    library, which has to land next to the QML module so libFishUI.so can be
#    loaded without LD_LIBRARY_PATH.
echo "==> configuring fishui"
cmake -S "$REPO_ROOT/fishui" -B "$BUILD_DIR/fishui" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_INSTALL_LIBDIR="$LIB_SUBDIR" \
      -DINSTALL_QMLDIR="$PREFIX/$QML_SUBDIR"

echo "==> building fishui"
cmake --build "$BUILD_DIR/fishui" -j"$(nproc)"

echo "==> installing fishui"
cmake --install "$BUILD_DIR/fishui"

# 2. dock
echo "==> configuring dock"
cmake -S "$REPO_ROOT/dock" -B "$BUILD_DIR/dock" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX"

echo "==> building dock"
cmake --build "$BUILD_DIR/dock" -j"$(nproc)"

echo "==> installing dock"
cmake --install "$BUILD_DIR/dock"

# 3. autostart. The Exec path has to match the installed binary exactly: KWin
#    resolves the client through the canonical Exec when deciding whether to
#    hand out org_kde_plasma_window_management.
mkdir -p "$HOME/.config/autostart"
sed "s|^Exec=.*|Exec=$PREFIX/bin/cutefish-dock|" \
    "$BUILD_DIR/dock/cutefish-dock.desktop" \
    > "$HOME/.config/autostart/cutefish-dock.desktop"

echo
echo "Installed:"
echo "  binary   $PREFIX/bin/cutefish-dock"
echo "  desktop  $PREFIX/share/applications/cutefish-dock.desktop"
echo "  qml      $PREFIX/$QML_SUBDIR/FishUI"
echo "  autostart $HOME/.config/autostart/cutefish-dock.desktop"
echo
echo "Run \"$PREFIX/bin/cutefish-dock\" in a Plasma Wayland session to try it."
echo "If the dock stays empty, the compositor denied the window-management"
echo "interface: check that the running desktop file is the installed one and"
echo "that \$XDG_DATA_DIRS/$PREFIX/share is visible to KWin."