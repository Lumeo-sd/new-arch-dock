#!/usr/bin/env bash
# Enables the dock's Activities overview button.
#
# The button works by sending the shortcut bound to [kwin] Overview, because
# Plasma has no API to open the overview. ydotool injects that key through
# /dev/uinput, below the compositor, so KWin sees it on Wayland.
#
# This is the only step that needs root: it installs one package. Everything
# else in this dock installs user-local.
set -euo pipefail

if command -v ydotool >/dev/null 2>&1; then
    echo "ydotool is already installed: $(command -v ydotool)"
else
    echo "Installing ydotool, the only root step for the overview button."
    sudo pacman -S --needed --noconfirm ydotool
fi

echo
echo "Checking the pieces the button needs:"

if [[ -w /dev/uinput ]]; then
    echo "  ok   /dev/uinput is writable by $(id -un)"
elif [[ -e /dev/uinput ]]; then
    echo "  FAIL /dev/uinput exists but $(id -un) cannot write to it."
    echo "       A udev rule is needed, for example:"
    echo "       echo 'KERNEL==\"uinput\", MODE=\"0660\", GROUP=\"input\", TAG+=\"uaccess\"'"
    echo "         | sudo tee /etc/udev/rules.d/99-uinput.rules && sudo udevadm control --reload"
    exit 1
else
    echo "  FAIL /dev/uinput is missing. Load the module with: sudo modprobe uinput"
    exit 1
fi

shortcut=$(sed -n 's/^Overview=\([^,]*\),.*/\1/p' "$HOME/.config/kglobalshortcutsrc" | head -1)
if [[ -n "$shortcut" ]]; then
    echo "  ok   [kwin] Overview is bound to: $shortcut"
else
    echo "  WARN no Overview shortcut found in kglobalshortcutsrc."
    echo "       Bind one in System Settings > Shortcuts > KWin before using the button."
fi

echo
echo "Restart the dock to pick up the new button:"
echo "  pkill -x cutefish-dock"
