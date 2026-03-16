#!/usr/bin/env node
// scripts/run.js — cross-platform script dispatcher
//
// Routes npm run commands to the correct script in tools/ based on the
// current OS and the requested action.
//
// Usage (internal — called by package.json scripts):
//   node scripts/run.js <action> [args...]
//
// Actions:
//   build    → tools/mac-build.sh  / tools/win-build.bat  / tools/linux-build.sh
//   install  → tools/mac-install.sh / tools/win-install.bat / tools/linux-install.sh
//   pkg      → tools/mac-pkg.sh       (macOS only — produces .pkg)
//   msi      → tools/win-msi.bat      (Windows only — produces .msi)
//   deb      → tools/linux-deb.sh     (Linux only — produces .deb)
//   appimage → tools/linux-appimage.sh (Linux only — produces .AppImage)

"use strict";

const { spawnSync } = require("child_process");
const path = require("path");
const os = require("os");

const ROOT = path.resolve(__dirname, "..");
const TOOLS = path.join(ROOT, "tools");

const platform = os.platform(); // "darwin" | "win32" | "linux"

// ── Action → script mapping ───────────────────────────────────────────────────

const SCRIPTS = {
  build: {
    darwin: { cmd: "bash", script: "mac-build.sh" },
    win32: { cmd: "cmd", script: "win-build.bat", bat: true },
    linux: { cmd: "bash", script: "linux-build.sh" },
  },
  install: {
    darwin: { cmd: "bash", script: "mac-install.sh" },
    win32: { cmd: "cmd", script: "win-install.bat", bat: true },
    linux: { cmd: "bash", script: "linux-install.sh" },
  },
  pkg: {
    darwin: { cmd: "bash", script: "mac-pkg.sh" },
  },
  msi: {
    win32: { cmd: "cmd", script: "win-msi.bat", bat: true },
  },
  deb: {
    linux: { cmd: "bash", script: "linux-deb.sh" },
  },
  appimage: {
    linux: { cmd: "bash", script: "linux-appimage.sh" },
  },
};

// ── Parse args ────────────────────────────────────────────────────────────────

const [, , action, ...passthrough] = process.argv;

if (!action || action === "--help" || action === "-h") {
  console.log(`
  Usage: node scripts/run.js <action> [args...]

  Actions:
    build      Build the plugin for the current platform
    install    Install built artefacts into the system plugin directories
    pkg        Build macOS .pkg installer         (macOS only)
    msi        Build Windows .msi installer       (Windows only)
    deb        Build Linux .deb package           (Linux only)
    appimage   Build Linux .AppImage              (Linux only)

  All extra arguments are forwarded to the underlying script.
  e.g.  node scripts/run.js build --rebuild --debug
`);
  process.exit(0);
}

const map = SCRIPTS[action];
if (!map) {
  console.error(`  ❌  Unknown action: "${action}"`);
  console.error(`      Available: ${Object.keys(SCRIPTS).join(", ")}`);
  process.exit(1);
}

const entry = map[platform];
if (!entry) {
  const supported = Object.keys(map)
    .map((p) => ({ darwin: "macOS", win32: "Windows", linux: "Linux" })[p] ?? p)
    .join(", ");
  console.error(`  ❌  Action "${action}" is not supported on this platform.`);
  console.error(`      Supported on: ${supported}`);
  process.exit(1);
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

const scriptPath = path.join(TOOLS, entry.script);

const spawnArgs = entry.bat
  ? ["/c", scriptPath, ...passthrough]
  : [scriptPath, ...passthrough];

const result = spawnSync(entry.cmd, spawnArgs, {
  stdio: "inherit",
  cwd: ROOT,
  shell: false,
});

if (result.error) {
  console.error(`  ❌  Failed to spawn: ${result.error.message}`);
  process.exit(1);
}

process.exit(result.status ?? 1);
