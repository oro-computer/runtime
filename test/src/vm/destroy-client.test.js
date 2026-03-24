import { test } from 'oro:test'
import { Script } from 'oro:vm'

test('vm: Script.destroy() detaches client without error', async (t) => {
  const script = new Script('(() => 42)()', {
    filename: 'vm-destroy-client.test.js'
  })
  const result = await script.runInContext({})
  t.equal(result, 42, 'script executed with expected result')
  await script.destroy()
  t.pass('script.destroy() resolved')
})
