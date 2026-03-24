// Minimal pattern to recreate fs.Watcher instances after applicationresume.
// Drop this into your app’s bootstrap to make file watching resilient.

import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

// Choose files you want to watch. This example uses `oro.toml` (or the legacy
// `oro.ini` shim) if present, otherwise falls back to a temp file.
const CONFIG_CANDIDATES = ['oro.toml', 'oro.ini']
const demoTmp = path.join(os.tmpdir(), `oro-watch-${Date.now()}.txt`)

/**
 * Return a list of paths to watch. Replace this with your app’s files.
 */
async function getWatchList () {
  for (const candidate of CONFIG_CANDIDATES) {
    try {
      const stat = await fs.promises.stat(candidate)
      if (stat && stat.isFile?.()) return [candidate]
    } catch {}
  }
  try {
    await fs.promises.writeFile(demoTmp, 'watch me', 'utf8')
  } catch {}
  return [demoTmp]
}

const watchers = new Map()
let files = []

async function createWatcher (file) {
  try {
    const watcher = new fs.Watcher(file)
    watcher.on('change', (evt, filename) => {
      console.log(`[watch] ${evt}:`, filename || file)
    })
    watchers.set(file, watcher)
  } catch (err) {
    console.warn(`[watch] failed to watch ${file}:`, err?.message || err)
  }
}

async function openWatchers () {
  if (!files.length) files = await getWatchList()
  for (const file of files) await createWatcher(file)
}

async function closeWatchers () {
  for (const [file, watcher] of watchers) {
    try {
      await watcher.close()
    } catch {}
    watchers.delete(file)
  }
}

// Initial open
openWatchers()

// Close on pause; reopen on resume. Stop on applicationstop.
globalThis.addEventListener('applicationpause', async () => {
  await closeWatchers()
})

globalThis.addEventListener('applicationresume', async () => {
  await openWatchers()
})

globalThis.addEventListener('applicationstop', async () => {
  await closeWatchers()
})

export {}
