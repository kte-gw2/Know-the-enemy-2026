#!/bin/sh
# Cross-compile from Linux with mingw-w64 (posix threads). Needs Dear ImGui v1.92.7 in ./imgui
set -e
mkdir -p out
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -shared -Wall -Wno-unused-parameter \
  -I. -Iimgui know_thy_enemy.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
  -static -static-libgcc -static-libstdc++ -Wl,--exclude-all-symbols -s \
  -o out/arcdps_know_thy_enemy.dll
