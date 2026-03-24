import test from 'oro:test'
import ipc from 'oro:ipc'
import { Buffer } from 'oro:buffer'

const originalRequest = ipc.request
const originalWrite = ipc.write

test.afterEach(() => {
  ipc.request = originalRequest
  ipc.write = originalWrite
})

function mockIPC (handler) {
  ipc.request = async (route, payload, options) =>
    handler(route, payload, options)
}

function mockWrite (handler) {
  ipc.write = async (route, payload, buffer, options) =>
    handler(route, payload, buffer, options)
}

test('navigator.hid.requestDevice returns single device', async (t) => {
  mockIPC((route) => {
    if (route === 'hid.requestDevice') {
      return {
        data: {
          device: { deviceId: 'hid-1', vendorId: 0x1234, productId: 0xabcd }
        }
      }
    }
    if (route === 'hid.getDevices') {
      return { data: { devices: [] } }
    }
    return {}
  })

  const device = await navigator.hid.requestDevice({ acceptAllDevices: true })
  t.equal(device.deviceId, 'hid-1')
  t.equal(device.vendorId, 0x1234)
  t.equal(device.productId, 0xabcd)
})

test('navigator.hid.requestDevice requires selection and chooser resolves', async (t) => {
  let chooserDetail = null
  mockIPC((route, payload) => {
    if (route === 'hid.requestDevice') {
      return {
        data: {
          requiresSelection: true,
          devices: [
            { deviceId: 'hid-a', vendorId: 0x1111, productId: 0x2222 },
            { deviceId: 'hid-b', vendorId: 0x3333, productId: 0x4444 }
          ]
        }
      }
    }
    if (route === 'hid.chooseDevice') {
      return {
        data: {
          device: {
            deviceId: payload.deviceId,
            vendorId: 0x1111,
            productId: 0x2222
          }
        }
      }
    }
    if (route === 'hid.cancelRequest') {
      return { data: { ok: true } }
    }
    return {}
  })

  const onChooser = (event) => {
    chooserDetail = event.detail
    chooserDetail.select(chooserDetail.devices[0])
  }
  globalThis.addEventListener('hid.chooserequest', onChooser, { once: true })

  const device = await navigator.hid.requestDevice({
    filters: [{ vendorId: 0x1111 }]
  })
  t.equal(device.deviceId, 'hid-a')
})

test('HIDDevice.sendReport writes payload including reportId prefix', async (t) => {
  const observed = {}
  mockIPC((route) => {
    if (route === 'hid.device.open') {
      return {
        data: {
          device: {
            deviceId: 'hid-2',
            vendorId: 1,
            productId: 2,
            opened: true,
            collections: []
          }
        }
      }
    }
    if (route === 'hid.getDevices') {
      return {
        data: {
          devices: [
            {
              deviceId: 'hid-2',
              vendorId: 1,
              productId: 2,
              authorized: true,
              collections: []
            }
          ]
        }
      }
    }
    if (route === 'hid.requestDevice') {
      return {
        data: {
          device: {
            deviceId: 'hid-2',
            vendorId: 1,
            productId: 2,
            authorized: true,
            collections: []
          }
        }
      }
    }
    if (route === 'hid.device.close') {
      return { data: { device: { deviceId: 'hid-2', opened: false } } }
    }
    return { data: { ok: true } }
  })

  mockWrite((route, payload, buffer) => {
    observed.route = route
    observed.payload = payload
    observed.buffer = new Uint8Array(buffer)
    return { data: { ok: true } }
  })

  const device = await navigator.hid.requestDevice({ acceptAllDevices: true })
  await device.open()
  await device.sendReport(5, new Uint8Array([1, 2, 3]))
  t.equal(observed.route, 'hid.device.sendReport')
  t.equal(observed.payload.reportId, 5)
  t.same(Array.from(observed.buffer), [1, 2, 3])
})

test('HIDDevice dispatches inputreport events', async (t) => {
  mockIPC((route) => {
    if (route === 'hid.requestDevice') {
      return {
        data: {
          device: {
            deviceId: 'hid-3',
            vendorId: 0x1,
            productId: 0x2,
            authorized: true,
            collections: []
          }
        }
      }
    }
    if (route === 'hid.getDevices') {
      return { data: { devices: [] } }
    }
    if (route === 'hid.device.open') {
      return { data: { device: { deviceId: 'hid-3', opened: true } } }
    }
    return { data: { ok: true } }
  })

  const device = await navigator.hid.requestDevice({ acceptAllDevices: true })
  await device.open()

  const reports = []
  device.addEventListener('inputreport', (event) => {
    reports.push({
      reportId: event.reportId,
      data: Array.from(new Uint8Array(event.data.buffer))
    })
  })

  const payload = Buffer.from([9, 10, 11]).toString('base64')
  const eventDetail = { deviceId: 'hid-3', reportId: 7, data: payload }
  globalThis.dispatchEvent(
    new CustomEvent('hid.inputreport', { detail: eventDetail })
  )

  t.equal(reports.length, 1)
  t.equal(reports[0].reportId, 7)
  t.same(reports[0].data, [9, 10, 11])
})

test('HIDDevice.receiveFeatureReport preserves leading bytes', async (t) => {
  mockIPC((route, payload) => {
    if (route === 'hid.requestDevice') {
      return {
        data: {
          device: {
            deviceId: 'hid-4',
            vendorId: 0x20,
            productId: 0x30,
            authorized: true,
            collections: []
          }
        }
      }
    }
    if (route === 'hid.getDevices') {
      return { data: { devices: [] } }
    }
    if (route === 'hid.device.open') {
      return { data: { device: { deviceId: 'hid-4', opened: true } } }
    }
    if (route === 'hid.device.receiveFeatureReport') {
      const data = Buffer.from([0, 0xaa, 0xbb]).toString('base64')
      return { data: { reportId: payload.reportId, encoding: 'base64', data } }
    }
    return { data: { ok: true } }
  })

  const device = await navigator.hid.requestDevice({ acceptAllDevices: true })
  await device.open()
  const view = await device.receiveFeatureReport(0, 0)
  t.equal(view.byteLength, 3)
  const bytes = Array.from(new Uint8Array(view.buffer))
  t.same(bytes, [0, 0xaa, 0xbb])
})

test('HIDDevice.receiveFeatureReport tolerates payload shorter than requested', async (t) => {
  mockIPC((route, payload) => {
    if (route === 'hid.requestDevice') {
      return {
        data: {
          device: {
            deviceId: 'hid-5',
            vendorId: 0x40,
            productId: 0x50,
            authorized: true,
            collections: []
          }
        }
      }
    }
    if (route === 'hid.getDevices') {
      return { data: { devices: [] } }
    }
    if (route === 'hid.device.open') {
      return { data: { device: { deviceId: 'hid-5', opened: true } } }
    }
    if (route === 'hid.device.receiveFeatureReport') {
      const data = Buffer.from([0xde]).toString('base64')
      t.equal(payload.length, 16)
      return { data: { reportId: payload.reportId, encoding: 'base64', data } }
    }
    return { data: { ok: true } }
  })

  const device = await navigator.hid.requestDevice({ acceptAllDevices: true })
  await device.open()
  const view = await device.receiveFeatureReport(2, 16)
  t.equal(view.byteLength, 1)
  t.same(Array.from(new Uint8Array(view.buffer)), [0xde])
})

test('navigator.hid emits connect and disconnect events from native bridge', async (t) => {
  const connected = []
  const disconnected = []

  const onConnect = (event) => {
    connected.push({
      id: event.detail.device.deviceId,
      vendor: event.detail.device.vendorId,
      product: event.detail.device.productId,
      opened: event.detail.device.opened
    })
  }

  const onDisconnect = (event) => {
    disconnected.push({
      id: event.detail.device.deviceId,
      opened: event.detail.device.opened
    })
  }

  navigator.hid.addEventListener('connect', onConnect)
  navigator.hid.addEventListener('disconnect', onDisconnect)

  try {
    globalThis.dispatchEvent(
      new CustomEvent('hid.deviceconnect', {
        detail: {
          device: {
            deviceId: 'hid-bridge-1',
            vendorId: 0x1111,
            productId: 0x2222,
            opened: true,
            authorized: true
          }
        }
      })
    )

    globalThis.dispatchEvent(
      new CustomEvent('hid.devicedisconnect', {
        detail: {
          device: {
            deviceId: 'hid-bridge-1'
          }
        }
      })
    )

    t.equal(connected.length, 1)
    t.equal(connected[0].id, 'hid-bridge-1')
    t.equal(connected[0].vendor, 0x1111)
    t.equal(connected[0].product, 0x2222)
    t.equal(connected[0].opened, true)

    t.equal(disconnected.length, 1)
    t.equal(disconnected[0].id, 'hid-bridge-1')
    t.equal(disconnected[0].opened, false)
  } finally {
    navigator.hid.removeEventListener('connect', onConnect)
    navigator.hid.removeEventListener('disconnect', onDisconnect)
  }
})
