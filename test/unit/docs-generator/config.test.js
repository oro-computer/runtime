import test from 'node:test'
import assert from 'node:assert/strict'

import {
  parseIni,
  stripInlineComment,
  firstAssignmentIndex
} from '../../../bin/docs-generator/config.js'

test('stripInlineComment respects quoting and escapes', () => {
  assert.equal(stripInlineComment('value ; note'), 'value')
  assert.equal(stripInlineComment('"semi;colon" ; comment'), '"semi;colon"')
  assert.equal(
    stripInlineComment('"semi\\";colon" ; comment'),
    '"semi\\";colon"'
  )
})

test('firstAssignmentIndex skips delimiters inside quotes', () => {
  const sample = 'key = "value=still" ; comment'
  assert.equal(sample[firstAssignmentIndex(sample)], '=')
  assert.equal(firstAssignmentIndex('no assignment here'), -1)
})

test('parseIni captures defaults, descriptions, and inline comments', () => {
  const iniSource = `
; heading comment
[section]
; description line
; default value: enabled
flag = true ; inline comment
path = "C:\\Program Files\\app" ; inline path
token = "a=value" ; keep equals
`

  const result = parseIni(iniSource)
  assert.deepEqual(result.section, [
    {
      key: 'flag',
      value: 'true',
      defaultValue: 'enabled',
      description: 'description line'
    },
    {
      key: 'path',
      value: '"C:\\Program Files\\app"',
      defaultValue: '',
      description: ''
    },
    {
      key: 'token',
      value: '"a=value"',
      defaultValue: '',
      description: ''
    }
  ])
})
