#!/usr/bin/env node
// scripts/set-version.js
// Updates the version string in all three places that track it:
//   - src/version.h          (FLOPSTER_VERSION macro, shown in UI)
//   - package.json           (npm package version field)
//   - CMakeLists.txt         (project() VERSION + juce_add_plugin VERSION)
//
// Usage:
//   node scripts/set-version.js 1.23
//   npm run set-version 1.23

"use strict";

const fs = require("fs");
const path = require("path");

// ── Argument parsing ──────────────────────────────────────────────────────────
const v = process.argv[2];

if (!v || !/^\d+\.\d+(\.\d+)?$/.test(v)) {
  console.error("Usage: npm run set-version <x.y[.z]>");
  console.error("  e.g. npm run set-version 1.23");
  process.exit(1);
}

const ROOT = path.resolve(__dirname, "..");

// ── Helper: read → transform → write (reports changes) ───────────────────────
function patch(relPath, transform) {
  const abs = path.join(ROOT, relPath);
  const before = fs.readFileSync(abs, "utf8");
  const after = transform(before);
  if (after === before) {
    console.log(`  (no change)  ${relPath}`);
  } else {
    fs.writeFileSync(abs, after, "utf8");
    console.log(`  ✅ updated   ${relPath}`);
  }
}

console.log(`Setting version → ${v}\n`);

// ── 1. src/version.h ─────────────────────────────────────────────────────────
patch(
  "src/version.h",
  () =>
    `#pragma once\n` +
    `// Edit this value to update the plugin version string, then run install.sh.\n` +
    `// npm run set-version <x.y.z> also updates this file automatically.\n` +
    `#define FLOPSTER_VERSION "${v}"\n`,
);

// ── 2. package.json ───────────────────────────────────────────────────────────
patch("package.json", (src) => {
  const pkg = JSON.parse(src);
  pkg.version = v;
  return JSON.stringify(pkg, null, 2) + "\n";
});

// ── 3. CMakeLists.txt ─────────────────────────────────────────────────────────
// Two separate version declarations live in CMakeLists.txt:
//
//   project(Flopster VERSION 1.21)
//   VERSION                     "1.21"      ← inside juce_add_plugin(...)
//
patch("CMakeLists.txt", (src) => {
  // project() line — no quotes around the version
  let out = src.replace(/^(project\s*\(\s*\w+\s+VERSION\s+)[\d.]+/m, `$1${v}`);

  // juce_add_plugin VERSION field — quoted value on its own line
  out = out.replace(/^(\s+VERSION\s+)"[\d.]+"/m, `$1"${v}"`);

  return out;
});

console.log("\nDone. Rebuild the plugin to apply the new version.");
