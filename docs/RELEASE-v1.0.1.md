# 🦀 Ferris-AO Switch — v1.0.1 (Audio Hotfix)

**The "I can finally hear it" release.** Blips, shout SFX **and** music now actually
play. Plus a fix for the server list dropping the connection when you tap a server
with mouse/touch.

---

## 🚨 READ THIS FIRST — DELETE YOUR OLD BUILD

> ### ⚠️ You MUST delete the old `ferris-ao-switch.nro` before installing this one.
>
> If you copy this over an old build — or your launcher/emulator runs a **cached**
> copy — you will keep getting the **old, silent** version and think the fix
> didn't work. This is the #1 reason people still hear nothing.
>
> **Do this:**
> 1. **Delete** the old `ferris-ao-switch.nro` from your SD card's `/switch/`
>    folder (and anywhere else you copied it).
> 2. Copy the **new** `ferris-ao-switch.nro` in its place.
> 3. On **Ryujinx**, remove the old entry / reload from the new file (don't relaunch
>    a recent-files cached copy).
>
> The in-game version now reads **v1.0.1** — check it to confirm you're on the new build.

---

## 🔈 RYUJINX USERS — SET YOUR AUDIO BACKEND OFF `Dummy`

> ### ⚠️ If Ryujinx's Audio Backend is `Dummy`, you get ZERO sound — in any game.
>
> Go to **Options → Settings → Audio** (older builds: the **System** tab) and set
> **Audio Backend** to **SDL2 / SDL3 / OpenAL / SoundIO — anything but `Dummy`**,
> then **restart Ryujinx**.
>
> Ryujinx silently falls back to `Dummy` (e.g. when it resets to defaults), and
> `Dummy` produces **no audio at all**. Also make sure Ryujinx's own **volume**
> (bottom status bar) and the **Windows Volume Mixer** for Ryujinx aren't muted.
> A quick check: if no *other* game makes sound either, it's this — not Ferris-AO.

---

## 🔊 What's fixed

- **Blips & SFX now play.** Modern Attorney Online sounds ship as **Opus**, and the
  Switch's bundled audio mixer silently mis-decoded Opus (it treated it as the
  other Ogg codec, Vorbis, and gave up). The client now decodes Opus sound effects
  itself — so every typewriter blip and shout/objection SFX is audible.
- **Music plays reliably.** Background music now picks its decoder from the file's
  real contents, so Opus tracks (the AO norm) play instead of going silent — even
  when a server sends a track name without a clean file extension.
- **Audio opens at the Switch's native 48 kHz.** Opening at 44.1 kHz came out
  **silent** (especially on Ryujinx). Fixed.
- **Server list: tapping a server no longer drops the connection.** A mouse/touch
  click on Ryujinx fires twice, which made the client connect and then immediately
  disconnect itself. Tapping now behaves like the keyboard/controller — one clean
  connect.

---

## 📥 Installing

**Modded Switch (Atmosphère):** **delete the old `ferris-ao-switch.nro` first**,
then copy the new `ferris-ao-switch.nro` to `/switch/` on your SD card, open the
**Homebrew Menu**, and launch **Ferris-AO**.

**Ryujinx:** **remove the old build / don't run a cached copy.** Load the new
`ferris-ao-switch.nro` (drag it on, or **File → Load Application from File…**) and
make sure internet access is enabled. Also make sure your **Audio Backend is not
`Dummy`** (Settings → Audio) and Ryujinx's volume is up — Dummy produces no sound
for *any* game.

No firmware keys, no title install, no base pack.

---

## 🎚️ Still quiet?

1. Confirm the in-game version says **v1.0.1** (if it says v1.0, you're still on the
   old build — delete it).
2. **Ryujinx:** Audio Backend must not be `Dummy`; Ryujinx volume + Windows volume
   mixer up.
3. In-app **Settings → SFX / Music volume** (0–128).

## 🙏 Credits

Created by **SyntaxNyah**, on the shoulders of the Attorney Online community,
devkitPro and SDL2.

*Unofficial homebrew — not affiliated with the official Attorney Online project or
Nintendo.*

**Court is back in session — with the sound ON. 🦜⚖️🔊**
