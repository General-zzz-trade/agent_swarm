#!/usr/bin/env node
/**
 * Postinstall script: downloads the pre-built native binary
 * for the current platform from GitHub Releases.
 *
 * Follows the same pattern as esbuild, turbo, biome:
 * npm package is just a shell that fetches the right native binary.
 */

const https = require("https");
const http = require("http");
const fs = require("fs");
const path = require("path");
const os = require("os");
const { execSync } = require("child_process");

const REPO = "General-zzz-trade/agent_swarm";
const VERSION = require("./package.json").version;

function getPlatformKey() {
  const platform = os.platform();
  const arch = os.arch();

  const map = {
    "win32-x64": "windows-x64.exe",
    "linux-x64": "linux-x64",
    "linux-arm64": "linux-arm64",
    "darwin-x64": "macos-x64",
    "darwin-arm64": "macos-arm64",
  };

  const key = `${platform}-${arch}`;
  if (!map[key]) {
    console.error(`Unsupported platform: ${key}`);
    console.error("Supported: win32-x64, linux-x64, linux-arm64, darwin-x64, darwin-arm64");
    console.error("You can build from source: https://github.com/" + REPO);
    process.exit(1);
  }

  return map[key];
}

function download(url, dest) {
  return new Promise((resolve, reject) => {
    const protocol = url.startsWith("https") ? https : http;
    protocol.get(url, { headers: { "User-Agent": "agent-swarm-npm" } }, (res) => {
      // Follow redirects (GitHub releases redirect to CDN)
      if (res.statusCode === 301 || res.statusCode === 302) {
        return download(res.headers.location, dest).then(resolve).catch(reject);
      }

      if (res.statusCode !== 200) {
        reject(new Error(`Download failed: HTTP ${res.statusCode} from ${url}`));
        return;
      }

      const file = fs.createWriteStream(dest);
      res.pipe(file);
      file.on("finish", () => {
        file.close();
        resolve();
      });
      file.on("error", reject);
    }).on("error", reject);
  });
}

async function main() {
  const platformKey = getPlatformKey();
  const binaryName = `agent-swarm-${platformKey}`;
  const binDir = path.join(__dirname, "bin");
  const binPath = path.join(binDir, os.platform() === "win32" ? "agent-swarm.exe" : "agent-swarm");

  // Skip if binary already exists (e.g., from local build)
  if (fs.existsSync(binPath)) {
    console.log("agent-swarm binary already exists, skipping download.");
    return;
  }

  fs.mkdirSync(binDir, { recursive: true });

  // Try downloading from GitHub Releases
  const releaseUrl = `https://github.com/${REPO}/releases/download/v${VERSION}/${binaryName}`;
  console.log(`Downloading agent-swarm v${VERSION} for ${os.platform()}-${os.arch()}...`);
  console.log(`  URL: ${releaseUrl}`);

  try {
    await download(releaseUrl, binPath);
    // Make executable on Unix
    if (os.platform() !== "win32") {
      fs.chmodSync(binPath, 0o755);
    }
    console.log(`agent-swarm installed successfully at ${binPath}`);
  } catch (err) {
    console.error(`\nFailed to download pre-built binary: ${err.message}`);
    console.error("\nAlternatives:");
    console.error("  1. Build from source: git clone && cmake -B build && cmake --build build");
    console.error(`  2. Download manually: https://github.com/${REPO}/releases`);
    console.error("  3. Check if a release exists for v" + VERSION);

    // Don't fail the install — user can still build from source
    // and manually place the binary in node_modules/agent-swarm-cpp/bin/
    process.exit(0);
  }
}

main();
