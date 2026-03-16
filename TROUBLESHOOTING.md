# Flopster Troubleshooting Guide

Having trouble getting Flopster up and running? This guide covers the most common issues across macOS, Windows, and Linux.

---

## Quick Fixes

| Problem | Quick Fix |
|---|---|
| Plugin doesn't show up in my DAW | Rescan plugins in your DAW's preferences, then restart |
| macOS says "unidentified developer" | Right-click the installer → **Open** |
| AU validation fails on macOS | Run `killall -9 AudioComponentRegistrar` in Terminal, then rescan |

---

## macOS

### 1. Installer won't open — "unidentified developer" warning

macOS Gatekeeper flags apps from outside the App Store. This is normal and safe to bypass.

**Option A — Easiest:** Right-click `Flopster-1.24.pkg` and choose **Open**, then confirm.

**Option B — System Settings:** Go to **System Settings → Privacy & Security** and click **"Open Anyway"** near the Flopster notice.

**Option C — Terminal:**
```flopster/TROUBLESHOOTING.md#L0-0
sudo xattr -rd com.apple.quarantine ~/Downloads/Flopster-1.24.pkg
```
Then double-click the installer as usual.

---

### 2. Plugin doesn't appear in DAW after install

Your DAW needs to rescan its plugin folders after a new install. Here's how:

- **Logic Pro:** Preferences → Plug-in Manager → **Reset & Rescan All**
- **Ableton Live:** Options → Preferences → Plug-Ins → **Rescan**
- **Reaper:** Options → Preferences → VST → **Re-scan**
- **GarageBand:** Quit the app completely and reopen it

If it still doesn't appear, reset the AU cache in Terminal:
```flopster/TROUBLESHOOTING.md#L0-0
killall -9 AudioComponentRegistrar && auval -v aumu Flps Shru
```
Then rescan in your DAW.

---

### 3. AU validation fails

The Audio Unit validator (`auval`) checks that Flopster is installed correctly.

1. Open Terminal and run:
   ```flopster/TROUBLESHOOTING.md#L0-0
   auval -v aumu Flps Shru
   ```
2. If it fails, restart Core Audio:
   ```flopster/TROUBLESHOOTING.md#L0-0
   killall -9 coreaudiod
   ```
3. If it still fails, re-run the `Flopster-1.24.pkg` installer and try again.

---

### 4. "Operation not permitted" or permission errors

This usually means macOS is blocking access to the plugin folders.

1. Fix folder permissions:
   ```flopster/TROUBLESHOOTING.md#L0-0
   sudo chmod -R 755 ~/Library/Audio/Plug-Ins/
   ```
2. Remove any lock flags:
   ```flopster/TROUBLESHOOTING.md#L0-0
   sudo chflags -R nouchg ~/Library/Audio/Plug-Ins/
   ```
3. Clear quarantine from the VST3 directly:
   ```flopster/TROUBLESHOOTING.md#L0-0
   sudo xattr -rd com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Flopster.vst3
   ```

---

### 5. Uninstalling on macOS

**Easy way:** Open `/Applications` and double-click **"Flopster Uninstaller"**.

**Manual removal:** Paste this into Terminal to remove all Flopster files:
```flopster/TROUBLESHOOTING.md#L0-0
rm -rf \
  ~/Library/Audio/Plug-Ins/VST3/Flopster.vst3 \
  ~/Library/Audio/Plug-Ins/Components/Flopster.component \
  /Applications/Flopster.app \
  "/Applications/Flopster Uninstaller.command"
```

---

## Windows

### 1. MSI won't install — UAC or SmartScreen blocks it

Windows may flag the installer if it was downloaded from the web.

1. Right-click `Flopster-1.24.msi` → **Run as Administrator**.
2. If Windows SmartScreen appears, click **"More info"** → **"Run anyway"**.

---

### 2. Plugin not found in DAW

Make sure your DAW is scanning the right folder. Flopster installs its VST3 to:

```flopster/TROUBLESHOOTING.md#L0-0
%CommonProgramFiles%\VST3\
```

- **Ableton Live:** Options → Preferences → Plug-Ins → Custom Folder → add the path above
- **FL Studio:** Options → Manage Plugins → **Find more plugins**

After updating the path, trigger a rescan in your DAW.

---

### 3. Uninstalling on Windows

- **Easy way:** Control Panel → Programs → **Flopster** → Uninstall
- **Script:** Run `scripts\installers\win-uninstall.bat` as Administrator

---

## Linux

### 1. Installing the `.deb` package

```flopster/TROUBLESHOOTING.md#L0-0
sudo dpkg -i flopster_1.24_amd64.deb
```

If you see dependency errors, fix them automatically:
```flopster/TROUBLESHOOTING.md#L0-0
sudo apt-get install -f
```

---

### 2. AppImage won't run

1. Make it executable:
   ```flopster/TROUBLESHOOTING.md#L0-0
   chmod +x Flopster-1.24-x86_64.AppImage
   ```
2. Run it:
   ```flopster/TROUBLESHOOTING.md#L0-0
   ./Flopster-1.24-x86_64.AppImage
   ```
3. If you see a **FUSE error**, install the required library:
   ```flopster/TROUBLESHOOTING.md#L0-0
   sudo apt-get install libfuse2
   ```

---

### 3. VST3 not found in DAW

Flopster's VST3 is installed to one of these locations depending on how you installed it:

| Install method | VST3 location |
|---|---|
| `.deb` package | `/usr/lib/vst3/` |
| Manual / AppImage | `~/.vst3/` |

Point your DAW (Bitwig, Reaper, etc.) to the correct path in its plugin settings, then rescan.

---

### 4. Missing system libraries

If Flopster crashes on launch, you may be missing a required library.

**Ubuntu / Debian:**
```flopster/TROUBLESHOOTING.md#L0-0
sudo apt-get install libasound2 libfreetype6 libfontconfig1 libgl1
```

**Fedora / RPM-based:**
```flopster/TROUBLESHOOTING.md#L0-0
sudo dnf install alsa-lib freetype fontconfig mesa-libGL
```

---

### 5. Uninstalling on Linux

**If you used the `.deb` package:**
```flopster/TROUBLESHOOTING.md#L0-0
sudo dpkg -r flopster
```

**Manual removal:**
```flopster/TROUBLESHOOTING.md#L0-0
rm -rf \
  ~/.vst3/Flopster.vst3 \
  ~/.local/bin/flopster \
  ~/.local/share/flopster \
  ~/.local/share/applications/flopster.desktop
```

---

## Still stuck?

If none of the above solved your issue, please open a bug report and include:
- Your OS version and DAW name + version
- Whether you used VST3 or AU
- Any error messages you saw (screenshots welcome!)

**[Open an issue on GitHub →](https://github.com/your-repo/flopster/issues)**

We'll do our best to help. 🎹