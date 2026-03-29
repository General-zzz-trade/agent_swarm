#!/usr/bin/env node
/**
 * CLI wrapper: finds and runs the native bolt binary.
 *
 * Usage:
 *   npx bolt agent "Read src/main.cpp"
 *   npx bolt web-chat --port 8080
 *   npx bolt bench --rounds 5
 */

const { execFileSync } = require("child_process");
const path = require("path");
const os = require("os");
const fs = require("fs");

function findBinary() {
  const ext = os.platform() === "win32" ? ".exe" : "";
  const name = "bolt" + ext;

  // Check in package's bin directory
  const packageBin = path.join(__dirname, name);
  if (fs.existsSync(packageBin)) return packageBin;

  // Check for locally built binary (development mode)
  const localBuild = path.join(__dirname, "..", "..", "build", "mini_nn" + ext);
  if (fs.existsSync(localBuild)) return localBuild;

  const localBuildPerf = path.join(__dirname, "..", "..", "build_perf", "mini_nn" + ext);
  if (fs.existsSync(localBuildPerf)) return localBuildPerf;

  console.error("Error: bolt binary not found.");
  console.error("Run 'npm install' to download, or build from source:");
  console.error("  cmake -B build -S . && cmake --build build -j8");
  process.exit(1);
}

const binary = findBinary();
const args = process.argv.slice(2);

try {
  execFileSync(binary, args, {
    stdio: "inherit",
    env: process.env,
  });
} catch (err) {
  if (err.status !== undefined) {
    process.exit(err.status);
  }
  throw err;
}
