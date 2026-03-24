import test from 'oro:test'
import ipc from 'oro:ipc'
import * as usb from 'oro:usb'

const originalRequest = ipc.request
const originalWrite = ipc.write

test.afterEach(() => {
  ipc.request = originalRequest
  ipc.write = originalWrite
})

test('oro:usb exposes the public WebUSB module surface', (t) => {
  t.equal(typeof usb.installNavigatorUSB, 'function', 'install helper exported')
  t.equal(typeof usb.NavigatorUSB, 'function', 'NavigatorUSB exported')
  t.equal(typeof usb.USBDevice, 'function', 'USBDevice exported')
  t.equal(usb.installNavigatorUSB(), navigator.usb, 'install helper returns navigator singleton')
})

function mockIPC (handler) {
  ipc.request = async (route, payload, options) =>
    handler(route, payload, options)
}

function mockWrite (handler) {
  ipc.write = async (route, payload, buffer, options) =>
    handler(route, payload, buffer, options)
}

test('navigator.usb.requestDevice returns single device immediately', async (t) => {
  mockIPC((route) => {
    if (route === 'usb.requestDevice') {
      return {
        data: {
          device: { deviceId: 'dev-1', vendorId: 0x1234, productId: 0xabcd }
        }
      }
    }
    if (route === 'usb.getDevices') {
      return { data: { devices: [] } }
    }
    return {}
  })

  const device = await navigator.usb.requestDevice({ acceptAllDevices: true })
  t.equal(device.deviceId, 'dev-1')
  t.equal(device.vendorId, 0x1234)
  t.equal(device.productId, 0xabcd)
})

test('navigator.usb.requestDevice requires selection and chooser resolves', async (t) => {
  let chooserDetail = null
  mockIPC((route, payload) => {
    if (route === 'usb.requestDevice') {
      return {
        data: {
          requiresSelection: true,
          devices: [
            { deviceId: 'dev-1', vendorId: 4660, productId: 43981 },
            { deviceId: 'dev-2', vendorId: 22136, productId: 61166 }
          ]
        }
      }
    }
    if (route === 'usb.chooseDevice') {
      return {
        data: {
          device: {
            deviceId: payload.deviceId,
            vendorId: 4660,
            productId: 43981
          }
        }
      }
    }
    if (route === 'usb.cancelRequest') {
      return { data: { ok: true } }
    }
    return {}
  })

  const onChooser = (event) => {
    chooserDetail = event.detail
    chooserDetail.select(chooserDetail.devices[0])
  }
  globalThis.addEventListener('usb.chooserequest', onChooser, { once: true })

  const device = await navigator.usb.requestDevice({
    filters: [{ vendorId: 4660 }]
  })
  t.equal(device.deviceId, 'dev-1')
})

test('USBDevice.controlTransferIn enforces maximum payload length', async (t) => {
  mockIPC((route) => {
    if (route === 'usb.transfer.controlIn') {
      t.fail('controlTransferIn should reject before calling ipc.request')
    }
    return {}
  })

  const device = navigator.usb._createDevice({
    deviceId: 'dev-ctl-over',
    vendorId: 1,
    productId: 2
  })
  try {
    await device.controlTransferIn(
      { requestType: 'vendor', recipient: 'device', request: 1 },
      0x10000
    )
    t.fail('Expected RangeError')
  } catch (err) {
    t.equal(err instanceof RangeError, true)
  }
})

test('USBDevice.controlTransferIn forwards valid maximum length to runtime', async (t) => {
  let called = false
  mockIPC((route, payload) => {
    if (route === 'usb.transfer.controlIn') {
      called = true
      t.equal(payload.length, 0xffff)
      return { data: { status: 'ok', transferred: 0, data: '' } }
    }
    return {}
  })

  const device = navigator.usb._createDevice({
    deviceId: 'dev-ctl-max',
    vendorId: 1,
    productId: 2
  })
  await device.controlTransferIn(
    { requestType: 'vendor', recipient: 'device', request: 1 },
    0xffff
  )
  t.equal(called, true)
})

test('USBDevice.controlTransferOut enforces maximum payload length', async (t) => {
  mockWrite(() => {
    t.fail('controlTransferOut should reject before calling ipc.write')
  })

  const device = navigator.usb._createDevice({
    deviceId: 'dev-ctl-out',
    vendorId: 1,
    productId: 2
  })
  const payload = new Uint8Array(0x10000)
  try {
    await device.controlTransferOut(
      { requestType: 'vendor', recipient: 'device', request: 1 },
      payload
    )
    t.fail('Expected RangeError')
  } catch (err) {
    t.equal(err instanceof RangeError, true)
  }
})

test('USBDevice.transferIn enforces maximum transfer length', async (t) => {
  mockIPC((route) => {
    if (route === 'usb.transfer.in') {
      t.fail('transferIn should reject before calling ipc.request')
    }
    return {}
  })

  const device = navigator.usb._createDevice({
    deviceId: 'dev-bulk-over',
    vendorId: 1,
    productId: 2
  })
  try {
    await device.transferIn(1, 0x80000000)
    t.fail('Expected RangeError')
  } catch (err) {
    t.equal(err instanceof RangeError, true)
  }
})
