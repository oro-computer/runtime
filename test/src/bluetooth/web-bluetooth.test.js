import { test } from 'oro:test'
import ipc from 'oro:ipc'

test('navigator.bluetooth injected', async (t) => {
  t.ok(typeof navigator !== 'undefined', 'navigator exists')
  t.ok('bluetooth' in navigator, 'navigator.bluetooth exists')
  t.equal(
    typeof navigator.bluetooth.requestDevice,
    'function',
    'requestDevice exists'
  )
  t.equal(
    typeof navigator.bluetooth.getAvailability,
    'function',
    'getAvailability exists'
  )
})

test('getAvailability returns a boolean', async (t) => {
  const available = await navigator.bluetooth.getAvailability()
  t.equal(typeof available, 'boolean', 'getAvailability resolves to boolean')
})

test('requestDevice option validation', async (t) => {
  await t.rejects(
    () => navigator.bluetooth.requestDevice(null),
    /options must be an object/i
  )
  await t.rejects(
    () => navigator.bluetooth.requestDevice({}),
    /either filters must be non-empty|acceptAllDevices/i
  )
  await t.rejects(
    () =>
      navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        filters: [{ services: ['0000180D-0000-1000-8000-00805f9b34fb'] }]
      }),
    /filters may not be used/i
  )
  await t.rejects(
    () => navigator.bluetooth.requestDevice({ filters: [123] }),
    /BluetoothDeviceFilter must be an object/i
  )
  await t.rejects(
    () => navigator.bluetooth.requestDevice({ filters: [{}] }),
    /must include at least one of services|name|namePrefix|manufacturerData/i
  )
  await t.rejects(
    () =>
      navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        servicesMatch: 'sometimes'
      }),
    /servicesMatch must be 'all' or 'any'/i
  )
})

test('requestDevice emits chooser and can be cancelled', async (t) => {
  const events = []
  const onChoose = () => events.push('choose')
  globalThis.addEventListener('bluetooth.chooserequest', onChoose, {
    once: true
  })
  const p = navigator.bluetooth.requestDevice({ acceptAllDevices: true })
  // Wait briefly for chooser then cancel via IPC
  await new Promise((resolve) => setTimeout(resolve, 50))
  try {
    await ipc.request('bluetooth.cancelRequest')
  } catch {}
  await t.rejects(() => p, /AbortError|Cancelled/i)
  t.ok(events.includes('choose'), 'chooser event observed')
})

test('servicesMatch=any is accepted and still cancelable', async (t) => {
  const events = []
  const onChoose = () => events.push('choose')
  globalThis.addEventListener('bluetooth.chooserequest', onChoose, {
    once: true
  })
  const p = navigator.bluetooth.requestDevice({
    acceptAllDevices: true,
    servicesMatch: 'any'
  })
  await new Promise((resolve) => setTimeout(resolve, 50))
  try {
    await ipc.request('bluetooth.cancelRequest')
  } catch {}
  await t.rejects(() => p, /AbortError|Cancelled/i)
  t.ok(
    events.includes('choose'),
    'chooser event observed for servicesMatch=any'
  )
})

test('manufacturerData byte sources serialize to arrays', async (t) => {
  const original = ipc.request
  let captured = null
  ipc.request = async (route, payload, body) => {
    if (route === 'bluetooth.requestDevice') {
      captured = payload
      return { err: { type: 'AbortError', message: 'Cancelled' } }
    }
    return original(route, payload, body)
  }

  const prefix = new Uint8Array([1, 2, 3])
  const mask = new Uint8Array([255, 0, 1])

  try {
    await t.rejects(
      () =>
        navigator.bluetooth.requestDevice({
          filters: [
            {
              manufacturerData: [
                {
                  companyIdentifier: 0x1234,
                  dataPrefix: prefix,
                  dataMask: mask
                }
              ]
            }
          ]
        }),
      /AbortError|Cancelled|NotFound|No device/i
    )
    t.ok(captured, 'IPC payload captured')
    const entry = captured?.options?.filters?.[0]?.manufacturerData?.[0]
    t.same(entry?.dataPrefix, [1, 2, 3], 'dataPrefix converts to number array')
    t.same(entry?.dataMask, [255, 0, 1], 'dataMask converts to number array')
    t.equal(entry?.companyIdentifier, 0x1234, 'companyIdentifier preserved')
  } finally {
    ipc.request = original
  }
})

test('writeValue variants and device helpers exist', async (t) => {
  const mod = await import('oro:internal/bluetooth-web')
  const { BluetoothDevice, BluetoothRemoteGATTService } = mod
  const ipcMod = await import('oro:ipc')
  const ipcInstance = ipcMod.default || ipcMod

  const calls = []
  const original = ipcInstance.request
  ipcInstance.request = async (route, args, payload) => {
    calls.push({ route, args, payload })
    if (route === 'bluetooth.service.getCharacteristic') {
      return { data: {} }
    }
    return { data: {} }
  }

  try {
    const raw = String.fromCharCode(0xaa, 0xbb)
    const encoded = btoa(raw)
    const dev = new BluetoothDevice({
      id: 'dev-write',
      name: 'Writer',
      services: ['0000180d-0000-1000-8000-00805f9b34fb'],
      manufacturerData: [
        { companyId: 0x1234, data: encoded, encoding: 'base64' }
      ]
    })
    await t.rejects(() => dev.watchAdvertisements(), /not supported/i)
    await t.rejects(() => dev.forget(), /not supported/i)
    t.equal(Array.isArray(dev.uuids), true, 'uuids populated as array')
    t.equal(
      dev.uuids.includes('0000180d-0000-1000-8000-00805f9b34fb'),
      true,
      'uuids contain normalized service'
    )
    const md = dev.manufacturerData.get(0x1234)
    t.ok(md instanceof DataView, 'manufacturerData entry is DataView')
    t.equal(md.byteLength, 2, 'manufacturerData decoded length matches')
    const bytes = new Uint8Array(md.buffer)
    t.equal(bytes[0], 0xaa, 'manufacturer data first byte decoded')
    t.equal(bytes[1], 0xbb, 'manufacturer data second byte decoded')

    const svc = new BluetoothRemoteGATTService(
      dev.gatt,
      '0000180a-0000-1000-8000-00805f9b34fb',
      true
    )
    const ch = await svc.getCharacteristic(
      '00002a29-0000-1000-8000-00805f9b34fb'
    )

    await ch.writeValueWithResponse(new Uint8Array([1]))
    await ch.writeValueWithoutResponse(new Uint8Array([2]))

    const writeCalls = calls.filter(
      (c) => c.route === 'bluetooth.characteristic.writeValue'
    )
    t.equal(writeCalls.length, 2, 'both write routes invoked')
    const withoutResponse = writeCalls.find((c) => c.args?.writeWithoutResponse)
    t.ok(withoutResponse, 'writeValueWithoutResponse forwards flag')
  } finally {
    ipcInstance.request = original
  }
})

test('devicefound detail exposes manufacturerDataMap', async (t) => {
  const mod = await import('oro:internal/bluetooth-web')
  const { BluetoothManufacturerDataMap } = mod
  const raw = String.fromCharCode(0xaa)
  const encoded = btoa(raw)
  const detail = {
    id: 'evt-1',
    name: 'Beacon',
    services: ['0000180f-0000-1000-8000-00805f9b34fb'],
    manufacturerData: [{ companyId: 0x4321, data: encoded }],
    rssi: -42
  }
  globalThis.dispatchEvent(new CustomEvent('bluetooth.devicefound', { detail }))
  t.ok(
    detail.manufacturerDataMap instanceof BluetoothManufacturerDataMap,
    'detail includes manufacturerDataMap'
  )
  const dv = detail.manufacturerDataMap.get(0x4321)
  t.ok(dv instanceof DataView, 'manufacturer entry is DataView')
  t.equal(dv.byteLength, 1, 'decoded manufacturer payload length')
  const bytes = new Uint8Array(dv.buffer)
  t.equal(bytes[0], 0xaa, 'manufacturer payload decoded correctly')
})

test('watchAdvertisements and forget proxy to backend routes', async (t) => {
  const mod = await import('oro:internal/bluetooth-web')
  const { BluetoothDevice } = mod
  const ipcMod = await import('oro:ipc')
  const ipcInstance = ipcMod.default || ipcMod

  const calls = []
  const original = ipcInstance.request
  ipcInstance.request = async (route, params) => {
    calls.push({ route, params })
    return { data: {} }
  }

  try {
    const dev = new BluetoothDevice({ id: 'dev-routes' })
    await dev.watchAdvertisements()
    await dev.forget()
    const watchCall = calls.find(
      (c) => c.route === 'bluetooth.device.watchAdvertisements'
    )
    const forgetCall = calls.find((c) => c.route === 'bluetooth.device.forget')
    t.ok(watchCall, 'watchAdvertisements routed to backend')
    t.same(
      watchCall?.params?.deviceId,
      'dev-routes',
      'watch call includes deviceId'
    )
    t.ok(forgetCall, 'forget routed to backend')
  } finally {
    ipcInstance.request = original
  }
})
