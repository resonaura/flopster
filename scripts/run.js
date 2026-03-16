#!/usr/bin/env node
// scripts/run.js — cross-platform build runner
// Usage: node scripts/run.js <script> [args...]
//   script: build | install
//   args:   --debug | --release | --rebuild (any combination)

const { spawnSync } = require("child_process");
const os = require("os");

const isWin = os.platform() === "win32";

const [, , script, ...args] = process.argv;

const commands = {
  build: isWin
    ? ["build.bat", args, { shell: true }]
    : ["bash", ["build.sh", ...args], {}],
  install: isWin
    ? ["install.bat", args, { shell: true }]
    : ["bash", ["install.sh", ...args], {}],
};

if (!commands[script]) {
  console.error(
    `Unknown script: "${script}". Available: ${Object.keys(commands).join(", ")}`,
  );
  process.exit(1);
}

const [cmd, cmdArgs, opts] = commands[script];

const result = spawnSync(cmd, cmdArgs, { stdio: "inherit", ...opts });

process.exit(result.status ?? 1);
