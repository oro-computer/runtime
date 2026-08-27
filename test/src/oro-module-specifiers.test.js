import test from 'oro:test'
import processRuntime from 'oro:process'
import { createRequire } from 'oro:module'
import ipc from 'oro:ipc'
import application from 'oro:application'

test('oro:process exposes the Oro Runtime version', (t) => {
  t.equal(typeof processRuntime.versions?.oro, 'string', 'oro version is available')
})

test('CommonJS require resolves oro:* specifiers', (t) => {
  const require = createRequire(import.meta.url)
  const os = require('oro:os')
  t.equal(typeof os.platform, 'function', 'platform helper is available')
})

test('oro:test re-exports the same runner', (t) => {
  t.equal(typeof test, 'function', 'oro:test exports the test harness')
})

test('oro:test registrations execute via Oro specifier', (t) => {
  t.ok(true, 'oro:test executed a test case via Oro module specifier')
})

test('oro:ipc exposes request primitives', (t) => {
  t.equal(typeof ipc.request, 'function', 'request helper is available')
  t.equal(typeof ipc.Result, 'function', 'Result constructor is available')
})

test('oro:application exposes config and APIs', (t) => {
  t.equal(typeof application.config, 'object', 'config object is available')
  t.equal(
    typeof application.getCurrentWindow,
    'function',
    'oro:application exports window methods'
  )
})
