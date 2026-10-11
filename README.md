> **Community-maintained fork** of [typedeck0/Know-thy-enemy](https://github.com/typedeck0/Know-thy-enemy), updated to work with current arcdps (ImGui 1.92.7) and the Visions of Eternity elite specializations. All credit for the original addon goes to typedeck0.

 [Know thy enemy discord](https://discord.gg/6xDbA64rZc) for update notifications and chat.

> Know thy enemy and know thyself, and need not fear the result of a hundred battles.
> 
> \- Sun Tzu

Some parts of the code are closed source, since it is related to arcdps/GW2 internals.

# Know-thy-enemy [WvW]
[![](https://img.shields.io/github/downloads/typedeck0/know-thy-enemy/total?style=plastic)](../../releases)

Tired of commanders saying "They're *twice* our size!"? Well now you can put a number on it!

Counts the amount and type of player enemies (that your squad hits or is hit by) in an arcdps fight instance (resets when arcdps does).

![image](https://user-images.githubusercontent.com/113395677/222940654-ff55d512-85e5-42dc-a289-9075641ce6be.png)
&nbsp;&nbsp;&nbsp;&nbsp;
![image](https://user-images.githubusercontent.com/113395677/222940678-08786dca-7a06-4b8d-8e75-18ba340e4422.png)
&nbsp;&nbsp;&nbsp;&nbsp;
![image](https://user-images.githubusercontent.com/113395677/226063666-4c092d1b-0017-421c-9d99-901a53ae5b00.png)

Columns and small names options.

![image](https://user-images.githubusercontent.com/113395677/229323981-305f5725-00c3-439d-a431-a8ee919c032b.png)

## Build
This version only uses the public arcdps API (no closed-source parts), built against Dear ImGui 1.92.7, which arcdps has used since May 2026.

Download [ImGui v1.92.7](https://github.com/ocornut/imgui/tree/v1.92.7) into `imgui/`, then either run `build_win32.bat` from an x64 MSVC prompt or `./build_mingw.sh` on Linux with mingw-w64. The DLL lands in `out/`.

## Install:
make sure you have [arcdps](https://www.deltaconnected.com/arcdps/)

move arcdps_know_thy_enemy.dll([releases](../../releases)) to the folder where the gw2 exe is or the bin64 folder (its in the same folder as your gw2 exe)

then in-game open the arcdps options window (alt-shift-t)

Under the "Extensions" tab, you will see a sub page called Know thy enemy, where you can enable and disable the extension.


## Use:
Right click the window to toggle between a history of fights and the current instance

## Comments, concerns, and/or proclamations

post an [issue](../../issues)

hmu in this new discord https://discord.gg/6xDbA64rZc

or in-game at woodel.6318
