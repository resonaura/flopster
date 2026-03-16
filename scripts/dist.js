#!/usr/bin/env node
// =============================================================================
//  scripts/dist.js — Build a distributable zip archive from compiled artefacts
//
//  Usage:
//    node scripts/dist.js [--mac] [--win] [--all]
//
//  Produces:
//    dist/Flopster-mac-v1.21.zip   — Flopster.vst3 + Flopster.component +
//                                    Flopster.app + mac-install.sh
//    dist/Flopster-win-v1.21.zip   — Flopster.vst3 + Flopster.exe +
//                                    win-install.bat
//
//  Platform behaviour:
//    --mac            requires macOS artefacts (AU/VST3/app); errors if missing
//    --win            requires Windows artefacts (VST3/exe); errors if missing
//    --all / no flag  packs whatever is available for the current platform,
//                     silently skips targets whose artefacts aren't built yet
// =============================================================================

const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");

// ── Config ────────────────────────────────────────────────────────────────────

const VERSION = "1.21";
const ROOT = path.resolve(__dirname, "..");
const DIST = path.join(ROOT, "dist");
const BUILD = path.join(ROOT, "build", "Flopster_artefacts", "Release");

const ASSETS = path.join(ROOT, "assets");
const SAMPLES = path.join(ROOT, "samples");

const TARGETS = {
  mac: {
    zip: `Flopster-mac-v${VERSION}.zip`,
    artefacts: [
      { src: path.join(BUILD, "VST3", "Flopster.vst3"), name: "Flopster.vst3" },
      {
        src: path.join(BUILD, "AU", "Flopster.component"),
        name: "Flopster.component",
      },
      {
        src: path.join(BUILD, "Standalone", "Flopster.app"),
        name: "Flopster.app",
      },
    ],
    resources: [
      { src: path.join(ASSETS, "back.bmp"), name: "back.bmp" },
      { src: path.join(ASSETS, "char.bmp"), name: "char.bmp" },
      { src: SAMPLES, name: "samples" },
    ],
    installer: {
      src: path.join(ROOT, "scripts", "installers", "mac-install.sh"),
      name: "install.sh",
    },
  },
  win: {
    zip: `Flopster-win-v${VERSION}.zip`,
    artefacts: [
      { src: path.join(BUILD, "VST3", "Flopster.vst3"), name: "Flopster.vst3" },
      {
        src: path.join(BUILD, "Standalone", "Flopster.exe"),
        name: "Flopster.exe",
      },
    ],
    resources: [
      { src: path.join(ASSETS, "back.bmp"), name: "back.bmp" },
      { src: path.join(ASSETS, "char.bmp"), name: "char.bmp" },
      { src: SAMPLES, name: "samples" },
    ],
    installer: {
      src: path.join(ROOT, "scripts", "installers", "win-install.bat"),
      name: "install.bat",
    },
  },
};

// ── CLI args ──────────────────────────────────────────────────────────────────

const args = process.argv.slice(2);
const explicitMac = args.includes("--mac");
const explicitWin = args.includes("--win");
const explicitAll = args.includes("--all");
const noFlags = args.length === 0;

// When a target is requested explicitly, we must deliver or die.
// When running --all or with no flags, we only pack what's actually built.
const doMac = explicitMac || explicitAll || noFlags;
const doWin = explicitWin || explicitAll || noFlags;

// ── Helpers ───────────────────────────────────────────────────────────────────

const isWin = process.platform === "win32";

function log(msg) {
  console.log(`  → ${msg}`);
}
function ok(msg) {
  console.log(`  ✅  ${msg}`);
}
function warn(msg) {
  console.warn(`  ⚠️   ${msg}`);
}
function die(msg) {
  console.error(`  ❌  ${msg}`);
  process.exit(1);
}

function checkExists(p, label) {
  if (!fs.existsSync(p))
    die(`${label} not found:\n     ${p}\n     Run npm run build first.`);
}

// Returns true if all artefacts for a target exist, false otherwise.
function artefactsAvailable(name) {
  return TARGETS[name].artefacts.every(({ src }) => fs.existsSync(src));
}

// Returns true if all resources (assets + samples) exist.
function resourcesAvailable() {
  return (
    fs.existsSync(path.join(ASSETS, "back.bmp")) &&
    fs.existsSync(path.join(ASSETS, "char.bmp")) &&
    fs.existsSync(SAMPLES)
  );
}

// Recursively chmod +x a file or directory (mac only, no-op on win)
function chmodX(p) {
  if (isWin || !fs.existsSync(p)) return;
  fs.chmodSync(p, 0o755);
  if (fs.statSync(p).isDirectory()) {
    for (const entry of fs.readdirSync(p)) {
      chmodX(path.join(p, entry));
    }
  }
}

// Run a command, die on failure
function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, { stdio: "inherit", ...opts });
  if (result.error) die(`Failed to run ${cmd}: ${result.error.message}`);
  if (result.status !== 0) die(`${cmd} exited with code ${result.status}`);
}

// Build a zip using the platform-native tooling:
//   macOS/Linux — zip CLI  (comes with macOS, available via apt/brew)
//   Windows     — PowerShell Compress-Archive
function buildZip(zipPath, entries, cwd) {
  // entries: array of names (relative to cwd) to include
  if (isWin) {
    // PowerShell: Compress-Archive takes a list of paths
    const sources = entries.map((e) => path.join(cwd, e)).join('", "');
    const psCmd = `Compress-Archive -Force -Path "${sources}" -DestinationPath "${zipPath}"`;
    run("powershell", ["-NoProfile", "-Command", psCmd]);
  } else {
    run("zip", ["-r", "--symlinks", zipPath, ...entries], { cwd });
  }
}

// ── Main ──────────────────────────────────────────────────────────────────────

console.log("");
console.log("  ╔══════════════════════════════════════════════╗");
console.log("  ║   📦  Flopster dist packager                 ║");
console.log("  ║       by Shiru & Resonaura                   ║");
console.log("  ╚══════════════════════════════════════════════╝");
console.log("");

// Ensure dist/ exists
fs.mkdirSync(DIST, { recursive: true });

// ── Staging directory (cleaned each run) ─────────────────────────────────────

const STAGE = path.join(DIST, ".stage");
if (fs.existsSync(STAGE)) fs.rmSync(STAGE, { recursive: true, force: true });
fs.mkdirSync(STAGE, { recursive: true });

// ── Pack each requested target ────────────────────────────────────────────────

function packTarget(name) {
  const target = TARGETS[name];
  const zipOut = path.join(DIST, target.zip);
  const stageDir = path.join(STAGE, name);

  console.log(`  ── Packing ${name.toUpperCase()} ──────────────────────────`);
  fs.mkdirSync(stageDir, { recursive: true });

  // Verify & copy artefacts
  for (const { src, name: destName } of target.artefacts) {
    checkExists(src, destName);
    log(`Copying ${destName}...`);
    const dest = path.join(stageDir, destName);
    fs.cpSync(src, dest, { recursive: true });
    ok(destName);
  }

  // Copy resources (assets + samples)
  if (!resourcesAvailable()) {
    die(
      "assets/ or samples/ not found. Make sure they exist in the project root.",
    );
  }
  for (const { src, name: destName } of target.resources) {
    checkExists(src, destName);
    log(`Copying ${destName}...`);
    const dest = path.join(stageDir, destName);
    fs.cpSync(src, dest, { recursive: true });
    ok(destName);
  }

  // Copy installer script
  const { src: instSrc, name: instName } = target.installer;
  checkExists(instSrc, instName);
  log(`Copying installer (${instName})...`);
  const instDest = path.join(stageDir, instName);
  fs.copyFileSync(instSrc, instDest);
  chmodX(instDest);
  ok(instName);

  // Remove old zip if present
  if (fs.existsSync(zipOut)) {
    fs.rmSync(zipOut);
    log(`Removed old ${target.zip}`);
  }

  // Build zip (entries are relative names inside stageDir)
  log(`Building ${target.zip}...`);
  const entries = fs.readdirSync(stageDir);
  buildZip(zipOut, entries, stageDir);

  const sizeMB = (fs.statSync(zipOut).size / 1024 / 1024).toFixed(1);
  ok(`${target.zip}  (${sizeMB} MB)`);
  console.log(`     → ${zipOut}`);
  console.log("");
}

// ── Resolve which targets to actually run ─────────────────────────────────────

// Explicit --win on macOS / --mac on Windows: unsupported, tell the user why.
if (explicitWin && !isWin) {
  die(
    "Cannot build a Windows dist on macOS — Windows artefacts require MSVC\n" +
      "     and Windows SDK which are not available on macOS.\n" +
      "     Build on a Windows machine and run  npm run dist:win  there.",
  );
}
if (explicitMac && isWin) {
  die(
    "Cannot build a macOS dist on Windows — AU/VST3 bundles require\n" +
      "     Xcode toolchain which is not available on Windows.\n" +
      "     Build on a Mac and run  npm run dist:mac  there.",
  );
}

let packed = 0;

if (doMac) {
  if (!artefactsAvailable("mac")) {
    if (explicitMac) {
      // Should not normally reach here after the platform check above, but
      // handle the case where artefacts simply weren't built yet.
      die("macOS artefacts not found. Run  npm run mac:build  first.");
    } else {
      warn(
        "macOS artefacts not found — skipping mac dist.  (Run npm run mac:build first.)",
      );
    }
  } else {
    packTarget("mac");
    packed++;
  }
}

if (doWin) {
  if (!artefactsAvailable("win")) {
    if (explicitWin) {
      die("Windows artefacts not found. Run  npm run win:build  first.");
    } else {
      warn(
        "Windows artefacts not found — skipping win dist.  (Build on Windows first.)",
      );
    }
  } else {
    packTarget("win");
    packed++;
  }
}

if (packed === 0) {
  warn("Nothing was packed. Build the project first with  npm run build.");
  process.exit(1);
}

// Clean up staging
fs.rmSync(STAGE, { recursive: true, force: true });

console.log("  ✅  Done.");
console.log("");
