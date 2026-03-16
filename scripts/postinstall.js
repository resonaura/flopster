#!/usr/bin/env node
// scripts/postinstall.js — run automatically after `npm install`
// Clones JUCE 8.0.7 if it is not already present.

const { spawnSync } = require("child_process");
const path = require("path");
const fs = require("fs");

const JUCE_DIR = path.resolve(__dirname, "..", "JUCE");
const JUCE_TAG = "8.0.7";
const JUCE_URL = "https://github.com/juce-framework/JUCE.git";

if (fs.existsSync(path.join(JUCE_DIR, "modules"))) {
  console.log("✅  JUCE already present — skipping clone.");
  process.exit(0);
}

console.log(`📦  JUCE not found — cloning JUCE ${JUCE_TAG}...`);

const result = spawnSync(
  "git",
  ["clone", "--depth", "1", "--branch", JUCE_TAG, JUCE_URL, JUCE_DIR],
  { stdio: "inherit" },
);

if (result.error) {
  console.error("❌  git not found. Install Git and re-run npm install.");
  process.exit(1);
}

if (result.status !== 0) {
  console.error("❌  Failed to clone JUCE. Check your internet connection.");
  process.exit(result.status ?? 1);
}

console.log("✅  JUCE cloned successfully.");
