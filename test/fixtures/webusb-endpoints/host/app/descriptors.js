function bytesOf (data) {
  return new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
}

/**
 * @param {DataView|null} data
 * @param {Uint8Array} expected
 * @returns {boolean}
 */
export function sameBytes (data, expected) {
  return data?.byteLength === expected.length && expected.every((byte, i) => byte === data.getUint8(i))
}

function getDescriptor (device, type, index, length) {
  return device.controlTransferIn({ requestType: 'standard', recipient: 'device', request: 6, value: type * 256 + index, index: 0 }, length)
}

async function readDescriptor (device, iteration, baseline) {
  let result
  try {
    result = await getDescriptor(device, 1, 0, 18)
  } catch (error) {
    throw new Error(`Descriptor read ${iteration}: ${error.name} — ${error.message}`)
  }
  const { status, data } = result
  if (status === 'ok' && data?.byteLength === 18 && data.getUint8(0) === 18 && data.getUint8(1) === 1 &&
      data.getUint16(8, true) === device.vendorId && data.getUint16(10, true) === device.productId &&
      (!baseline || sameBytes(data, baseline))) return bytesOf(data)
  const hex = data ? Array.from(bytesOf(data), b => b.toString(16).padStart(2, '0')).join(' ') : 'no data'
  throw new Error(`Descriptor read ${iteration}: ${status}, ${data?.byteLength ?? 0} bytes [${hex}]`)
}

/**
 * Check a selected, open device and report the response to an absent descriptor.
 * @param {USBDevice} device
 * @param {(message: string) => void} log
 * @returns {Promise<void>}
 */
export async function checkDeviceDescriptors (device, log) {
  const baseline = await readDescriptor(device, 1)
  for (let i = 2; i <= 256; i++) await readDescriptor(device, i, baseline)
  log('256 consecutive device descriptors matched byte for byte.')
  // Configuration descriptor indices are zero-based, so bNumConfigurations is out of range.
  let outcome
  try {
    const invalid = await getDescriptor(device, 2, baseline[17], 9)
    outcome = invalid.status
  } catch (error) {
    if (error.name !== 'NetworkError' || !error.message.includes('LIBUSB_ERROR_TIMEOUT')) throw error
    outcome = 'timeout'
  }
  log(`Absent configuration descriptor returned ${outcome}.`)
  await readDescriptor(device, 'after absent descriptor', baseline)
  log('The next valid device descriptor matched the baseline.')
  if (!['stall', 'timeout'].includes(outcome)) throw new Error('Unexpected absent-descriptor response: ' + outcome)
}
