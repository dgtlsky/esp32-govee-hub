// Replaces every file in ../data with a .gz copy. The ESP32 serves these with Content-Encoding: gzip,
// which keeps the LittleFS image small and page loads fast.
import { readdirSync, readFileSync, statSync, unlinkSync, writeFileSync } from "node:fs"
import { join } from "node:path"
import { fileURLToPath } from "node:url"
import { gzipSync } from "node:zlib"

const root = fileURLToPath(new URL("../../data/", import.meta.url))

function walk(dir) {
  for (const name of readdirSync(dir)) {
    const path = join(dir, name)
    if (statSync(path).isDirectory()) {
      walk(path)
    } else if (!path.endsWith(".gz")) {
      writeFileSync(`${path}.gz`, gzipSync(readFileSync(path), { level: 9 }))
      unlinkSync(path)
    }
  }
}

walk(root)
