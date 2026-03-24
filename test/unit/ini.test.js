import test from 'node:test'
import assert from 'node:assert/strict'

import { flattenIni } from '../src/util/ini.js'

test('flattenIni preserves lists and relative sections', () => {
  const source = `
[root]
value = plain
list[] = 1
list[] = 2

[.child]
value = "nested"
`.trim()

  const result = flattenIni(source)
  assert.equal(result.root_value, 'plain')
  assert.equal(result.root_list, '1 2')
  assert.equal(result.root_child_value, 'nested')
})

test('flattenIni decodes escaped sections and single quotes', () => {
  const source = `
[section\\]]
key = 'It\\'s fine'
`.trim()

  const result = flattenIni(source)
  assert.equal(result['section]_key'], "It's fine")
})
