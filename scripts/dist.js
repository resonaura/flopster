#!/usr/bin/env node
// =============================================================================
//  scripts/dist.js — Flopster release packager
//
//  Builds the correct native installer for the current platform, then wraps
//  the result in a zip ready for distribution.
//
//  Usage:
//    node scripts/dist.js            # auto-detect platform
//    node scripts/dist.js --mac      # macOS  → dist/Flopster-mac-1.24.zip
//    node scripts/dist.js --win      # Windows → dist/Flopster-win-1.24.zip
//    node scripts/dist.js --win --arch arm64   # ARM64 only
//    node scripts/dist.js --win --arch x64     # x64 only
//    node scripts/dist.js --win --arch x86     # x86 only
//    node scripts/dist.js --win --all-arch     # arm64 + x64 + x86 (on ARM host)
//    node scripts/dist.js --linux    # Linux   → dist/Flopster-linux-1.24.zip
//    node scripts/dist.js --rebuild  # force clean rebuild before packaging
//    node scripts/dist.js --no-build # skip build, use existing artefacts
//
//  What goes into each zip:
//
//    macOS   → Flopster-1.24.pkg  +  TROUBLESHOOTING.md
//    Windows → Flopster-1.24-<arch>.msi  +  TROUBLESHOOTING.md
//              (--all-arch produces one zip per architecture)
//    Linux   → flopster_1.24_amd64.deb
//              Flopster-1.24-x86_64.AppImage
//              TROUBLESHOOTING.md
//
//  The pkg/msi/deb are built by the platform-specific scripts in tools/.
//  This script is just the outer wrapper that calls them and zips the output.
// =============================================================================

"use strict";

const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");
const os = require("os");

// ── Config ────────────────────────────────────────────────────────────────────

const ROOT = path.resolve(__dirname, "..");
const DIST = path.join(ROOT, "dist");
const TOOLS = path.join(ROOT, "tools");
const TROUBLE = path.join(ROOT, "TROUBLESHOOTING.md");

// Read version from package.json — single source of truth
const VERSION = JSON.parse(
  fs.readFileSync(path.join(ROOT, "package.json"), "utf8"),
).version;

// ── CLI args ──────────────────────────────────────────────────────────────────

const argv = process.argv.slice(2);

const wantMac = argv.includes("--mac");
const wantWin = argv.includes("--win");
const wantLinux = argv.includes("--linux");
const doRebuild = argv.includes("--rebuild");
const noBuild = argv.includes("--no-build");
const showHelp = argv.includes("--help") || argv.includes("-h");

// Windows arch flags
const allArch = argv.includes("--all-arch");
const archIdx = argv.indexOf("--arch");
const explicitArch = archIdx !== -1 ? argv[archIdx + 1] : null;

// Detect whether we're running on a Windows ARM64 host
function detectWinHostArch() {
  const env = process.env;
  // PROCESSOR_ARCHITECTURE is set to ARM64 natively; on x64 it's AMD64.
  // PROCESSOR_ARCHITEW6432 is set to AMD64 when a 32-bit process runs on x64.
  const pa = (env.PROCESSOR_ARCHITECTURE || "").toLowerCase();
  const pa2 = (env.PROCESSOR_ARCHITEW6432 || "").toLowerCase();
  if (pa === "arm64" || pa2 === "arm64") return "arm64";
  return "x64";
}

const WIN_HOST_ARCH = detectWinHostArch();

// Architectures available when --all-arch is used
// On ARM host all three are natively/cross-supported by MSVC.
// On x64 host we include x64 and x86 (arm64 cross-compile also works but is
// opt-in only — user must pass --arch arm64 explicitly on x64 hosts).
function defaultAllArchList() {
  if (WIN_HOST_ARCH === "arm64") return ["arm64", "x64", "x86"];
  return ["x64", "x86"];
}

function resolveWinArchList() {
  if (allArch) return defaultAllArchList();
  if (explicitArch) {
    const a = explicitArch
      .toLowerCase()
      .replace("amd64", "x64")
      .replace("x86_64", "x64")
      .replace("win32", "x86")
      .replace("i686", "x86")
      .replace("i386", "x86");
    if (!["arm64", "x64", "x86"].includes(a)) {
      die(
        `Unknown --arch value: ${explicitArch}. Valid values: arm64, x64, x86`,
      );
    }
    return [a];
  }
  // Default: all supported archs for this host
  return defaultAllArchList();
}

if (showHelp) {
  console.log(`
  Usage: node scripts/dist.js [platform] [options]

  Platforms (default: current OS):
    --mac              Build macOS distribution  (.pkg inside zip)
    --win              Build Windows distribution (.msi inside zip)
                       Default on ARM host: arm64 + x64 + x86
                       Default on x64 host: x64 + x86
    --linux            Build Linux distribution  (.deb + .AppImage inside zip)

  Windows arch options:
    --arch <arch>      Target a specific architecture only: arm64 | x64 | x86
    --all-arch         Explicitly build all supported architectures (same as default)
                       On ARM host: arm64 + x64 + x86
                       On x64 host: x64 + x86

  General options:
    --rebuild          Force clean rebuild before packaging
    --no-build         Skip the build step, use existing artefacts
    --help             Show this help

  Output:
    dist/Flopster-mac-${VERSION}.zip
    dist/Flopster-win-${VERSION}-arm64.zip   (Windows ARM64, built on ARM host)
    dist/Flopster-win-${VERSION}-x64.zip     (Windows x64)
    dist/Flopster-win-${VERSION}-x86.zip     (Windows x86)
    dist/Flopster-linux-${VERSION}.zip

  On ARM Windows, running --win with no --arch produces all three zips at once.
`);
  process.exit(0);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

const isWin = os.platform() === "win32";
const isLinux = os.platform() === "linux";
const isMac = os.platform() === "darwin";

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
  console.error(`\n  ❌  ${msg}\n`);
  process.exit(1);
}
function step(msg) {
  console.log(`\n  ── ${msg} ──`);
}

// Run a command synchronously, inheriting stdio. Dies on failure.
function run(cmd, args = [], opts = {}) {
  const result = spawnSync(cmd, args, {
    stdio: "inherit",
    cwd: ROOT,
    ...opts,
  });
  if (result.error) die(`Failed to spawn '${cmd}': ${result.error.message}`);
  if (result.status !== 0)
    die(`'${cmd} ${args.join(" ")}' exited with code ${result.status}`);
}

// Run a shell script via bash
function runBash(scriptPath, extraArgs = []) {
  run("bash", [scriptPath, ...extraArgs]);
}

// Run a PowerShell script
function runPwsh(scriptPath, extraArgs = []) {
  run("powershell", ["-ExecutionPolicy", "Bypass", "-File", scriptPath, ...extraArgs]);
}

// Build a zip from a list of file paths into zipPath.
// All source paths must be absolute. They will appear at the top level of the zip.
function buildZip(zipPath, files) {
  // Work in a temp staging dir so all files sit at the zip root
  const stage = path.join(DIST, ".zip-stage");
  if (fs.existsSync(stage)) fs.rmSync(stage, { recursive: true, force: true });
  fs.mkdirSync(stage, { recursive: true });

  for (const src of files) {
    if (!fs.existsSync(src)) {
      warn(`Skipping missing file: ${path.basename(src)}`);
      continue;
    }
    const dest = path.join(stage, path.basename(src));
    const stat = fs.statSync(src);
    if (stat.isDirectory()) {
      fs.cpSync(src, dest, { recursive: true });
    } else {
      fs.copyFileSync(src, dest);
    }
  }

  // Remove old zip
  if (fs.existsSync(zipPath)) fs.rmSync(zipPath);

  if (isWin) {
    const entries = fs
      .readdirSync(stage)
      .map((e) => path.join(stage, e))
      .join('","');
    const ps = `Compress-Archive -Force -Path "${entries}" -DestinationPath "${zipPath}"`;
    run("powershell", ["-NoProfile", "-Command", ps]);
  } else {
    const entries = fs.readdirSync(stage);
    run("zip", ["-r", "--symlinks", zipPath, ...entries], { cwd: stage });
  }

  fs.rmSync(stage, { recursive: true, force: true });
}

function sizeMB(p) {
  try {
    return (fs.statSync(p).size / 1024 / 1024).toFixed(1) + " MB";
  } catch {
    return "?";
  }
}

// ── Extra args to pass down to build tools ────────────────────────────────────

function buildToolArgs() {
  const args = [];
  if (doRebuild) args.push("--rebuild");
  if (noBuild) args.push("--no-build");
  return args;
}

// PowerShell-style args (-Rebuild, -NoBuild) for PS1 scripts
function buildToolArgsPwsh() {
  const args = [];
  if (doRebuild) args.push("-Rebuild");
  if (noBuild) args.push("-NoBuild");
  return args;
}

// ── Platform targets ──────────────────────────────────────────────────────────

function distMac() {
  if (!isMac) die("macOS dist must be built on macOS.");

  step("Building macOS installer (.pkg)");
  const pkgScript = path.join(TOOLS, "mac-pkg.sh");
  if (!fs.existsSync(pkgScript)) die(`Not found: tools/mac-pkg.sh`);
  runBash(pkgScript, buildToolArgs());

  const pkg = path.join(DIST, `Flopster-${VERSION}.pkg`);
  if (!fs.existsSync(pkg)) die(`Expected pkg not found: ${pkg}`);
  ok(`Flopster-${VERSION}.pkg  (${sizeMB(pkg)})`);

  step("Assembling macOS zip");
  const zipPath = path.join(DIST, `Flopster-mac-${VERSION}.zip`);
  buildZip(zipPath, [pkg, TROUBLE]);

  ok(`Flopster-mac-${VERSION}.zip  (${sizeMB(zipPath)})`);
  log(zipPath);

  return zipPath;
}

function distWinArch(arch) {
  if (!isWin) die("Windows dist must be built on Windows.");

  step(`Building Windows installer (.msi) for ${arch}`);
  const msiScript = path.join(TOOLS, "win-msi.ps1");
  if (!fs.existsSync(msiScript)) die(`Not found: tools/win-msi.ps1`);
  runPwsh(msiScript, [...buildToolArgsPwsh(), "-Arch", arch]);

  const msi = path.join(DIST, `Flopster-${VERSION}-${arch}.msi`);
  if (!fs.existsSync(msi)) die(`Expected msi not found: ${msi}`);
  ok(`Flopster-${VERSION}-${arch}.msi  (${sizeMB(msi)})`);

  step(`Assembling Windows zip for ${arch}`);
  const zipPath = path.join(DIST, `Flopster-win-${VERSION}-${arch}.zip`);
  buildZip(zipPath, [msi, TROUBLE]);

  ok(`Flopster-win-${VERSION}-${arch}.zip  (${sizeMB(zipPath)})`);
  log(zipPath);

  return zipPath;
}

function distWin() {
  if (!isWin) die("Windows dist must be built on Windows.");

  const archList = resolveWinArchList();
  log(
    `Windows target arch(es): ${archList.join(", ")}  (host: ${WIN_HOST_ARCH})`,
  );

  const results = [];
  for (const arch of archList) {
    results.push(distWinArch(arch));
  }
  return results;
}

function distLinux() {
  if (!isLinux) die("Linux dist must be built on Linux.");

  step("Building Linux .deb");
  const debScript = path.join(TOOLS, "linux-deb.sh");
  if (!fs.existsSync(debScript)) die(`Not found: tools/linux-deb.sh`);
  runBash(debScript, buildToolArgs());

  step("Building Linux AppImage");
  const appImageScript = path.join(TOOLS, "linux-appimage.sh");
  if (!fs.existsSync(appImageScript)) die(`Not found: tools/linux-appimage.sh`);
  runBash(appImageScript, buildToolArgs());

  // Find the .deb — name includes arch so glob it
  const debFiles = fs
    .readdirSync(DIST)
    .filter((f) => f.startsWith("flopster_") && f.endsWith(".deb"));
  if (debFiles.length === 0) die("Expected .deb not found in dist/");
  const deb = path.join(DIST, debFiles[debFiles.length - 1]);
  ok(`${debFiles[debFiles.length - 1]}  (${sizeMB(deb)})`);

  // Find the AppImage
  const appImageFiles = fs
    .readdirSync(DIST)
    .filter((f) => f.startsWith("Flopster-") && f.endsWith(".AppImage"));
  if (appImageFiles.length === 0) die("Expected .AppImage not found in dist/");
  const appImage = path.join(DIST, appImageFiles[appImageFiles.length - 1]);
  ok(`${appImageFiles[appImageFiles.length - 1]}  (${sizeMB(appImage)})`);

  step("Assembling Linux zip");
  const arch = os.arch() === "arm64" ? "arm64" : "x86_64";
  const zipPath = path.join(DIST, `Flopster-linux-${VERSION}-${arch}.zip`);
  buildZip(zipPath, [deb, appImage, TROUBLE]);

  ok(`${path.basename(zipPath)}  (${sizeMB(zipPath)})`);
  log(zipPath);

  return zipPath;
}

// ── Main ──────────────────────────────────────────────────────────────────────

console.log("");
console.log("  ╔══════════════════════════════════════════════╗");
console.log("  ║   📦  Flopster dist packager                 ║");
console.log(`  ║       v${VERSION}                                ║`);
console.log("  ║       by Shiru & Resonaura                   ║");
console.log("  ╚══════════════════════════════════════════════╝");
console.log("");

fs.mkdirSync(DIST, { recursive: true });

// Resolve which target to run
let target;
if (wantMac) target = "mac";
else if (wantWin) target = "win";
else if (wantLinux) target = "linux";
else if (isMac) target = "mac";
else if (isWin) target = "win";
else if (isLinux) target = "linux";
else die("Could not detect platform. Use --mac, --win, or --linux.");

log(`Platform target: ${target}`);

let result;
switch (target) {
  case "mac":
    result = distMac();
    break;
  case "win":
    result = distWin();
    break;
  case "linux":
    result = distLinux();
    break;
}

console.log("");
console.log("  ╔══════════════════════════════════════════════╗");
console.log("  ║   ✅  Done!                                  ║");
console.log("  ╚══════════════════════════════════════════════╝");
console.log("");
if (Array.isArray(result)) {
  result.forEach((r) => console.log(`  ${r}`));
} else {
  console.log(`  ${result}`);
}
console.log("");
