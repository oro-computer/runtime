import test from 'oro:test'

import { NotificationAction, NotificationOptions, getResponses, acknowledgeResponse } from 'oro:notification'
import os from 'oro:os'
import platform from 'oro:platform'

test('notification receipt acknowledgements validate identity', async (t) => {
  for (const id of ['', null, 42, 'x'.repeat(257)]) {
    try {
      await acknowledgeResponse(id)
      t.fail('invalid receipt identity should reject')
    } catch (err) {
      t.ok(err instanceof TypeError, 'invalid receipt identity rejects before native access')
    }
  }
})

test('notification receipts report unsupported platforms', async (t) => {
  if (os.platform() === 'android') return
  const descriptor = await platform.getSupport('notifications')
  if (descriptor.implementation === 'implemented') {
    t.equal(descriptor.platform, os.platform(), 'notification receipt backend identifies its supported platform')
    return
  }
  for (const request of [() => getResponses(), () => acknowledgeResponse('opaque-receipt')]) {
    try {
      await request()
      t.fail('unsupported receipt API should reject')
    } catch (err) {
      t.equal(err.code, 'ENOSYS', 'unsupported platforms reject explicitly')
    }
  }
})

test('NotificationAction toJSON serializes fields', (t) => {
  const action = new NotificationAction({
    action: 'accept',
    title: 'Accept',
    icon: 'oro://icon.png'
  })

  t.same(
    action.toJSON(),
    {
      action: 'accept',
      title: 'Accept',
      icon: 'oro://icon.png'
    },
    'NotificationAction exposes plain-object representation'
  )
})

test('NotificationOptions toJSON maps actions and clamps vibration', (t) => {
  const options = new NotificationOptions({
    actions: [
      { action: 'accept', title: 'Accept', icon: 'oro://accept.png' },
      { action: 'later', title: 'Later' }
    ],
    vibrate: [100, -50, 20000],
    tag: 'notification-options-regression',
    renotify: true,
    requireInteraction: true
  })

  t.equal(
    options.actions.length,
    2,
    'wraps actions as NotificationAction instances'
  )
  t.ok(
    options.actions[0] instanceof NotificationAction,
    'stores NotificationAction instances'
  )

  const json = options.toJSON()

  t.same(
    json.actions[0],
    {
      action: 'accept',
      title: 'Accept',
      icon: 'oro://accept.png'
    },
    'actions serialize to plain objects'
  )

  t.same(
    json.vibrate,
    [100, 10000, 10000],
    'vibrate sequence is clamped and normalized'
  )
  t.equal(json.renotify, true, 'renotify flag persists')
  t.equal(json.tag, 'notification-options-regression', 'renotify tag persists')
  t.equal(json.requireInteraction, true, 'requireInteraction flag persists')
})
