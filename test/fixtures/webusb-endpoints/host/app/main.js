import { installNavigatorUSB } from 'oro:usb'
import { checkDeviceDescriptors, sameBytes } from './descriptors.js'

const fixture = [{ vendorId: 0xcafe, productId: 0x4030 }]
const pixel = [{ vendorId: 0x18d1 }]
const drive = [{ vendorId: 0x13fe, productId: 0x6500 }]
const buttons = document.querySelectorAll('button')
let usb

function log (line) {
  document.querySelector('pre').append(line + '\n')
  console.log('USB qualification — ' + line)
}

function check (ok, message) {
  if (!ok) throw new Error(message)
}

async function checkPixelDescriptors (device) {
  if (!/^Pixel(?:\b|_)/i.test(device.productName) && ![0x2d00, 0x2d01].includes(device.productId)) {
    throw new Error('Select a Pixel or its Android Open Accessory interface.')
  }
  await checkDeviceDescriptors(device, log)
}

async function echo (device, number, kind) {
  const bytes = crypto.getRandomValues(new Uint8Array(23))
  const sent = await device.transferOut(number, bytes)
  check(sent.status === 'ok' && sent.bytesWritten === bytes.length, kind + ' OUT length or status mismatch')
  const received = await device.transferIn(number, 32)
  check(received.status === 'ok' && sameBytes(received.data, bytes), kind + ' IN length, bytes or status mismatch')
}

function checkCleanupError (error, expectRemoval) {
  // On removal the backend can already have discarded the open handle and its claims.
  if (expectRemoval && ['NotFoundError', 'InvalidStateError'].includes(error.name)) return
  throw error
}

async function checkEndpoints (device, expectRemoval) {
  check(device.serialNumber === 'ORO-ENDPOINT-1', 'Select the Oro endpoint fixture')
  if (!device.configuration) await device.selectConfiguration(1)
  const iface = device.configuration.interfaces.find(i => i.interfaceNumber === 0)
  const alt = iface?.alternates.find(a => a.alternateSetting === 1)
  check(alt?.interfaceClass === 255 && alt.interfaceSubclass === 0x53 && alt.interfaceProtocol === 1, 'Unexpected fixture alternate')
  const types = ['bulk', 'interrupt', 'isochronous']
  for (const [i, type] of types.entries()) {
    for (const direction of ['out', 'in']) {
      check(alt.endpoints.some(e => e.endpointNumber === i + 1 && e.direction === direction && e.type === type), 'Missing ' + type + ' ' + direction)
    }
  }
  await device.claimInterface(0)
  try {
    await device.selectAlternateInterface(0, 1)
    if (expectRemoval) {
      const arm = await device.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request: 0x41, value: 0, index: 0 })
      check(arm.status === 'ok' && arm.bytesWritten === 0, 'Select the running Linux virtual fixture to test disconnection')
      try {
        await device.isochronousTransferIn(3, [16])
      } catch (error) {
        if (error.name !== 'NotFoundError') throw error
        log('Pending isochronous read rejected with NotFoundError on disconnect.')
        return
      }
      throw new Error('Isochronous read completed after fixture disconnect')
    }
    await echo(device, 1, 'Bulk')
    await echo(device, 2, 'Interrupt')
    log('Bulk and interrupt each returned 23 matching bytes on a 32-byte read.')
    const arm = await device.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request: 0x40, value: 0, index: 0 })
    check(arm.status === 'ok' && arm.bytesWritten === 0, 'Could not arm the fixture bulk IN stall')
    const held = await device.transferIn(1, 32)
    check(held.status === 'stall', 'Expected bulk IN to report stall, got ' + held.status)
    await device.clearHalt('in', 1)
    await echo(device, 1, 'Bulk after clearHalt')
    log('Bulk IN reported stall. clearHalt restored a byte-checked bulk echo.')

    const sizes = [11, 16, 7]
    const payload = crypto.getRandomValues(new Uint8Array(34))
    const sent = await device.isochronousTransferOut(3, payload, sizes)
    check(sent.packets.length === sizes.length && sent.packets.every((p, i) => p.status === 'ok' && p.bytesWritten === sizes[i]), 'Isochronous OUT packet length or status mismatch: ' + JSON.stringify(sent.packets.map(p => [p.status, p.bytesWritten])))
    const received = await device.isochronousTransferIn(3, [16, 16, 16])
    check(received.packets.length === 3, 'Expected three isochronous IN packets')
    let offset = 0
    for (const [i, packet] of received.packets.entries()) {
      check(packet.status === 'ok' && sameBytes(packet.data, payload.subarray(offset, offset + sizes[i])), 'Isochronous IN mismatch in packet ' + i)
      check(packet.data.byteOffset - received.data.byteOffset === i * 16, 'Incorrect IN packet offset')
      offset += sizes[i]
    }
    log('Isochronous OUT and IN matched all three packets (11, 16, 7 bytes), including short IN packets at offsets 0, 16, 32.')
  } finally {
    await device.releaseInterface(0).catch(error => checkCleanupError(error, expectRemoval))
  }
}

async function run (name, filters, verify, expectRemoval = false) {
  for (const button of buttons) button.disabled = true
  try {
    const device = await usb.requestDevice({ filters })
    await device.open()
    try {
      await verify(device, expectRemoval)
    } finally {
      await device.close().catch(error => checkCleanupError(error, expectRemoval))
    }
    log('PASS ' + name + (expectRemoval ? ' — device removed.' : ' — device closed.'))
  } catch (error) {
    log('FAIL ' + name + ' — ' + error.name + ': ' + error.message)
  } finally {
    for (const button of buttons) button.disabled = false
  }
}

document.querySelector('#fixture').onclick = () => run('endpoint fixture', fixture, checkEndpoints)
document.querySelector('#detach').onclick = () => run('virtual disconnect', fixture, checkEndpoints, true)
document.querySelector('#pixel').onclick = () => run('Pixel', pixel, checkPixelDescriptors)
document.querySelector('#drive').onclick = () => run('drive', drive, device => checkDeviceDescriptors(device, log))
for (let i = 0; i < 100 && !usb; i++) {
  usb = navigator.usb || installNavigatorUSB()
  if (!usb) await new Promise(resolve => setTimeout(resolve, 50))
}
if (usb) {
  for (const button of buttons) button.disabled = false
  log('Ready.')
} else {
  log('WebUSB unavailable.')
}
