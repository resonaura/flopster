# Flopster

**Floppy drive instrument plugin by Shiru & Resonaura**

A VST3 / AU / Standalone plugin that imitates the noises of a floppy disk drive playing music — a beloved gimmick of the MS-DOS era. Sample-based, with the engine controlling playback in a way that produces a realistic mechanical sound.

Original plugin and all samples recorded by **Shiru**.  
Cross-platform JUCE rewrite, macOS AU + Standalone, keyboard interface by **Resonaura**.

---

## Supported Formats

| Platform | Formats |
|----------|---------|
| macOS    | AU, VST3, Standalone |
| Windows  | VST3, Standalone |

---

## Getting Started

### Prerequisites

| Tool | macOS | Windows |
|------|-------|---------|
| Git | ✅ | ✅ |
| CMake | ✅ | ✅ |
| Ninja | ✅ (auto-installed via Homebrew if missing) | optional |
| Xcode CLT | ✅ | — |
| Visual Studio 2022 | — | ✅ (Ninja as fallback) |
| Node.js | optional, for npm scripts | optional, for npm scripts |

JUCE 8.0.7 is fetched automatically — either by the build scripts or via `npm install`.

---

## Building & Installing

### With npm (recommended)

```sh
npm install          # fetches JUCE if not present
npm run build        # release build (auto-detects OS)
npm run install:plugin  # build + install into the system
```

#### All npm scripts

| Script | Description |
|--------|-------------|
| `npm run build` | Release build (auto-detects OS) |
| `npm run build:debug` | Debug build |
| `npm run build:release` | Release build (explicit) |
| `npm run rebuild` | Clean release build |
| `npm run rebuild:debug` | Clean debug build |
| `npm run install:plugin` | Install into the system |
| `npm run reinstall` | Clean rebuild + install |
| `npm run clean` | Remove `build/` and `build-debug/` |
| `npm run dist` | Pack both mac + win distributable zips |
| `npm run dist:mac` | Pack macOS distributable zip only |
| `npm run dist:win` | Pack Windows distributable zip only |
| `npm run mac:build` | macOS release build |
| `npm run mac:build:debug` | macOS debug build |
| `npm run mac:build:release` | macOS release build (explicit) |
| `npm run mac:rebuild` | macOS clean release build |
| `npm run mac:rebuild:debug` | macOS clean debug build |
| `npm run mac:install` | macOS install |
| `npm run mac:reinstall` | macOS clean rebuild + install |
| `npm run win:build` | Windows release build |
| `npm run win:build:debug` | Windows debug build |
| `npm run win:build:release` | Windows release build (explicit) |
| `npm run win:rebuild` | Windows clean release build |
| `npm run win:install` | Windows install |
| `npm run win:reinstall` | Windows clean rebuild + install |

---

### Distribution (sending to someone without build tools)

Build first, then pack:

```sh
npm run build       # compile
npm run dist        # pack both platforms into dist/
npm run dist:mac    # pack macOS only → dist/Flopster-mac-v1.21.zip
npm run dist:win    # pack Windows only → dist/Flopster-win-v1.21.zip
```

Each archive contains the compiled plugin bundles plus a ready-to-run installer — the recipient needs **zero** dev tools installed.

**macOS zip** (`Flopster-mac-v1.21.zip`):
```
Flopster.vst3
Flopster.component
Flopster.app
install.sh          ← double-click or run in Terminal
```
The macOS installer strips Gatekeeper quarantine, ad-hoc codesigns all bundles, runs `spctl --add` so Logic/Ableton don't block the AU, resets the Audio Unit cache, and installs the Standalone into `/Applications`.

**Windows zip** (`Flopster-win-v1.21.zip`):
```
Flopster.vst3\
Flopster.exe
install.bat         ← right-click → Run as Administrator
```
The Windows installer auto-elevates via UAC, copies VST3 to both the system and user-local VST3 folders, installs the Standalone to `%LOCALAPPDATA%\Programs\Flopster\`, and creates a Desktop shortcut.

---

### Without npm

**macOS:**
```sh
bash build.sh               # release build
bash build.sh --debug       # debug build
bash build.sh --rebuild     # clean rebuild

bash install.sh             # build (if needed) + install
bash install.sh --rebuild   # clean rebuild + install
```

**Windows:**
```bat
build.bat                   :: release build
build.bat --debug           :: debug build
build.bat --rebuild         :: clean rebuild

install.bat                 :: build (if needed) + install
install.bat --rebuild       :: clean rebuild + install
```

---

## Install Paths

**macOS:**
```
AU          ~/Library/Audio/Plug-Ins/Components/Flopster.component
VST3        ~/Library/Audio/Plug-Ins/VST3/Flopster.vst3
Standalone  ~/Applications/Flopster.app
```

**Windows:**
```
VST3 (system)  %CommonProgramFiles%\VST3\Flopster.vst3
VST3 (user)    %LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3
Standalone     %USERPROFILE%\Desktop\Flopster.exe
```

After installing, restart your DAW and trigger a plugin rescan.

---

## Sound Reference

All sounds are triggered by MIDI note and velocity:

| Note | Velocity | Sound |
|------|----------|-------|
| C6 | any | Spindle motor |
| D6 | any | Head steps in sequence |
| Below C6 | ~15 | Single head step (fixed, by note) |
| Below C6 | ~40 | Continuous steps loop |
| C3–B5 | ~60 | **Head buzz** — most musical, recommended |
| C3–B5 | ~90 | Head seek, resuming from last position |
| C3–B5 | ~120 | Head seek, always from initial position |
| E6 | any | Push disk into drive |
| F6 | any | Insert disk |
| G6 | any | Eject disk |
| A6 | any | Pull disk out of drive |

---

## Built-in Keyboard

The pixel-art keyboard spans **C3–E6**. Click with the mouse, or in Standalone mode use computer keyboard keys (FL Studio layout).

**Upper row** (one octave up):

| Key | Q | 2 | W | 3 | E | R | 5 | T | 6 | Y | 7 | U | I | 9 | O | 0 | P |
|-----|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Note | C5 | C#5 | D5 | D#5 | E5 | F5 | F#5 | G5 | G#5 | A5 | A#5 | B5 | C6 | C#6 | D6 | D#6 | E6 |

**Lower row:**

| Key | Z | S | X | D | C | V | G | B | H | N | J | M | , | L | . | ; | / |
|-----|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Note | C4 | C#4 | D4 | D#4 | E4 | F4 | F#4 | G4 | G#4 | A4 | A#4 | B4 | C5 | C#5 | D5 | D#5 | E5 |

Keyboard velocity is ~80, which maps to head buzz mode.

---

## Custom Sample Presets

You can add your own preset by placing a new folder inside `samples/` prefixed with a number (e.g. `05 My Drive`).

**Requirements:** uncompressed 44100 Hz, 16-bit, mono WAV files only.

---

## Project Structure

```
flopster/
├── src/               # C++ plugin source
├── assets/            # UI bitmaps (back.bmp, char.bmp) and icons
├── samples/           # Sample presets (bundled into plugin on install)
├── scripts/           # Node.js helper scripts (run.js, postinstall.js)
├── JUCE/              # JUCE 8.0.7 (auto-fetched, not committed)
├── build/             # Release build output (gitignored)
├── build-debug/       # Debug build output (gitignored)
├── CMakeLists.txt
├── build.sh / build.bat
├── install.sh / install.bat
└── package.json
```

---

## History

| Version | Date | Notes |
|---------|------|-------|
| v1.22 | — | Full JUCE 8 cross-platform rewrite (Resonaura): AU + VST3 + Standalone, pixel-art keyboard, FL Studio layout, CMake build system, npm scripts, Gatekeeper fix |
| v1.21 | 16.01.20 | Two more sample sets, volume balanced across sets |
| v1.2 | 13.01.20 | Sample integrity checks, three more sample sets |
| v1.1 | 28.07.19 | GUI improvements, detune, octave shift, pitch bend, second sample set |
| v1.01 | 03.06.19 | x64 support |
| v1.0 | 30.06.17 | Initial release |

---

## License

WTFPL — no warranty, do whatever you want.  
See [wtfpl.net](http://www.wtfpl.net/) for details.

---

## Contact

**Shiru** (original author)  
✉️ shiru@mail.ru  
🌐 [shiru.untergrund.net](http://shiru.untergrund.net)  
❤️ [patreon.com/shiru8bit](https://www.patreon.com/shiru8bit)

**Resonaura** (macOS / cross-platform port)  
🐙 [github.com/resonaura](https://github.com/resonaura)