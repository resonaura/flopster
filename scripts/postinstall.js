#!/usr/bin/env node
// scripts/postinstall.js — run automatically after `npm install`
// Clones JUCE 8.0.7 if it is not already present.
// On Windows: checks that all required build tools are present and prints
// clear instructions for anything that is missing.
// Additionally generates CMake compile_commands.json and places (or links) a copy
// at the repository root for language servers (clangd / IDEs).  Ensures the
// compile_commands.json entry is present in .gitignore so it is not committed.

const { spawnSync } = require("child_process");
const path = require("path");
const fs = require("fs");

const ROOT_DIR = path.resolve(__dirname, "..");
const JUCE_DIR = path.join(ROOT_DIR, "JUCE");
const JUCE_TAG = "8.0.7";
const JUCE_URL = "https://github.com/juce-framework/JUCE.git";

function runCommand(cmd, args, opts = {}) {
  const res = spawnSync(
    cmd,
    args,
    Object.assign({ stdio: "inherit", cwd: ROOT_DIR, shell: true }, opts),
  );
  if (res.error) {
    console.error(`❌  Failed to run ${cmd}: ${res.error.message}`);
    return { ok: false, status: 1, notFound: res.error.code === "ENOENT" };
  }
  return { ok: res.status === 0, status: res.status };
}

// Run a command silently and return { ok, stdout }
function probe(cmd, args = []) {
  const res = spawnSync(cmd, args, { encoding: "utf8", shell: true });
  return {
    ok: !res.error && res.status === 0,
    stdout: (res.stdout || "").trim(),
  };
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows prerequisite checks
// ─────────────────────────────────────────────────────────────────────────────
if (process.platform === "win32") {
  console.log("");
  console.log("  ┌─────────────────────────────────────────────────┐");
  console.log("  │  🔍  Checking Windows build prerequisites        │");
  console.log("  └─────────────────────────────────────────────────┘");
  console.log("");

  let allOk = true;

  // ── 1. Git ──────────────────────────────────────────────────────────────────
  {
    const r = probe("git", ["--version"]);
    if (r.ok) {
      console.log(`  ✅  git          ${r.stdout}`);
    } else {
      console.log("  ❌  git          NOT FOUND");
      console.log("       Install: https://git-scm.com/download/win");
      console.log('       ⚠️  Check "Add Git to PATH" during install.');
      allOk = false;
    }
  }

  // ── 2. CMake ─────────────────────────────────────────────────────────────────
  {
    const r = probe("cmake", ["--version"]);
    if (r.ok) {
      const ver = r.stdout.split("\n")[0];
      console.log(`  ✅  cmake        ${ver}`);
    } else {
      console.log("  ❌  cmake        NOT FOUND");
      console.log("       Install: https://cmake.org/download/");
      console.log(
        '       ⚠️  Check "Add CMake to system PATH" during install.',
      );
      allOk = false;
    }
  }

  // ── 3. Visual Studio with C++ workload (via vswhere) ─────────────────────────
  {
    const vswherePaths = [
      path.join(
        process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)",
        "Microsoft Visual Studio",
        "Installer",
        "vswhere.exe",
      ),
      path.join(
        process.env["ProgramFiles"] || "C:\\Program Files",
        "Microsoft Visual Studio",
        "Installer",
        "vswhere.exe",
      ),
    ];
    const vswhere = vswherePaths.find((p) => fs.existsSync(p));

    if (vswhere) {
      // Check for VS with C++ Desktop workload (MSBuild component implies it)
      const r = probe(`"${vswhere}"`, [
        "-latest",
        "-requires",
        "Microsoft.VisualCpp.Tools.HostX64.TargetX64",
        "-property",
        "installationVersion",
      ]);
      if (r.ok && r.stdout) {
        // Also get the display name
        const rName = probe(`"${vswhere}"`, [
          "-latest",
          "-requires",
          "Microsoft.VisualCpp.Tools.HostX64.TargetX64",
          "-property",
          "displayName",
        ]);
        const name = rName.ok ? rName.stdout : "Visual Studio";
        console.log(`  ✅  VS C++       ${name} ${r.stdout}`);
      } else {
        console.log(
          "  ❌  VS C++       Visual Studio found but C++ workload missing",
        );
        console.log(
          "       Open Visual Studio Installer → Modify your VS install.",
        );
        console.log('       Select workload: "Desktop development with C++"');
        console.log("       Make sure these components are checked:");
        console.log("         • MSVC v143 (or newer) — C++ compiler");
        console.log("         • Windows 11 SDK (latest)");
        console.log("         • C++ CMake tools for Windows");
        allOk = false;
      }
    } else {
      console.log("  ❌  VS C++       Visual Studio NOT FOUND");
      console.log("       Install: https://visualstudio.microsoft.com/");
      console.log('       Select workload: "Desktop development with C++"');
      console.log("       Make sure these components are checked:");
      console.log("         • MSVC v143 (or newer) — C++ compiler");
      console.log("         • Windows 11 SDK (latest)");
      console.log("         • C++ CMake tools for Windows");
      allOk = false;
    }
  }

  // ── 4. .NET SDK (required for WiX v4) ────────────────────────────────────────
  {
    const r = probe("dotnet", ["--version"]);
    if (r.ok) {
      console.log(`  ✅  .NET SDK     ${r.stdout}`);
    } else {
      console.log("  ❌  .NET SDK     NOT FOUND");
      console.log("       Install: https://dotnet.microsoft.com/download");
      console.log("       (Required for WiX v4 — needed by npm run dist)");
      allOk = false;
    }
  }

  // ── 5. WiX v4 ────────────────────────────────────────────────────────────────
  {
    const r = probe("wix", ["--version"]);
    if (r.ok) {
      console.log(`  ✅  WiX v4       ${r.stdout}`);

      // Check UI extension
      const rExt = probe("wix", ["extension", "list"]);
      if (rExt.ok && rExt.stdout.includes("WixToolset.UI")) {
        console.log("  ✅  WiX UI ext   WixToolset.UI.wixext installed");
      } else {
        console.log("  ⚠️   WiX UI ext   WixToolset.UI.wixext NOT installed");
        console.log("       Run: wix extension add WixToolset.UI.wixext");
      }
    } else {
      // WiX v3 fallback check
      const r3 = probe("candle.exe", ["-?"]);
      if (r3.ok) {
        console.log("  ✅  WiX v3       candle.exe found (v3 fallback)");
      } else {
        console.log("  ❌  WiX          NOT FOUND (needed for npm run dist)");
        console.log("       Install WiX v4 (recommended):");
        console.log("         dotnet tool install --global wix");
        console.log("         wix extension add WixToolset.UI.wixext");
        console.log("       OR WiX v3: https://wixtoolset.org/releases/");
        // Not fatal — dist is optional, warn only
      }
    }
  }

  // ── Summary ───────────────────────────────────────────────────────────────────
  console.log("");
  if (allOk) {
    console.log("  ✅  All required build tools are present.");
  } else {
    console.log("  ⚠️   Some required tools are missing (see above).");
    console.log(
      "      Install them, open a NEW terminal, then re-run: npm install",
    );
  }
  console.log("");
}

// --- Clone JUCE if missing ---
if (fs.existsSync(path.join(JUCE_DIR, "modules"))) {
  console.log("✅  JUCE already present — skipping clone.");
} else {
  console.log(`📦  JUCE not found — cloning JUCE ${JUCE_TAG}...`);

  const result = runCommand("git", [
    "clone",
    "--depth",
    "1",
    "--branch",
    JUCE_TAG,
    JUCE_URL,
    JUCE_DIR,
  ]);

  if (!result.ok) {
    if (result.notFound || result.status === 127 || result.status === 9009) {
      console.error("❌  git not found in PATH.");
      console.error("    Install Git from https://git-scm.com/download/win");
      console.error('    Make sure to check "Add Git to PATH" during install,');
      console.error("    then open a new terminal and re-run: npm install");
    } else {
      console.error(
        "❌  Failed to clone JUCE. Check your internet connection.",
      );
    }
    process.exit(result.status ?? 1);
  }

  console.log("✅  JUCE cloned successfully.");
}

// --- Generate compile_commands.json via CMake (idempotent) ---

// On Windows, CMake without an activated MSVC environment defaults to NMake
// which is almost never installed standalone. Skip gracefully unless we're
// already inside a Developer/vcvarsall shell (VCINSTALLDIR is set in that case).
if (process.platform === "win32" && !process.env["VCINSTALLDIR"]) {
  console.warn("⚠️  Skipping compile_commands.json generation on Windows.");
  console.warn(
    "    Run from a 'Developer PowerShell for VS' prompt to enable this.",
  );
  process.exit(0);
}

console.log("🔧  Generating compile_commands.json with CMake...");

const cmakeArgs = [
  "-B",
  "build",
  "-DCMAKE_BUILD_TYPE=Debug",
  "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
];
const cmakeRes = runCommand("cmake", cmakeArgs);

if (!cmakeRes.ok) {
  console.warn(
    "⚠️  CMake failed to generate compile database. Skipping compile_commands.json generation.",
  );
  // Do not fail npm install for this — language server integration is convenience only.
  process.exit(0);
}

// Path inside build and repo root
const buildCompileDb = path.join(ROOT_DIR, "build", "compile_commands.json");
const rootCompileDb = path.join(ROOT_DIR, "compile_commands.json");

// Ensure build/compile_commands.json exists
if (!fs.existsSync(buildCompileDb)) {
  console.warn(
    "⚠️  compile_commands.json was not produced in build/. Skipping symlink/copy.",
  );
  process.exit(0);
}

// Try to (re)create a symlink at repo root pointing to build/compile_commands.json.
// On systems where symlink requires privileges (Windows), fall back to copying the file.
try {
  // Remove any existing file or symlink at rootCompileDb
  try {
    fs.unlinkSync(rootCompileDb);
  } catch (e) {
    /* ignore if not present */
  }

  // Copy to repo root so it survives build-directory rebuilds
  fs.copyFileSync(buildCompileDb, rootCompileDb);
  console.log("📄  Copied compile_commands.json to repository root.");
} catch (symlinkErr) {
  try {
    // Fallback: copy the file into repo root
    fs.copyFileSync(buildCompileDb, rootCompileDb);
    console.log(
      "📄  Copied compile_commands.json to repository root (symlink not available).",
    );
  } catch (copyErr) {
    console.warn(
      "⚠️  Failed to create symlink or copy compile_commands.json:",
      copyErr.message,
    );
    console.warn(
      "      Language server integration will not be configured automatically.",
    );
    process.exit(0);
  }
}

// --- Ensure compile_commands.json is ignored in .gitignore (idempotent) ---
const gitignorePath = path.join(ROOT_DIR, ".gitignore");
const gitignoreEntry = "compile_commands.json";

try {
  let gi = "";
  if (fs.existsSync(gitignorePath)) {
    gi = fs.readFileSync(gitignorePath, "utf8");
  }

  if (!gi.split(/\r?\n/).some((line) => line.trim() === gitignoreEntry)) {
    // Append the entry to .gitignore with a newline
    const toAppend =
      gi.length && !gi.endsWith("\n")
        ? "\n" + gitignoreEntry + "\n"
        : gitignoreEntry + "\n";
    fs.appendFileSync(gitignorePath, toAppend, "utf8");
    console.log("✅  Added compile_commands.json to .gitignore");
  } else {
    console.log("✅  .gitignore already ignores compile_commands.json");
  }
} catch (e) {
  console.warn("⚠️  Could not update .gitignore:", e.message);
}

console.log("✅  postinstall tasks complete.");
