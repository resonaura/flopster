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
//
// Windows arch behaviour:
//   By default on Windows, build/install/msi run for ALL supported architectures:
//     ARM host → arm64, x64, x86
//     x64 host → x64, x86
//   Pass --arch <arch> to target a single architecture instead.
//
// Examples:
//   node scripts/run.js build                 ← all archs (default on Windows)
//   node scripts/run.js build --arch arm64    ← single arch
//   node scripts/run.js build --arch x64
//   node scripts/run.js msi   --arch x64

"use strict";

const { spawnSync } = require("child_process");
const path = require("path");
const os = require("os");

const ROOT = path.resolve(__dirname, "..");
const TOOLS = path.join(ROOT, "tools");

const platform = os.platform(); // "darwin" | "win32" | "linux"

// ── Windows multi-arch helpers ────────────────────────────────────────────────

function detectWinHostArch() {
  const pa = (process.env["PROCESSOR_ARCHITECTURE"] || "").toLowerCase();
  const pa2 = (process.env["PROCESSOR_ARCHITEW6432"] || "").toLowerCase();
  if (pa === "arm64" || pa2 === "arm64") return "arm64";
  return "x64";
}

function defaultWinArchList() {
  return detectWinHostArch() === "arm64"
    ? ["arm64", "x64", "x86"]
    : ["x64", "x86"];
}

// Returns the list of archs to build based on passthrough args.
// If --arch <a> is present → [a]. Otherwise → all supported for this host.
function resolveWinArchList(args) {
  const idx = args.indexOf("--arch");
  if (idx !== -1 && args[idx + 1]) {
    let a = args[idx + 1]
      .toLowerCase()
      .replace("amd64", "x64")
      .replace("x86_64", "x64")
      .replace("win32", "x86")
      .replace("i686", "x86")
      .replace("i386", "x86");
    if (!["arm64", "x64", "x86"].includes(a)) {
      console.error(`  ❌  Unknown --arch value: ${args[idx + 1]}`);
      console.error(`      Valid values: arm64, x64, x86`);
      process.exit(1);
    }
    return [a];
  }
  return defaultWinArchList();
}

// Strip --arch <value> from args array (we inject it ourselves per iteration)
function stripArchFlag(args) {
  const out = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--arch") {
      i++;
      continue;
    } // skip --arch + its value
    out.push(args[i]);
  }
  return out;
}

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
  const hostArch = detectWinHostArch();
  const allArchs = defaultWinArchList();
  console.log(`
  Usage: node scripts/run.js <action> [args...]

  Actions:
    build      Build the plugin for the current platform
    install    Install built artefacts into the system plugin directories
    pkg        Build macOS .pkg installer         (macOS only)
    msi        Build Windows .msi installer       (Windows only)
    deb        Build Linux .deb package           (Linux only)
    appimage   Build Linux .AppImage              (Linux only)

  Windows arch options (for build / install / msi):
    --arch <arch>    Target a single architecture: arm64 | x64 | x86
                     Default: ALL supported archs for this host
                     This host (${hostArch}): ${allArchs.join(", ")}

  All extra arguments are forwarded to the underlying script.
  e.g.  node scripts/run.js build              ← builds ${allArchs.join(" + ")} on this host
  e.g.  node scripts/run.js build --rebuild
  e.g.  node scripts/run.js build --arch arm64
  e.g.  node scripts/run.js msi   --arch x64
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

// On Windows, for build/install/msi: run once per architecture
const WIN_MULTI_ARCH_ACTIONS = ["build", "install", "msi"];

if (
  platform === "win32" &&
  entry.bat &&
  WIN_MULTI_ARCH_ACTIONS.includes(action)
) {
  const archList = resolveWinArchList(passthrough);
  const baseArgs = stripArchFlag(passthrough);

  console.log(`\n  → Windows target arch(es): ${archList.join(", ")}\n`);

  for (const arch of archList) {
    console.log(`\n  ══ arch: ${arch} ══\n`);
    const spawnArgs = ["/c", scriptPath, ...baseArgs, "--arch", arch];
    const result = spawnSync(entry.cmd, spawnArgs, {
      stdio: "inherit",
      cwd: ROOT,
      shell: false,
    });
    if (result.error) {
      console.error(`  ❌  Failed to spawn: ${result.error.message}`);
      process.exit(1);
    }
    if (result.status !== 0) {
      console.error(
        `  ❌  Build failed for arch: ${arch} (exit code ${result.status})`,
      );
      process.exit(result.status ?? 1);
    }
    console.log(`\n  ✅  arch ${arch} done\n`);
  }

  process.exit(0);
}

// Non-Windows or non-multi-arch action — single run as before
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
