# Means Arms Ammo — CardputerZero Edition

Native 320×170 C++20 / SDL2 / framebuffer (`/dev/fb_lcd`) port of **Means Arms Ammo** for the **M5Stack CardputerZero** (Raspberry Pi CM0, Debian Trixie arm64).

## Features
- **Caliber Tabs:** Browse one caliber at a time (`Z`/`C` or `Tab` to switch calibers).
- **Sort & Search:** Default sort by Bullet Weight (`S:Wt`), toggle with Name (`S:Name`) via `S`, live search via `/`.
- **4-Tab Load Detail:**
  1. **Specs:** Load name, caliber, cartridge/shotshell, imperial/metric, powder charge, COAL, trim, component descriptions.
  2. **Costs & Savings:** Per-round breakdown, stacked cost bar, loads/lb (or /kg), savings table (`20`–`1000` rds), retail comparison.
  3. **Ballistics:** Muzzle velocity, SD, ES, BC (`G1`/`G7`), muzzle energy (`ft-lbs`), and `100`–`500 yd` drop table (inches, MOA, MIL, velocity).
  4. **Notes:** View and edit load notes inline.
- **Android Backup Compatibility:** Imports and exports the same JSON backup schema (`ReloadLoad`) as the Android Means Arms Ammo app (`~/.local/share/means-arms-ammo/loads.json`).

## Controls (No `Fn` Needed for Navigation)
- **Load List:** `F`/`X` Up/Down · `Z`/`C` Switch Caliber · `Enter` Open · `N` New · `D` Duplicate · `/` Find · `S` Sort (`Wt`/`Name`) · `Backspace` Delete · `I` Import JSON · `E` Export JSON · `Q` or hold `` ` `` (1s) Quit
- **Load Detail:** `Z`/`C` or `1`–`4` Switch Tabs · `F`/`X` Select Field · `Enter` Edit/Toggle · `` ` `` Back

## Build & Install on CardputerZero
```bash
./install_cardputer.sh
```
*(Builds from source if `means-arms-zero` isn't already present, installs to `/usr/local/bin/means-arms-zero`, and registers the `APPLaunch` entry and icon.)*

## Run Tests
```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```
