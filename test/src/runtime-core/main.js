import extension from 'oro:extension'
import test from 'oro:test'

test('runtime core native suite', async (t) => {
  try {
    const runtimeCore = await extension.load('runtime-core-tests')
    t.ok(runtimeCore.loaded, 'native suite completed successfully')
    t.ok(await runtimeCore.unload(), 'native suite extension unloaded')
  } catch (err) {
    t.ifError(err)
  }
})
