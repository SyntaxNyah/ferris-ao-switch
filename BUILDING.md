# Building ferris-ao-switch

A Nintendo Switch homebrew **Attorney Online 2** client. The build produces a
single `ferris-ao-switch.nro` that runs on modded hardware (Atmosphère) **and**
the Ryujinx emulator — the same file, no separate builds.

---

## TL;DR (already have devkitPro?)

```sh
make            # produces ferris-ao-switch.nro in the project root
```

That's the whole build. Everything below is the one-time setup and the details.

---

## Step 1 — Install devkitPro (one time)

devkitPro is the ARM64 cross-compiler toolchain. You do **not** need Visual
Studio, CMake, or anything else.

| OS | What to do |
|----|-----------|
| **Windows** | Download the graphical installer from <https://devkitpro.org/wiki/Getting_Started> and run it. It installs an MSYS2 shell at `C:\devkitPro\msys2\`. **Tick the "Switch Development" box** during install. |
| **Linux** | Follow <https://devkitpro.org/wiki/devkitPro_pacman> to add the `dkp-pacman` repo, then `sudo dkp-pacman -S switch-dev`. |
| **macOS** | Install the `.pkg` from the Getting Started page, then use `dkp-pacman`. |

After installing, **open a new terminal** so the `DEVKITPRO` environment
variable is set. On Windows this means launching **"MSYS2 (devkitPro)"** from
the Start menu (NOT a normal cmd/PowerShell window — see Step 4 for those).

Verify it worked:

```sh
echo $DEVKITPRO        # should print /opt/devkitpro  (or C:/devkitPro)
```

If it prints nothing, the toolchain isn't on your PATH yet — re-open the shell.

---

## Step 2 — Install the libraries this project needs (one time)

In the **devkitPro / MSYS2 shell**, run:

```sh
pacman -Syu                # update the package database (may ask to restart the shell)
pacman -S switch-dev \
          switch-sdl2 switch-sdl2_image switch-sdl2_ttf \
          switch-sdl2_mixer switch-sdl2_net \
          switch-libwebp switch-mbedtls
```

What each one is for:

| Package | Used for |
|---------|----------|
| `switch-dev` | the compiler + libnx (core Switch APIs) |
| `switch-sdl2` | window, input, threads, rendering |
| `switch-sdl2_image` | PNG / WebP / APNG / GIF sprite decoding |
| `switch-sdl2_ttf` | on-screen text |
| `switch-sdl2_mixer` | music + sound effects |
| `switch-sdl2_net` | TCP networking (desktop-fallback path) |
| `switch-libwebp` | animated WebP sprites |
| `switch-mbedtls` | TLS for `wss://` servers + HTTPS asset CDNs |

Accept the defaults when `pacman` asks. This downloads a few hundred MB once.

---

## Step 3 — Build

From the project folder, in the devkitPro shell:

```sh
cd /path/to/ferris-ao-switch
make
```

When it finishes you'll have **`ferris-ao-switch.nro`** in the project root.
That's the file you run. (`make` also leaves a `ferris-ao-switch.elf` and a
`build/` folder full of object files — you can ignore both.)

To rebuild from scratch:

```sh
make clean && make
```

---

## Step 4 — Building from a normal Windows shell (optional)

If you'd rather stay in PowerShell / cmd / Git Bash instead of the MSYS2 shell,
call the devkitPro bash explicitly. It sets up `DEVKITPRO` for you via `-l`
(login shell):

```powershell
# PowerShell — note the quoting around the path
& C:\devkitPro\msys2\usr\bin\bash.exe -l -c "cd '/c/Users/you/Documents/GitHub/ferris-ao-switch' && make"
```

Windows drive paths become `/c/...`, `/d/...` etc. inside that bash.

---

## Step 5 — Run it

| Where | How |
|-------|-----|
| **Ryujinx (easiest for testing)** | Just open `ferris-ao-switch.nro` from Ryujinx: *File → Load Application from File*. No SD card needed. |
| **Real Switch (Atmosphère CFW)** | Copy `ferris-ao-switch.nro` to your SD card under `sdmc:/switch/`, then launch it from the **hbmenu** (Homebrew Menu). |

On launch you get the **Connect** screen. Pick a server from the list (it
fetches the public master server) or use **Direct Connect** to type a
`host`, `port`, and username. `ws://` / `wss://` prefixes select WebSocket /
secure-WebSocket; anything else is raw TCP.

### Do I need to download character assets?

**No.** The client **streams** assets over HTTP:

1. If the server advertises an asset CDN (an `ASS` packet during the
   handshake), everything — characters, backgrounds, music — downloads on
   demand. Nothing to install.
2. A built-in community fallback CDN (`attorneyoffline.de/base/`) covers the
   classic base pack for servers that only host their own custom characters.
3. *(Optional)* If you want local assets, drop an AO2 base pack at
   `sdmc:/switch/ferris-ao/base/` on the SD card. These are used when a CDN
   doesn't have a file.
4. `romfs/` (baked into the `.nro`) is the last-resort fallback for UI chrome.

So the normal experience is: **build, run, connect, play** — no asset download.

---

## Don't want to build it yourself? (GitHub Actions)

Every push builds the `.nro` for you in CI (`.github/workflows/build.yml`):

1. Open the repo's **Actions** tab on GitHub.
2. Click the most recent **Build NRO** run (green check = success).
3. Scroll to **Artifacts** and download **`ferris-ao-switch`** — it contains
   `ferris-ao-switch.nro`, ready to drop on your SD card or open in Ryujinx.

The workflow runs in the official `devkitpro/devkita64` container, installs the
same SDL2 portlibs listed in Step 2 via `dkp-pacman`, runs `make`, and uploads
the result. It also triggers manually (**Run workflow** button) and, when you
push a Git tag like `v0.1.0`, publishes a **GitHub Release** with the `.nro`
attached.

---

## Troubleshooting

**`"DEVKITPRO is not set"`** — You're not in the devkitPro shell. Open
"MSYS2 (devkitPro)" (Step 1) or use the explicit-bash command (Step 4).

**`cannot find -lSDL2_image` (or similar `-l...`)** — A portlib from Step 2
isn't installed. Re-run the `pacman -S ...` line.

**`fatal error: SDL2/SDL.h: No such file`** — Same cause: SDL2 portlibs
missing, or you're building outside the devkitPro shell so the portlib include
paths aren't set.

**`make` says "Nothing to be done"** — It already built. Run `make clean`
first to force a rebuild.

**Can't connect to any server (list loads, but joining hangs)** — AO2 over
WebSocket is a *text* protocol. `ws_encode_frame` must emit a TEXT frame
(opcode `0x01` / first byte `0x81`); servers drop binary (`0x82`) frames, so
`HI` never arrives and the handshake stalls. This is fixed in the current code
— if you ever see it regress, that opcode is the first thing to check.

**Sprites/backgrounds don't appear in-game** — That's an *asset streaming*
matter, not a build problem: the server may not advertise a CDN and the
fallback CDN may not have that character. Try a server that sends an `ASS`
packet, or install a local base pack (Step 5).

**Animated WebP/APNG shows as a still frame** — Your devkitPro `switch-sdl2_image`
predates 2.6 (no `IMG_LoadAnimation_RW`). Update it: `pacman -S switch-sdl2_image`.
The code compiles either way; animations just won't move on the old version.

---

## Verified build notes (read this if `make` "can't find DEVKITPRO")

This build has been compiled end-to-end with devkitPro on Windows — it produces
a ~12 MB `ferris-ao-switch.nro` with a valid `HOMEBREW` / `NRO0` header. A few
things that trip people up, learned the hard way:

### `make` says DEVKITPRO is not set even though `echo $DEVKITPRO` prints it

Some shells (notably **Git Bash**, and any shell where the devkit profile sets
the variable without `export`) show `DEVKITPRO` to *you* but don't pass it to
the `make` child process, so the Makefile's guard trips. Two reliable fixes:

```sh
# A) Build through the devkitPro MSYS2 *login* shell — it sources the devkit
#    environment itself. This is the command that works on this machine:
/c/devkitPro/msys2/usr/bin/bash -l -c \
  "cd '/c/Users/<you>/Documents/GitHub/ferris-ao-switch' && make -j4"

# B) Or just run `make` from the "MSYS2 (devkitPro)" Start-menu shell, where
#    DEVKITPRO is already exported.
```

Setting `export DEVKITPRO=...` in a *non-devkit* shell and then calling the
MSYS2 `make` is unreliable — the two MSYS roots translate paths differently.
Use a devkit login shell instead.

### Syntax-check one file without a full build (fast feedback)

You don't need a full `make` to catch C++ errors. Point the cross-compiler at
the portlib headers and use `-fsyntax-only`:

```sh
/opt/devkitpro/devkitA64/bin/aarch64-none-elf-g++ -fsyntax-only \
  -std=gnu++17 -fno-exceptions -fno-rtti -D__SWITCH__ -DAO_TLS -Wall -Wextra \
  -I/opt/devkitpro/portlibs/switch/include \
  -I/opt/devkitpro/portlibs/switch/include/SDL2 \
  -I/opt/devkitpro/libnx/include -Isrc \
  src/ui/screens/courtroom_screen.cpp
```

(`/opt/devkitpro` is the POSIX path inside the devkit/MSYS2 shells; it maps to
`C:\devkitPro` on disk.)

### Expected warnings (harmless — not build errors)

- **`-Wstringop-truncation` / `-Wformat-truncation`** on `strncpy(dst, src,
  sizeof(dst)-1)` and `snprintf` calls. The codebase deliberately uses
  fixed-size buffers and bounded copies; these are GCC false-positives that
  appear throughout (e.g. `connect_screen.cpp`, `courtroom_screen.cpp`). They do
  not affect correctness.
- **`-Wmissing-field-initializers`** coming from libnx's own `switch/sf/hipc.h`
  and `cmif.h` — not our code at all.

A clean build ends with `linking ferris-ao-switch.elf` followed by
`built ... ferris-ao-switch.nro`. The `.nro`, `.elf`, and `build/` are
`.gitignore`d — CI and local builds regenerate them.

---

## How the build is wired (for the curious)

- The `Makefile` includes devkitPro's standard `switch_rules`, which compiles
  every `.cpp` under the `SOURCES` directories and links the `LIBS` list.
- `-DAO_TLS` is defined so `wss://` and HTTPS CDNs work (via `switch-mbedtls`).
  Without it, `TlsConn`/`https_get` compile to stubs and only `ws://`/`http://`
  and raw TCP work.
- `-fno-exceptions -fno-rtti` — standard Switch practice; there is no
  `try`/`catch` or `dynamic_cast` anywhere in the codebase.
- `ROMFS := romfs` bakes the `romfs/` folder into the NRO (reachable at
  `romfs:/` at runtime) for bundled fallback assets.

See **CLAUDE.md** for the full architecture reference.
