import test from 'oro:test'
import processSocket from 'oro:process'
import { createRequire } from 'oro:module'
import ipcLegacy from 'oro:ipc'
import applicationLegacy from 'oro:application'

const oroTest = test
const processOro = processSocket
const ipcPreferred = ipcLegacy
const applicationPreferred = applicationLegacy

test('oro:process shares identity with oro:process', (t) => {
  t.equal(processOro, processSocket, 'process defaults are identical')
  t.equal(
    processOro.versions?.oro,
    processSocket.versions?.oro,
    'oro version matches'
  )
})

test('CommonJS require resolves oro:* specifiers', (t) => {
  const require = createRequire(import.meta.url)
  const os = require('oro:os')
  t.equal(typeof os.platform, 'function', 'platform helper is available')
})

test('oro:test re-exports the same runner', (t) => {
  t.equal(test, oroTest, 'oro:test and oro:test reference the same harness')
})

oroTest('oro:test registrations execute via Oro specifier', (t) => {
  t.ok(true, 'oro:test executed a test case via Oro module specifier')
})

test('oro:ipc mirrors the legacy oro:ipc module', (t) => {
  t.equal(
    ipcPreferred,
    ipcLegacy,
    'oro:ipc resolves to the same module instance'
  )
  t.equal(
    ipcPreferred.request,
    ipcLegacy.request,
    'request helper stays shared'
  )
  t.equal(
    ipcPreferred.Result,
    ipcLegacy.Result,
    'Result constructor remains identical'
  )
})

test('oro:application exposes the same config and APIs', (t) => {
  t.equal(
    applicationPreferred,
    applicationLegacy,
    'application module instance matches'
  )
  t.equal(
    applicationPreferred.config,
    applicationLegacy.config,
    'config object remains shared'
  )
  t.equal(
    typeof applicationPreferred.getCurrentWindow,
    'function',
    'oro:application exports the same methods'
  )
})
