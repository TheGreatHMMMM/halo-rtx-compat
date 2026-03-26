<h1 align="center">Halo CE RTX Remix Compatibility Mod</h1>
<br>
<div align="center" markdown="1"> 
A RTX Remix compatibility mod for Halo: Combat Evolved, built on top of the  
[RTX Remix Compatibility Codebase](https://github.com/xoxor4d/remix-comp-base) by [xoxor4d](https://patreon.com/xoxor4d).  
Feel free to join the discord server: https://discord.gg/FMnfhpfZy9
<br>
</div>

# Overview
This repository contains a compatibility mod that enables NVIDIA RTX Remix support for Halo: Combat Evolved.  
It is built on top of xoxor4d's RTX Remix Compatibility Codebase.

#### It features:
- A hooked D3D9 interface, with every function detoured for easy access and interception
- Logic to aid with drawcall modifications
- A basic ImGui menu for debugging purposes

<br>

## Documentation / Guides
Please see: https://github.com/xoxor4d/remix-comp-base/tree/master/documentation
<br>

## Installation
1. Rename the RTX Remix `d3d9.dll` to `d3d9_remix.dll` in your Halo CE game directory.
2. Compile the mod (see below) — it will generate a `d3d9.dll` for you to use as the proxy.
3. Place the generated `d3d9.dll` into your Halo CE game directory alongside `d3d9_remix.dll`.

<br>

## Compiling
- Clone the repository `git clone --recurse-submodules https://github.com/YOUR_USERNAME/halo-rtx-compat.git`
- Install [VS 2022 Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)
  (the full VS 2022 IDE works too). Select the **Desktop development with C++** workload.
- Optional: set environment variable `REMIX_COMP_ROOT` to your game folder and `REMIX_COMP_ROOT_EXE` to the exe name.
- Run `build.bat` — it compiles everything directly with `cl.exe` and produces `build\bin\Release\d3d9.dll`.
  - Pass `debug` as an argument for a debug build: `build.bat debug`
- Copy everything inside the `assets` folder into the game directory.

<br>

## Credits
- [xoxor4d](https://patreon.com/xoxor4d) - Creator of the [RTX Remix Compatibility Codebase](https://github.com/xoxor4d/remix-comp-base) this mod is built upon — consider supporting them on [Patreon](https://patreon.com/xoxor4d)!
- [LivingFray - HaloCEVR](https://github.com/LivingFray/HaloCEVR) - Halo CE mod reference and inspiration
- [NVIDIA - RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix)
- [People of the showcase discord](https://discord.gg/j6sh7JD3v9) - especially the nvidia engineers ✌️
- [Dear ImGui](https://github.com/ocornut/imgui)
- [minhook](https://github.com/TsudaKageyu/minhook)
- All 🍓 Testers

<div align="center" markdown="1"> 
And of course, all the people that helped along the way!
</div>
