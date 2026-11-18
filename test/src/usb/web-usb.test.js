import register from 'oro:test'
import os from 'oro:os'
import * as usb from 'oro:usb'

function test (name, run) {
  if (os.platform() === 'ios') return
  register(name, async t => {
    const activation = Object.getOwnPropertyDescriptor(navigator, 'userActivation')
    const xhr = globalThis.XMLHttpRequest.prototype
    const open = xhr.open
    try {
      await run(t)
    } finally {
      xhr.open = open
      if (activation) Object.defineProperty(navigator, 'userActivation', activation)
      else delete navigator.userActivation
    }
  })
}

test('oro:usb exposes the public WebUSB module surface', (t) => {
  t.equal(typeof usb.installNavigatorUSB, 'function', 'install helper exported')
  t.equal(typeof usb.NavigatorUSB, 'function', 'NavigatorUSB exported')
  t.equal(typeof usb.USBDevice, 'function', 'USBDevice exported')
  t.equal(usb.installNavigatorUSB(), navigator.usb, 'install helper returns navigator singleton')
})

function mockIPC (handler) {
  const xhr = globalThis.XMLHttpRequest.prototype
  const open = xhr.open
  xhr.open = function (method, uri, ...args) {
    const url = new URL(uri, globalThis.location.href)
    if (url.protocol !== 'ipc:' || !(url.hostname.startsWith('usb.') || url.hostname === 'permissions.query')) {
      return open.call(this, method, uri, ...args)
    }
    this.send = body => {
      const result = handler(url.hostname, Object.fromEntries(url.searchParams), body)
      Object.defineProperties(this, {
        readyState: { value: this.DONE },
        status: { value: result === null ? 0 : 200 },
        responseText: { value: result === null ? '' : JSON.stringify({ source: url.hostname, ...result }) }
      })
      this.dispatchEvent(new Event('readystatechange'))
      this.dispatchEvent(new Event(result === null ? 'error' : 'load'))
    }
  }
}

test('USB control transfers reject when the IPC transport fails', async t => {
  mockIPC(() => null)
  const device = navigator.usb._createDevice({ deviceId: 'failed-transfer' })
  const setup = { requestType: 'vendor', recipient: 'device', request: 1, value: 0, index: 0 }
  await t.rejects(device.controlTransferIn(setup, 18), err => err.name === 'NetworkError', 'failed read rejects')
  await t.rejects(device.controlTransferOut(setup, new Uint8Array(1)), err => err.name === 'NetworkError', 'failed write rejects')
})

test('navigator.usb.requestDevice resolves the native picker selection', async (t) => {
  Object.defineProperty(navigator, 'userActivation', { configurable: true, value: { isActive: true } })
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
  Object.defineProperty(navigator, 'userActivation', { configurable: true, value: { isActive: true } })
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
    event.preventDefault()
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
      { requestType: 'vendor', recipient: 'device', request: 1, value: 0, index: 0 },
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
      t.equal(Number(payload.length), 0xffff)
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
    { requestType: 'vendor', recipient: 'device', request: 1, value: 0, index: 0 },
    0xffff
  )
  t.equal(called, true)
})

test('USBDevice.controlTransferOut enforces maximum payload length', async (t) => {
  mockIPC(() => {
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
      { requestType: 'vendor', recipient: 'device', request: 1, value: 0, index: 0 },
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

test('USB device selection requires activation before IPC', async t => {
  Object.defineProperty(navigator, 'userActivation', { configurable: true, value: { isActive: false } })
  mockIPC(() => t.fail('inactive request reached IPC'))
  await t.rejects(navigator.usb.requestDevice({ filters: [] }), err => err.name === 'SecurityError', 'activation required')
})

test('USB configurations expose claimed alternate endpoints and preserve identity', async t => {
  const device = new usb.USBDevice({
    deviceId: 'fixture',
    configurations: [2],
    configurationValue: 2,
    interfaces: [{
      configurationValue: 2,
      interfaceNumber: 3,
      alternateSetting: 0,
      classCode: 255,
      endpoints: [{ endpointNumber: 4, direction: 'in', type: 'interrupt', packetSize: 64 }]
    }]
  })
  mockIPC(() => ({ data: { configurationValue: 2 } }))
  await device.open()
  const iface = device.configuration.interfaces[0]
  await device.claimInterface(3)
  t.equal(iface.claimed, true, 'claim updates the existing interface')
  t.equal(iface.alternate.endpoints[0].type, 'interrupt', 'endpoint type is discoverable')
  await device.close()
  t.equal(iface.claimed, false, 'close releases the interface')
})

test('failed configuration changes preserve only the remaining native claims', async t => {
  const device = new usb.USBDevice({
    deviceId: 'configuration-switch',
    configurations: [1, 2],
    configurationValue: 1,
    opened: true,
    interfaces: [0, 1, 2].map(interfaceNumber => ({
      configurationValue: 1, interfaceNumber, alternateSetting: 0, classCode: 255
    })),
    claimedInterfaces: [0, 1, 2].map(interfaceNumber => ({ interfaceNumber, alternateSetting: 0 }))
  })
  const interfaces = device.configuration.interfaces
  for (const releasedInterfaces of [[], [0], [1, 2]]) {
    mockIPC(() => ({ err: { type: 'NetworkError', name: 'NetworkError', message: 'USB configuration failed', releasedInterfaces } }))
    await t.rejects(device.selectConfiguration(2), err => err.name === 'NetworkError', 'reports the native failure')
    t.equal(device.configuration.configurationValue, 1, 'original configuration stays selected')
    for (const number of releasedInterfaces) t.equal(interfaces[number].claimed, false, 'released claim is cleared')
    t.equal(interfaces[2].claimed, !releasedInterfaces.includes(2), 'retains a claim until its release succeeds')
  }
})

test('WebUSB validates dependent filters and passes serial and exclusion filters', async t => {
  Object.defineProperty(navigator, 'userActivation', { configurable: true, value: { isActive: true } })
  mockIPC((route, payload) => {
    const options = JSON.parse(payload.options)
    t.equal(options.filters[0].serialNumber, 'fixture', 'serial filter reaches native service')
    t.equal(options.exclusionFilters[0].vendorId, 7, 'exclusion reaches native service')
    return { data: { device: { deviceId: 'matched' } } }
  })
  await t.rejects(navigator.usb.requestDevice({ filters: [{ productId: 7 }] }), err => err instanceof TypeError, 'product requires vendor')
  await navigator.usb.requestDevice({ filters: [{ serialNumber: 'fixture' }], exclusionFilters: [{ vendorId: 7 }] })
})

test('USB permission queries return only matching grants without a picker', async t => {
  mockIPC(route => {
    if (route === 'permissions.query') return { data: { state: 'prompt' } }
    t.equal(route, 'usb.getDevices', 'only granted devices are enumerated')
    return {
      data: {
        devices: [
          { deviceId: 'allowed', vendorId: 1, productId: 2 },
          { deviceId: 'excluded', vendorId: 1, productId: 3 }
        ]
      }
    }
  })
  const result = await navigator.permissions.query({
    name: 'usb', filters: [{ vendorId: 1 }], exclusionFilters: [{ vendorId: 1, productId: 3 }]
  })
  t.equal(result.state, 'prompt', 'query keeps the device picker available')
  t.equal(result.devices.length, 1, 'one grant matches')
  t.equal(result.devices[0].deviceId, 'allowed', 'the matching device is exposed')
  t.equal(Object.isFrozen(result.devices), true, 'the list is read-only')
})

test('USB disconnect invalidates the old object on reattachment', async t => {
  const descriptor = { deviceId: 'replugged', opened: true }
  const device = navigator.usb._createDevice(descriptor)
  globalThis.dispatchEvent(new CustomEvent('usb.devicedisconnect', { detail: { device: descriptor } }))
  mockIPC(() => t.fail('the detached object must not open the replacement'))
  await t.rejects(device.open(), err => err.name === 'NotFoundError', 'old object stays disconnected')
  t.equal(navigator.usb._createDevice(descriptor) === device, false, 'reattachment creates a fresh object')
})

test('isochronous IN preserves requested offsets for short packets', async t => {
  mockIPC(() => ({
    data: {
      data: 'YQAAAGJj',
      packets: [{ status: 'ok', transferred: 1 }, { status: 'ok', transferred: 2 }]
    }
  }))
  const device = navigator.usb._createDevice({ deviceId: 'iso' })
  const result = await device.isochronousTransferIn(1, [4, 2])
  t.equal(result.packets[0].data.byteLength, 1, 'short packet exposes actual length')
  t.equal(result.packets[1].data.getUint8(0), 98, 'next packet begins at its reserved offset')
})
