#!/usr/bin/env bash
# Runs the shortcut translation checks for the Activities overview button.
#
# Separate from the dock build because it needs no window: the dock hides itself
# whenever a window is maximised, so the button cannot always be clicked by hand,
# and the translation from "Meta+W" to ydotool keycodes is the part that breaks
# quietly.
set -euo pipefail

cd "$(dirname "$0")/.."

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

# Reuse the include paths the dock build already resolved.
repo=$(pwd)
cat > "$out/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.16)
project(overviewparser CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core DBus)
add_executable(overviewparser
    "$repo/dock/tests/overviewparser.cpp"
    "$repo/dock/src/overviewmanager.cpp"
    "$repo/dock/src/overviewmanager.h")
target_include_directories(overviewparser PRIVATE "$repo/dock/src")
target_link_libraries(overviewparser PRIVATE Qt6::Core Qt6::DBus)
CMAKE

cmake -S "$out" -B "$out/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$out/build" >/dev/null

"$out/build/overviewparser"
