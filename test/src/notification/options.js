import test from 'oro:test'

import { NotificationAction, NotificationOptions } from 'oro:notification'

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
  t.equal(json.requireInteraction, true, 'requireInteraction flag persists')
})
