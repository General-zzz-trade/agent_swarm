#!/usr/bin/env node
/**
 * Bolt CLI wrapper — finds and runs the native binary.
 *
 * Uses spawn (not execFileSync) for proper stdin/stdout/stderr
 * passthrough, including interactive mode and streaming output.
 */

const { spawn } = require("child_process");
const path = require("path");
const os = require("os");
const fs = require("fs");

function findBinary() {
  const ext = os.platform() === "win32" ? ".exe" : "";
  const name = "bolt" + ext;

  // Check in package's bin directory (downloaded by postinstall)
  const packageBin = path.join(__dirname, name);
  if (fs.existsSync(packageBin)) return packageBin;

  // Check for locally built binary (development mode)
  for (const dir of ["build", "build_perf"]) {
    const local = path.join(__dirname, "..", "..", dir, name);
    if (fs.existsSync(local)) return local;
  }

  console.error("Error: bolt binary not found.");
  console.error("Run 'npm install' to download, or build from source:");
  console.error("  cmake -B build -S . && cmake --build build -j8");
  process.exit(1);
}

const binary = findBinary();
const args = process.argv.slice(2);

// Show help if no args
if (args.length === 0) {
  args.push("agent");  // Default to interactive agent mode
}

// Use spawn for proper interactive terminal support
const child = spawn(binary, args, {
  stdio: "inherit",  // Pass through stdin/stdout/stderr directly
  env: process.env,
});

child.on("error", (err) => {
  console.error("Failed to start bolt:", err.message);
  process.exit(1);
});

child.on("exit", (code) => {
  process.exit(code || 0);
});
