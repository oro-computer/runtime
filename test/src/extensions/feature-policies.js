import extension from 'oro:extension'
import test from 'oro:test'
import ipc from 'oro:ipc'

test('extension.load(name) - feature policies', async (t) => {
  let result = null
  let simple = null

  try {
    simple = await extension.load('simple-ipc-ping')
  } catch (err) {
    return t.ifError(err)
  }

  result = await ipc.request('simple.ping', { value: 'hello world' })
  if (result.err) return t.ifError(result.err)
  t.equal(result.data, 'hello world', 'ipc default')

  await simple.unload()

  try {
    simple = await extension.load('simple-ipc-ping', { allow: ['none'] })
  } catch (err) {
    return t.ifError(err)
  }

  result = await ipc.request('simple.ping', { value: 'hello world' })
  t.ok(/not found/i.test(result.err?.message), 'ipc disabled')

  await simple.unload()

  try {
    simple = await extension.load('simple-ipc-ping', {
      allow: ['context', 'ipc']
    })
  } catch (err) {
    return t.ifError(err)
  }

  result = await ipc.request('simple.ping', { value: 'hello world' })
  if (result.err) return t.ifError(result.err)
  t.equal(result.data, 'hello world', 'ipc enabled')

  await simple.unload()
})
