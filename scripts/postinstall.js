#!/usr/bin/env node
// scripts/postinstall.js — run automatically after `npm install`
// Clones JUCE 8.0.7 if it is not already present.
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
    Object.assign({ stdio: "inherit", cwd: ROOT_DIR }, opts),
  );
  if (res.error) {
    console.error(`❌  Failed to run ${cmd}: ${res.error.message}`);
    return { ok: false, status: 1 };
  }
  return { ok: res.status === 0, status: res.status };
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
    if (result.status === 127) {
      console.error("❌  git not found. Install Git and re-run npm install.");
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

  // Attempt to create a relative symlink for portability
  const relativeTarget = path.join("build", "compile_commands.json");
  fs.symlinkSync(relativeTarget, rootCompileDb);
  console.log(
    `🔗  Created symlink: compile_commands.json -> ${relativeTarget}`,
  );
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
