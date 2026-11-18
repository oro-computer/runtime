import { installNavigatorUSB } from 'oro:usb'

const vendorId = 0x18d1
const accessoryIds = [0x2d00, 0x2d01]
const setupButton = document.querySelector('#setup')
const echoButton = document.querySelector('#echo')
const stallButton = document.querySelector('#stall')
let usb
let session

function report (message) {
  document.querySelector('#log').append(message + '\n')
  console.log('WebUSB bulk echo — ' + message)
}

function control (request, index = 0) {
  return { requestType: 'vendor', recipient: 'device', request, index, value: 0 }
}

function checkOut (result, length) {
  if (result.status !== 'ok' || result.bytesWritten !== length) {
    throw new Error(`OUT returned ${result.status}, ${result.bytesWritten} of ${length} bytes`)
  }
}

async function startAccessory () {
  const device = await usb.requestDevice({ filters: [{ vendorId }] })
  if (!/^Pixel\b/i.test(device.productName) || accessoryIds.includes(device.productId)) {
    throw new Error('Select a Pixel in its ordinary USB mode. Unplug and reconnect it to start again.')
  }
  const nonce = Array.from(crypto.getRandomValues(new Uint8Array(8)), n => n.toString(16).padStart(2, '0')).join('')
  await device.open()
  let switching = false
  try {
    const version = await device.controlTransferIn(control(51), 2)
    if (version.status !== 'ok' || version.data?.byteLength !== 2 || ![1, 2].includes(version.data.getUint16(0, true))) {
      throw new Error('The Pixel did not return a supported Android accessory protocol version.')
    }
    const strings = ['Oro Runtime', 'WebUSB Bulk Echo', 'USB bulk echo fixture', '1', '', nonce]
    for (const [index, value] of strings.entries()) {
      if (!value) continue
      const data = new TextEncoder().encode(value + '\0')
      checkOut(await device.controlTransferOut(control(52, index), data), data.length)
    }
    switching = true
    checkOut(await device.controlTransferOut(control(53), new Uint8Array(0)), 0)
  } catch (error) {
    if (!switching || error.name !== 'NotFoundError') throw error
  } finally {
    await device.close().catch(error => {
      if (!switching || error.name !== 'NotFoundError') throw error
    })
  }
  session = { nonce, serial: device.serialNumber }
  report('On the Pixel, open Oro USB Echo. When it shows “Ready for bulk echo”, click Run bulk echo and select the accessory.')
}

async function verifyEcho (testStall = false) {
  const device = await usb.requestDevice({ filters: accessoryIds.map(productId => ({ vendorId, productId })) })
  if (session.serial && device.serialNumber && device.serialNumber !== session.serial) {
    throw new Error('The accessory has a different USB serial number from the selected Pixel.')
  }
  await device.open()
  try {
    if (!device.configuration) await device.selectConfiguration(1)
    const iface = device.configuration.interfaces.find(i => i.interfaceNumber === 0)
    const alt = iface?.alternate
    if (alt?.interfaceClass !== 255 || alt.interfaceSubclass !== 255 || alt.interfaceProtocol !== 0) {
      throw new Error('Interface 0 does not match the Android accessory interface descriptor.')
    }
    const endpoints = alt.endpoints
    const input = endpoints.find(e => e.type === 'bulk' && e.direction === 'in')
    const output = endpoints.find(e => e.type === 'bulk' && e.direction === 'out')
    if (endpoints.length !== 2 || !input || !output) throw new Error('Expected one bulk endpoint in each direction.')
    await device.claimInterface(iface.interfaceNumber)
    try {
      report(`Configuration ${device.configuration.configurationValue}, interface 0, alternate ${alt.alternateSetting}, bulk OUT ${output.endpointNumber} / IN ${input.endpointNumber} (max packet ${output.packetSize} / ${input.packetSize} bytes).`)
      if (testStall) {
        const setup = { requestType: 'standard', recipient: 'endpoint', request: 0, value: 0, index: 0x80 | input.endpointNumber }
        const before = await device.controlTransferIn(setup, 2)
        if (before.status !== 'ok' || before.data?.byteLength !== 2 || !(before.data.getUint16(0, true) & 1)) {
          throw new Error('The accessory bulk IN endpoint is not halted. Prepare the stall before running this check.')
        }
        try {
          const probe = await device.transferIn(input.endpointNumber, 32)
          if (probe.status !== 'stall') throw new Error('Expected bulk IN stall, received ' + probe.status)
        } finally {
          await device.clearHalt('in', input.endpointNumber)
        }
        const after = await device.controlTransferIn(setup, 2)
        if (after.status !== 'ok' || after.data?.byteLength !== 2 || (after.data.getUint16(0, true) & 1)) {
          throw new Error('Bulk IN halt bit remained set after clearHalt.')
        }
        report('Bulk IN returned stall. clearHalt cleared the endpoint halt bit.')
      }
      const prefix = new TextEncoder().encode(session.nonce)
      for (const length of [17, 31, 257, 1025]) {
        const bytes = crypto.getRandomValues(new Uint8Array(length))
        bytes.set(prefix)
        checkOut(await device.transferOut(output.endpointNumber, bytes), length)
        const { status, data } = await device.transferIn(input.endpointNumber, length + 7)
        if (status !== 'ok' || data?.byteLength !== length || !bytes.every((byte, i) => byte === data.getUint8(i))) {
          throw new Error(`Echo mismatch for ${length} bytes (IN status ${status}, received ${data?.byteLength ?? 0}).`)
        }
        report(`Matched all ${length} bytes (requested ${length + 7} on IN).`)
      }
    } finally {
      await device.releaseInterface(iface.interfaceNumber)
    }
  } finally {
    await device.close()
  }
  report('PASS — four bulk round trips matched. Interface released and device closed.')
}

async function run (operation) {
  setupButton.disabled = true
  echoButton.disabled = true
  stallButton.disabled = true
  try {
    await operation()
  } catch (error) {
    report(`${error.name} — ${error.message}`)
    if (operation !== startAccessory && error.message?.includes('LIBUSB_ERROR_ACCESS')) {
      report('Leave the Pixel connected. Grant your Linux desktop user read/write access to the accessory-mode USB node, then click Run bulk echo again.')
    }
  } finally {
    setupButton.disabled = false
    echoButton.disabled = !session
    stallButton.disabled = !session
  }
}

setupButton.onclick = () => run(startAccessory)
echoButton.onclick = () => run(verifyEcho)
stallButton.onclick = () => run(() => verifyEcho(true))
for (let i = 0; i < 100 && !usb; i++) {
  usb = navigator.usb || installNavigatorUSB()
  if (!usb) await new Promise(resolve => setTimeout(resolve, 50))
}
if (usb) {
  setupButton.disabled = false
  report('Ready to select a Pixel.')
} else {
  report('WebUSB is unavailable in this window.')
}
