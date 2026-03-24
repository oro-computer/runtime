import test from 'node:test'
import assert from 'node:assert/strict'

import { flattenIni } from '../src/util/ini.js'
import { parse as parseToml } from '../../api/toml.js'

function isTomlTable (value) {
  if (!value || typeof value !== 'object') return false
  if (Array.isArray(value)) return false
  const proto = Object.getPrototypeOf(value)
  return proto === Object.prototype
}

function flattenTomlObject (table, prefix = '', out = {}) {
  for (const [key, value] of Object.entries(table)) {
    const composedKey = prefix
      ? `${prefix}_${key.replace(/\./g, '_')}`
      : key.replace(/\./g, '_')

    if (isTomlTable(value)) {
      flattenTomlObject(value, composedKey, out)
      continue
    }

    if (Array.isArray(value)) {
      const rendered = value.map((entry) => String(entry))
      const useNewlines = composedKey.endsWith('_headers')
      out[composedKey] = useNewlines ? rendered.join('\n') : rendered.join(' ')
      continue
    }

    out[composedKey] = String(value)
  }

  return out
}

test('basic config fields migrate from INI to TOML without changing semantics', () => {
  const iniSource = `
[build]
name = "oro-runtime-javascript-tests"
flags = "-O3 -g"
headless = true
env[] = PWD
env[] = TMP

[build.extensions]
sqlite3 = src/extensions/sqlite3
simple-ipc-ping = src/extensions/simple/ipc-ping.cc

[webview]
default_index = /
autoindex = true
`.trim()

  const tomlSource = `
[build]
name = "oro-runtime-javascript-tests"
flags = "-O3 -g"
headless = true
env = ["PWD", "TMP"]

[build.extensions]
sqlite3 = "src/extensions/sqlite3"
simple-ipc-ping = "src/extensions/simple/ipc-ping.cc"

[webview]
default_index = "/"
autoindex = true
`.trim()

  const iniFlattened = flattenIni(iniSource)
  const tomlFlattened = flattenTomlObject(parseToml(tomlSource))

  for (const [key, value] of Object.entries(iniFlattened)) {
    assert.equal(
      tomlFlattened[key],
      value,
      `expected TOML key ${key} to equal INI value`
    )
  }
})
