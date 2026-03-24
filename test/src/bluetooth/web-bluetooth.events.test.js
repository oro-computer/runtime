import { test } from 'oro:test'

test('readValue sets characteristic.value and notification updates it', async (t) => {
  const mod = await import('oro:internal/bluetooth-web')
  const { BluetoothDevice, BluetoothRemoteGATTService } = mod
  const ipcMod = await import('oro:ipc')
  const ipc = ipcMod.default || ipcMod

  // Stub ipc.request for characteristic methods
  const original = ipc.request
  ipc.request = async (route, args, payload) => {
    if (route === 'bluetooth.service.getCharacteristic') {
      return { data: {} }
    }
    if (route === 'bluetooth.characteristic.readValue') {
      return { data: new Uint8Array([1, 2, 3]) }
    }
    return original(route, args, payload)
  }

  try {
    const dev = new BluetoothDevice({ id: 'dev1', name: 'Test Dev' })
    const svc = new BluetoothRemoteGATTService(
      dev.gatt,
      '0000180a-0000-1000-8000-00805f9b34fb',
      true
    )
    const ch = await svc.getCharacteristic(
      '00002a29-0000-1000-8000-00805f9b34fb'
    )

    const dv = await ch.readValue()
    t.ok(dv instanceof DataView, 'readValue returns DataView')
    t.ok(
      ch.value instanceof DataView,
      'characteristic.value is set after readValue'
    )
    t.equal(ch.value.byteLength, 3, 'value length matches stubbed data')

    // Listen for notify and confirm value is updated
    const notified = new Promise((resolve) => {
      ch.addEventListener(
        'characteristicvaluechanged',
        () => resolve(ch.value),
        { once: true }
      )
    })
    const bytes = new Uint8Array([9, 8, 7])
    let s = ''
    for (let i = 0; i < bytes.length; ++i) s += String.fromCharCode(bytes[i])
    const b64 = btoa(s)
    globalThis.dispatchEvent(
      new CustomEvent('bluetooth.characteristicvaluechanged', {
        detail: {
          deviceId: dev.id,
          service: svc.uuid,
          characteristic: ch.uuid,
          value: b64,
          encoding: 'base64'
        }
      })
    )
    const v = await notified
    t.ok(v instanceof DataView, 'notification updates characteristic.value')
    t.equal(v.byteLength, 3, 'notification payload length matches')
  } finally {
    ipc.request = original
  }
})
