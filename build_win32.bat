@REM Build with the Visual Studio x64 compiler. Run vcvars64.bat first.
@REM Needs Dear ImGui v1.92.7 (the version arcdps uses since May 2026) extracted to .\imgui
@set OUT_DIR=out
@set OUT_DLL=arcdps_know_thy_enemy.dll
@set SOURCES=know_thy_enemy.cpp .\imgui\imgui.cpp .\imgui\imgui_draw.cpp .\imgui\imgui_tables.cpp .\imgui\imgui_widgets.cpp
mkdir %OUT_DIR%
cl /LD /EHsc /O2 /std:c++17 /MT /I. /I.\imgui %SOURCES% /Fe%OUT_DIR%/%OUT_DLL% /Fo%OUT_DIR%/ /link user32.lib
