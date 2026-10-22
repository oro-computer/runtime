import test from 'oro:test'
import background from 'oro:background'
import os from 'oro:os'

test('native background tasks reject arbitrary execution and serialized payloads', async t => {
  await t.rejects(background.register('javascript'), TypeError, 'JS execution is not a native task kind')
  await t.rejects(background.schedule({ kind: 'share-prune', payload: 'credential' }), TypeError, 'payloads are rejected')
  await t.rejects(background.schedule({ kind: 'share-prune', delayMs: -1 }), TypeError, 'negative delays are rejected')
  await t.rejects(background.schedule({ kind: 'share-prune', requiresCharging: 'yes' }), TypeError, 'constraints are typed')
  if (os.platform() !== 'android') {
    try {
      await background.status()
      t.fail('native scheduling must reject on an unsupported platform')
    } catch (error) {
      t.equal(error.code, 'ENOSYS', 'unsupported native scheduling is explicit')
    }
    return
  }
  const status = await background.status()
  t.equal(status.backgroundJavaScript, false, 'a scheduled native task is not durable JS execution')
  t.equal(status.push, false, 'native cleanup is independent of push transport')
  t.deepEqual(status.supportedKinds, ['share-prune'], 'only implemented native kinds are advertised')
})
