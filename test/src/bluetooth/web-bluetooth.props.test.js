import { test } from 'oro:test'

test('getCharacteristics maps properties to characteristic.properties', async (t) => {
  const mod = await import('oro:internal/bluetooth-web')
  const { BluetoothDevice, BluetoothRemoteGATTService } = mod
  const ipcMod = await import('oro:ipc')
  const ipc = ipcMod.default || ipcMod

  const original = ipc.request
  ipc.request = async (route, args, payload) => {
    if (route === 'bluetooth.service.getCharacteristics') {
      return {
        data: {
          characteristics: [
            '00002a37-0000-1000-8000-00805f9b34fb',
            '00002a38-0000-1000-8000-00805f9b34fb'
          ],
          propertiesByCharacteristic: {
            '00002a37-0000-1000-8000-00805f9b34fb': { notify: true },
            '00002a38-0000-1000-8000-00805f9b34fb': { read: true }
          }
        }
      }
    }
    return original(route, args, payload)
  }

  try {
    const dev = new BluetoothDevice({ id: 'dev2', name: 'Props Dev' })
    const svc = new BluetoothRemoteGATTService(
      dev.gatt,
      '0000180d-0000-1000-8000-00805f9b34fb',
      true
    )
    const list = await svc.getCharacteristics()
    t.equal(Array.isArray(list), true, 'getCharacteristics returns an array')
    t.equal(list.length, 2, 'received two characteristics')
    t.equal(
      list[0].properties.notify,
      true,
      'notify property set from backend mapping'
    )
    t.equal(
      list[1].properties.read,
      true,
      'read property set from backend mapping'
    )
  } finally {
    ipc.request = original
  }
})
