import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { parse } from 'acorn'
import { simple } from 'acorn-walk'
import vm from 'node:vm'

test('IPC debug traces exclude request bodies, query parameters, events and private results', () => {
  const source = readFileSync(new URL('../../api/ipc.js', import.meta.url), 'utf8')
  const syntax = parse(source, { ecmaVersion: 'latest', sourceType: 'module' })
  const traces = []
  simple(syntax, {
    CallExpression (node) {
      if (node.callee.type === 'MemberExpression' && node.callee.object.name === 'debug' && node.callee.property.name === 'log') {
        traces.push(source.slice(node.start, node.end))
      }
    }
  })
  const privateValue = 'private-credential-and-request-fixture'
  const records = []
  const scope = {
    command: 'runtime.platform',
    seq: 'R1-public-document',
    uri: `ipc://runtime.platform?options=${privateValue}`,
    value: { password: privateValue },
    buffer: Buffer.from(privateValue),
    result: { data: { password: privateValue } },
    name: privateValue,
    target: privateValue,
    options: { password: privateValue },
    debug: { log: (...args) => records.push(args) }
  }
  assert.ok(traces.length >= 10, 'Expected all IPC request, response and event traces')
  for (const trace of traces) vm.runInNewContext(trace, scope)
  assert.equal(records.length, traces.length)
  assert.ok(records.some(record => record.includes('runtime.platform')))
  assert.ok(records.some(record => record.includes('R1-public-document')))
  assert.ok(!JSON.stringify(records).includes(privateValue), 'Private IPC data reached debug output')
})
