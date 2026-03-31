# Building ferris-ao-switch

## Prerequisites

### 1. Install devkitPro

Download and run the devkitPro installer from https://devkitpro.org/wiki/Getting_Started.

The installer sets up an MSYS2 environment at `C:\devkitPro\msys2\` and registers the `dkp-libs` package repository.

### 2. Install required portlibs

Open the devkitPro MSYS2 shell (`C:\devkitPro\msys2\msys2.exe`) and run:

```sh
pacman -Syu
pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_ttf \
           switch-sdl2_mixer switch-sdl2_net switch-libwebp switch-mbedtls
```

## Building

### From the devkitPro MSYS2 shell

```sh
cd /path/to/ferris-ao-switch
make
```

### From any shell on Windows (Git Bash, PowerShell, cmd)

```sh
/c/devkitPro/msys2/usr/bin/bash -l -c "cd '/c/path/to/ferris-ao-switch' && make"
```

Output: `ferris-ao-switch.nro` in the project root.

## Running

- **Real hardware (Atmosphere CFW):** Copy `ferris-ao-switch.nro` to `sdmc:/switch/` and launch from hbmenu.
- **Ryujinx emulator:** Open `ferris-ao-switch.nro` directly via File → Open.

## Cleaning

```sh
make clean
```

## Known build notes

### SDL2_image version

The devkitPro repository ships SDL2_image 2.0.4. `IMG_LoadAnimation_RW` (needed for
animated APNG/GIF/WebP) requires 2.6+. The code guards this behind
`#if SDL_IMAGE_VERSION_ATLEAST(2,6,0)`, so it compiles cleanly on 2.0.4 — animations
fall back to static images until a newer portlib is available.

### Additional link libraries

The installed freetype 2.13 requires HarfBuzz and bzip2. SDL2 requires EGL/GLES2.
SDL2_mixer requires modplug and mpg123. SDL2_image requires libjpeg. These are all
bundled in the devkitPro portlibs; the Makefile links them automatically.

### `CFLAGS += $(INCLUDE) -D__SWITCH__`

Standard devkitPro NX Makefiles require this line so that portlib include paths
(resolved via `$(PORTLIBS)`) are passed to the compiler. Without it, SDL2 and libnx
headers are not found.

### NRO asset sections (icon, NACP)

`elf2nro` must receive `--icon`, `--nacp`, and `--romfsdir` flags or the NRO will be
missing its asset section. Ryujinx and hbmenu both require the asset section to be
present. The Makefile exports these via `NROFLAGS`.
