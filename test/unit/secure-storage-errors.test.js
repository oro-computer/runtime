import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/secure-storage.js', import.meta.url), 'utf8')
  .replace(/^import .*\n/gm, '').replace(/^export /gm, '').replace('default api', '')

test('secure storage missing records return null for native error objects and IPC error instances', async () => {
  let result
  const context = vm.createContext({
    Buffer,
    location: { origin: 'https://public.example' },
    ipc: { async request () { return result } }
  })
  const getItem = vm.runInContext(`${source}; getItem`, context)
  for (const err of [
    { type: 'NotFoundError' },
    Object.assign(new Error('Key not found'), { name: 'NotFoundError', code: 'NOT_FOUND_ERR' }),
    { code: 'NOT_FOUND_ERR' }
  ]) {
    result = { err }
    assert.equal(await getItem('public-missing'), null)
    assert.equal(await getItem('public-missing', { encoding: 'buffer' }), null)
  }
  for (const err of [
    Object.assign(new Error('Origin denied'), { name: 'SecurityError', code: 'SECURITY_ERR' }),
    { type: 'NotReadableError', code: 'EKEYLOST' },
    { code: 'ENOENT', message: 'Native storage service is unavailable' }
  ]) {
    result = { err }
    await assert.rejects(getItem('public-missing'), error => error === err)
  }
  result = { data: { value: '' } }
  assert.equal(await getItem('public-empty'), '')
  result = { data: { value: 'cHVibGljJTJGZGF0YQ==' } }
  assert.equal(Buffer.from(await getItem('public-existing', { encoding: 'buffer' })).toString(), 'public%2Fdata')
})
