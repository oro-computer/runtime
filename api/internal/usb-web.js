/* global EventTarget, CustomEvent, DOMException */
// WebUSB renderer API
// Spec reference: https://wicg.github.io/webusb/

import ipc, { maybeMakeError } from '../ipc.js'
import { Buffer } from '../buffer.js'
import os from '../os.js'

// Only the committed application document receives this token from its WebView.
async function usbDocument () {
  let source
  try {
    source = globalThis.top || globalThis
    if (source.__oroUSBDocument) return source.__oroUSBDocument
  } catch {
    throw new DOMException('USB requires the application origin', 'SecurityError')
  }
  await new Promise((resolve, reject) => {
    const ready = () => {
      if (!source.__oroUSBDocument) return
      clearTimeout(timer)
      source.removeEventListener('oro-usb-document', ready)
      resolve()
    }
    const timer = setTimeout(() => {
      source.removeEventListener('oro-usb-document', ready)
      reject(new DOMException('USB is unavailable in this document', 'SecurityError'))
    }, 3000)
    source.addEventListener('oro-usb-document', ready)
    ready()
  })
  return source.__oroUSBDocument
}

async function requestUSB (route, params) {
  return ipc.request(route, { ...params, usbDocument: await usbDocument() })
}

async function writeUSB (route, params, data) {
  return ipc.write(route, { ...params, usbDocument: await usbDocument() }, data)
}

function usbError (value, caller) {
  const name = value.type ?? value.name
  if (['SecurityError', 'InvalidStateError', 'NotFoundError', 'NotAllowedError', 'NotSupportedError', 'NetworkError', 'DataError', 'OperationError', 'AbortError'].includes(name)) {
    return new DOMException(value.message, name)
  }
  return maybeMakeError(value, caller)
}

const TRANSFER_STATUS_OK = 'ok'

const REQUEST_TYPE_MAP = {
  standard: 0,
  class: 1,
  vendor: 2
}

const RECIPIENT_MAP = {
  device: 0,
  interface: 1,
  endpoint: 2,
  other: 3
}

const MAX_CONTROL_TRANSFER_LENGTH = 0xffff
const MAX_BULK_TRANSFER_LENGTH = 16 * 1024 * 1024

const clampByte = (value, label) => {
  const n = Number(value)
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 0 || n > 255) {
    throw new TypeError(`${label} must be an integer between 0 and 255`)
  }
  return n
}

const clampUnsignedShort = (value, label) => {
  const n = Number(value)
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 0 || n > 0xffff) {
    throw new TypeError(`${label} must be an integer between 0 and 65535`)
  }
  return n & 0xffff
}

const validateTransferLength = (
  value,
  max,
  label,
  { allowZero = false } = {}
) => {
  const n = Number(value)
  if (!Number.isFinite(n) || !Number.isInteger(n)) {
    throw new TypeError(`${label} must be an integer`)
  }
  const min = allowZero ? 0 : 1
  if (n < min || n > max) {
    throw new RangeError(`${label} must be between ${min} and ${max}`)
  }
  return n >>> 0
}

const ensurePayloadWithin = (length, max, label) => {
  if (length > max) {
    throw new RangeError(`${label} byte length must not exceed ${max}`)
  }
}

const toUSBSetup = (setup, direction) => {
  if (!setup || typeof setup !== 'object') {
    throw new TypeError('USB control transfer setup must be an object')
  }
  if (
    !('requestType' in setup) ||
    !('recipient' in setup) ||
    !('request' in setup) || !('value' in setup) || !('index' in setup)
  ) {
    throw new TypeError(
      'setup must include requestType, recipient, request, value, and index'
    )
  }
  const requestType = String(setup.requestType).toLowerCase()
  const recipient = String(setup.recipient).toLowerCase()
  if (!(requestType in REQUEST_TYPE_MAP)) {
    throw new TypeError(
      'setup.requestType must be one of "standard", "class", or "vendor"'
    )
  }
  if (!(recipient in RECIPIENT_MAP)) {
    throw new TypeError(
      'setup.recipient must be one of "device", "interface", "endpoint", or "other"'
    )
  }

  const dir = String(direction).toLowerCase() === 'in' ? 0x80 : 0x00
  const bmRequestType =
    dir | (REQUEST_TYPE_MAP[requestType] << 5) | RECIPIENT_MAP[recipient]

  const setupObject = {
    bmRequestType,
    bRequest: clampByte(setup.request, 'setup.request'),
    wValue:
      setup.value == null ? 0 : clampUnsignedShort(setup.value, 'setup.value'),
    wIndex:
      setup.index == null ? 0 : clampUnsignedShort(setup.index, 'setup.index')
  }

  const timeout = Number(setup.timeout)
  if (Number.isFinite(timeout) && timeout > 0) {
    setupObject.timeout = Math.trunc(timeout)
  }

  return setupObject
}

const bufferSourceToUint8Array = (value, label = 'data') => {
  if (value == null) return new Uint8Array(0)
  if (value instanceof Uint8Array) return value
  if (value instanceof ArrayBuffer) return new Uint8Array(value)
  if (ArrayBuffer.isView(value)) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
  }
  if (
    typeof SharedArrayBuffer === 'function' &&
    value instanceof SharedArrayBuffer
  ) {
    return new Uint8Array(value)
  }
  throw new TypeError(`${label} must be a BufferSource`)
}

const base64ToDataView = (encoded) => {
  if (typeof encoded !== 'string') return null
  if (encoded.length === 0) return new DataView(new ArrayBuffer(0))
  try {
    let bytes
    if (typeof Buffer === 'function') {
      bytes = Buffer.from(encoded, 'base64')
      return new DataView(
        bytes.buffer.slice(
          bytes.byteOffset,
          bytes.byteOffset + bytes.byteLength
        )
      )
    }
    const binary = atob(encoded)
    const buffer = new ArrayBuffer(binary.length)
    const view = new Uint8Array(buffer)
    for (let i = 0; i < binary.length; ++i) view[i] = binary.charCodeAt(i)
    return new DataView(buffer)
  } catch {
    return null
  }
}

class USBInTransferResult {
  /** @param {USBTransferStatus} status @param {DataView|null} [dataView] */
  constructor (status, dataView) {
    this.status = status
    this.data = dataView ?? null
  }
}

class USBOutTransferResult {
  /** @param {USBTransferStatus} status @param {number} [bytesWritten] */
  constructor (status, bytesWritten) {
    this.status = status
    this.bytesWritten = bytesWritten >>> 0
  }
}

class USBConnectionEvent extends Event {
  constructor (type, init) {
    super(type, init)
    if (!(init?.device instanceof USBDevice)) throw new TypeError('A USBDevice is required')
    this.device = init.device
  }
}

const normalizeFilters = (filters) => {
  if (!Array.isArray(filters)) return []
  return filters.map((filter, index) => {
    if (!filter || typeof filter !== 'object') {
      throw new TypeError(`filters[${index}] must be an object`)
    }
    const normalized = {}
    if ('vendorId' in filter) {
      const vendorId = Number(filter.vendorId)
      if (!Number.isInteger(vendorId) || vendorId < 0 || vendorId > 0xffff) {
        throw new TypeError('vendorId must be an integer between 0 and 65535')
      }
      normalized.vendorId = vendorId
    }
    if ('productId' in filter) {
      const productId = Number(filter.productId)
      if (!Number.isInteger(productId) || productId < 0 || productId > 0xffff) {
        throw new TypeError('productId must be an integer between 0 and 65535')
      }
      normalized.productId = productId
    }
    if ('classCode' in filter) {
      normalized.classCode = clampByte(filter.classCode, 'filters.classCode')
    }
    if ('subclassCode' in filter) {
      normalized.subclassCode = clampByte(
        filter.subclassCode,
        'filters.subclassCode'
      )
    }
    if ('protocolCode' in filter) {
      normalized.protocolCode = clampByte(
        filter.protocolCode,
        'filters.protocolCode'
      )
    }
    if ('serialNumber' in filter) normalized.serialNumber = String(filter.serialNumber)
    if (('productId' in normalized && !('vendorId' in normalized)) ||
        ('subclassCode' in normalized && !('classCode' in normalized)) ||
        ('protocolCode' in normalized && !('subclassCode' in normalized))) {
      throw new TypeError('Invalid USB filter dependency')
    }
    return normalized
  })
}

const normalizeRequestDeviceOptionsForIPC = (options) => {
  if (!options || (!Array.isArray(options.filters) && !options.acceptAllDevices)) {
    throw new TypeError('USB device filters are required')
  }
  if (options.exclusionFilters !== undefined && !Array.isArray(options.exclusionFilters)) {
    throw new TypeError('exclusionFilters must be an array')
  }
  return {
    acceptAllDevices: !!options.acceptAllDevices,
    filters: normalizeFilters(options.filters),
    exclusionFilters: normalizeFilters(options.exclusionFilters)
  }
}

/** Select granted devices for a USB permission descriptor.
 * @param {USBDevice[]} devices
 * @param {USBDeviceRequestOptions} options
 * @returns {USBDevice[]}
 */
export function filterUSBDevices (devices, options) {
  const { filters, exclusionFilters } = normalizeRequestDeviceOptionsForIPC({ filters: [], ...options })
  const matches = (device, filter) => {
    if (filter.vendorId !== undefined && filter.vendorId !== device.vendorId) return false
    if (filter.productId !== undefined && filter.productId !== device.productId) return false
    if (filter.serialNumber !== undefined && filter.serialNumber !== device.serialNumber) return false
    if (filter.classCode === undefined) return true
    const matchesClass = (cls, sub, protocol) => cls === filter.classCode &&
      (filter.subclassCode === undefined || filter.subclassCode === sub) &&
      (filter.protocolCode === undefined || filter.protocolCode === protocol)
    return matchesClass(device.deviceClass, device.deviceSubclass, device.deviceProtocol) ||
      device.configurations.some(config => config.interfaces.some(iface => iface.alternates.some(alt =>
        matchesClass(alt.interfaceClass, alt.interfaceSubclass, alt.interfaceProtocol))))
  }
  return devices.filter(device => (!filters.length || filters.some(f => matches(device, f))) &&
    !exclusionFilters.some(f => matches(device, f)))
}

function usbPolicyAllowsDocument () {
  const policy = globalThis.document?.permissionsPolicy ?? globalThis.document?.featurePolicy
  return (policy?.features && !policy.features().includes('usb')) || policy?.allowsFeature?.('usb') !== false
}

/** @typedef {{vendorId?: number, productId?: number, classCode?: number, subclassCode?: number, protocolCode?: number, serialNumber?: string}} USBDeviceFilter */
/** @typedef {{filters?: USBDeviceFilter[], exclusionFilters?: USBDeviceFilter[], acceptAllDevices?: boolean}} USBDeviceRequestOptions */
/** @typedef {{requestType: 'standard'|'class'|'vendor', recipient: 'device'|'interface'|'endpoint'|'other', request: number, value: number, index: number}} USBControlTransferParameters */

/** @typedef {{endpointNumber: number, direction: 'in'|'out', type: 'bulk'|'interrupt'|'isochronous', packetSize: number}} USBEndpointDescriptor */
/** @typedef {{configurationValue: number, interfaceNumber: number, alternateSetting: number, classCode: number, subclassCode?: number, protocolCode?: number, interfaceName?: string, endpoints?: USBEndpointDescriptor[]}} USBAlternateDescriptor */
/** @typedef {{deviceId: string, vendorId?: number, productId?: number, classCode?: number, subclassCode?: number, protocolCode?: number, productName?: string, manufacturerName?: string, serialNumber?: string, opened?: boolean, claimedInterfaces?: {interfaceNumber: number, alternateSetting: number}[], authorized?: boolean, usbVersion?: number, deviceVersion?: number, configurationValue?: number, configurations?: number[], configurationNames?: Record<string, string>, interfaces?: USBAlternateDescriptor[]}} USBDeviceDescriptor */
/** @typedef {'ok'|'stall'|'babble'} USBTransferStatus */

/** Describes an endpoint on an alternate interface. */
class USBEndpoint {
  /** @param {USBAlternateInterface} alternate @param {number} endpointNumber @param {'in'|'out'} direction */
  constructor (alternate, endpointNumber, direction) {
    const record = alternate._endpoints.find(ep => ep.endpointNumber === endpointNumber && ep.direction === direction)
    if (!record) throw new RangeError('Unknown USB endpoint')
    this.endpointNumber = record.endpointNumber
    this.direction = record.direction
    this.type = record.type
    this.packetSize = record.packetSize
    Object.freeze(this)
  }
}

/** Describes the endpoints and class of one interface setting. */
class USBAlternateInterface {
  /** @param {USBInterface} iface @param {number} alternateSetting */
  constructor (iface, alternateSetting) {
    const record = iface._records.find(item => item.alternateSetting === alternateSetting)
    if (!record) throw new RangeError('Unknown USB alternate setting')
    this.alternateSetting = alternateSetting
    this.interfaceClass = record.classCode
    this.interfaceSubclass = record.subclassCode
    this.interfaceProtocol = record.protocolCode
    this.interfaceName = record.interfaceName || null
    this._endpoints = record.endpoints || []
    this.endpoints = Object.freeze(this._endpoints.map(ep => new USBEndpoint(this, ep.endpointNumber, ep.direction)))
    Object.freeze(this)
  }
}

/** Describes a claimable USB interface and its alternate settings. */
class USBInterface {
  /** @param {USBConfiguration} configuration @param {number} interfaceNumber */
  constructor (configuration, interfaceNumber) {
    this._device = configuration._device
    this._configurationValue = configuration.configurationValue
    this._records = configuration._records.filter(item => item.interfaceNumber === interfaceNumber)
    if (!this._records.length) throw new RangeError('Unknown USB interface')
    this.interfaceNumber = interfaceNumber
    this.alternates = Object.freeze(this._records.map(item => new USBAlternateInterface(this, item.alternateSetting)))
    Object.freeze(this)
  }

  /** @returns {boolean} */
  get claimed () {
    return this._device.configuration?.configurationValue === this._configurationValue &&
      this._device._claims.has(this.interfaceNumber)
  }

  /** @returns {USBAlternateInterface} */
  get alternate () {
    const setting = this.claimed ? this._device._claims.get(this.interfaceNumber) : 0
    return this.alternates.find(alt => alt.alternateSetting === setting) ?? this.alternates[0]
  }
}

/** Describes one selectable USB configuration. */
class USBConfiguration {
  /** @param {USBDevice} device @param {number} configurationValue */
  constructor (device, configurationValue) {
    if (!device._values.includes(configurationValue)) throw new RangeError('Unknown USB configuration')
    this._device = device
    this.configurationValue = configurationValue
    this.configurationName = device._configurationNames[configurationValue] || null
    this._records = device._interfaces.filter(item => item.configurationValue === configurationValue)
    const numbers = new Set(this._records.map(item => item.interfaceNumber))
    this.interfaces = Object.freeze(Array.from(numbers, number => new USBInterface(this, number)))
    Object.freeze(this)
  }
}

class USBIsochronousInTransferPacket extends USBInTransferResult {}
class USBIsochronousOutTransferPacket extends USBOutTransferResult {}
class USBIsochronousInTransferResult {
  /** @param {USBIsochronousInTransferPacket[]} packets @param {DataView|null} [data] */
  constructor (packets, data = null) {
    this.packets = Object.freeze(packets)
    this.data = data
  }
}
class USBIsochronousOutTransferResult {
  /** @param {USBIsochronousOutTransferPacket[]} packets */
  constructor (packets) { this.packets = Object.freeze(packets) }
}

const packetSizes = (lengths) => {
  const packets = Array.from(lengths, value => validateTransferLength(value, MAX_BULK_TRANSFER_LENGTH, 'packet length', { allowZero: true }))
  if (!packets.length || packets.length > 1024) throw new RangeError('Expected 1 to 1024 USB packets')
  ensurePayloadWithin(packets.reduce((sum, length) => sum + length, 0), MAX_BULK_TRANSFER_LENGTH, 'packets')
  return packets
}

const disconnectedDevices = new WeakSet()

class USBDevice extends EventTarget {
  /** @param {USBDeviceDescriptor} descriptor */
  constructor (descriptor) {
    super()
    this._applyDescriptor(descriptor)
  }

  /** @param {USBDeviceDescriptor} descriptor */
  _applyDescriptor (descriptor) {
    this.deviceId = String(descriptor.deviceId ?? '')
    this.vendorId = descriptor.vendorId >>> 0
    this.productId = descriptor.productId >>> 0
    this.deviceClass = descriptor.classCode >>> 0
    this.deviceSubclass = descriptor.subclassCode >>> 0
    this.deviceProtocol = descriptor.protocolCode >>> 0
    this.productName = descriptor.productName || null
    this.manufacturerName = descriptor.manufacturerName || null
    this.serialNumber = descriptor.serialNumber || null
    this._authorized = !!descriptor.authorized
    /** @type {Map<number, number>} */
    this._claims ??= new Map()
    if (descriptor.opened === false || descriptor.claimedInterfaces) {
      this._claims.clear()
      for (const claim of descriptor.claimedInterfaces ?? []) {
        this._claims.set(claim.interfaceNumber, claim.alternateSetting)
      }
    }
    /** @type {boolean} */
    this.opened = descriptor.opened ?? this.opened ?? false
    const major = version => ((version >>> 12) & 15) * 10 + ((version >>> 8) & 15)
    if (descriptor.usbVersion || this.usbVersionMajor === undefined) {
      this.usbVersionMajor = major(descriptor.usbVersion)
      this.usbVersionMinor = (descriptor.usbVersion >>> 4) & 15
      this.usbVersionSubminor = descriptor.usbVersion & 15
    }
    this.deviceVersionMajor = major(descriptor.deviceVersion)
    this.deviceVersionMinor = (descriptor.deviceVersion >>> 4) & 15
    this.deviceVersionSubminor = descriptor.deviceVersion & 15
    this._configurationNames = descriptor.configurationNames ?? {}
    this._values = descriptor.configurations ?? []
    this._interfaces = descriptor.interfaces ?? []
    // Keep descriptor object identity while a connected device is enumerated again.
    if (!this.configurations?.length) {
      this.configurations = Object.freeze(this._values.map(value => new USBConfiguration(this, value)))
    }
    if (!this.opened || descriptor.opened !== undefined || this._configurationValue === undefined) {
      this._configurationValue = descriptor.configurationValue ?? 0
    }
  }

  /** @returns {USBConfiguration|null} */
  get configuration () {
    return this.configurations.find(config => config.configurationValue === this._configurationValue) ?? null
  }

  get #connectedId () {
    if (disconnectedDevices.has(this)) throw new DOMException('USB device disconnected', 'NotFoundError')
    return this.deviceId
  }

  async open () {
    if (this.opened) return
    const res = await requestUSB('usb.device.open', {
      deviceId: this.#connectedId
    })
    if (res?.err) throw usbError(res.err, USBDevice.prototype.open)
    this._configurationValue = res.data?.configurationValue ?? this._configurationValue
    if (res.data?.usbVersion) {
      const version = res.data.usbVersion
      this.usbVersionMajor = (version >>> 12) * 10 + ((version >>> 8) & 15)
      this.usbVersionMinor = (version >>> 4) & 15
      this.usbVersionSubminor = version & 15
    }
    this.opened = true
  }

  async close () {
    if (!this.opened) return
    const res = await requestUSB('usb.device.close', {
      deviceId: this.#connectedId
    })
    if (res?.err) throw usbError(res.err, USBDevice.prototype.close)
    this._claims.clear()
    this.opened = false
  }

  async forget () {
    const res = await requestUSB('usb.forgetDevice', {
      deviceId: this.deviceId
    })
    if (res?.err) throw usbError(res.err, USBDevice.prototype.forget)
    this._authorized = false
    this._claims.clear()
    this.opened = false
  }

  /**
   * @param {number} configurationValue
   */
  async selectConfiguration (configurationValue) {
    configurationValue = clampByte(configurationValue, 'configurationValue')
    const res = await requestUSB('usb.device.selectConfiguration', {
      deviceId: this.#connectedId,
      configurationValue
    })
    if (res?.err) {
      for (const number of res.err.releasedInterfaces ?? []) this._claims.delete(number)
      throw usbError(res.err, USBDevice.prototype.selectConfiguration)
    }
    if (this._configurationValue !== configurationValue) this._claims.clear()
    this._configurationValue = configurationValue
  }

  /**
   * @param {number} interfaceNumber
   */
  async claimInterface (interfaceNumber) {
    interfaceNumber = clampByte(interfaceNumber, 'interfaceNumber')
    const res = await requestUSB('usb.device.claimInterface', {
      deviceId: this.#connectedId,
      interfaceNumber
    })
    if (res?.err) {
      throw usbError(res.err, USBDevice.prototype.claimInterface)
    }
    if (!this._claims.has(interfaceNumber)) this._claims.set(interfaceNumber, 0)
  }

  /**
   * @param {number} interfaceNumber
   */
  async releaseInterface (interfaceNumber) {
    interfaceNumber = clampByte(interfaceNumber, 'interfaceNumber')
    const res = await requestUSB('usb.device.releaseInterface', {
      deviceId: this.#connectedId,
      interfaceNumber
    })
    if (res?.err) {
      throw usbError(res.err, USBDevice.prototype.releaseInterface)
    }
    this._claims.delete(interfaceNumber)
  }

  /**
   * @param {number} interfaceNumber
   * @param {number} alternateSetting
   */
  async selectAlternateInterface (interfaceNumber, alternateSetting) {
    interfaceNumber = clampByte(interfaceNumber, 'interfaceNumber')
    alternateSetting = clampByte(alternateSetting, 'alternateSetting')
    const res = await requestUSB('usb.device.selectAlternateInterface', {
      deviceId: this.#connectedId,
      interfaceNumber,
      alternateSetting
    })
    if (res?.err) {
      throw usbError(
        res.err,
        USBDevice.prototype.selectAlternateInterface
      )
    }
    this._claims.set(interfaceNumber, alternateSetting)
  }

  /**
   * @param {USBControlTransferParameters} setup
   * @param {number} length
   */
  async controlTransferIn (setup, length) {
    const normalizedSetup = toUSBSetup(setup, 'in')
    const requestLength = validateTransferLength(
      length,
      MAX_CONTROL_TRANSFER_LENGTH,
      'length',
      { allowZero: true }
    )
    const res = await requestUSB('usb.transfer.controlIn', {
      deviceId: this.#connectedId,
      setup: JSON.stringify(normalizedSetup),
      length: requestLength
    })
    if (res?.err) {
      throw usbError(res.err, USBDevice.prototype.controlTransferIn)
    }
    const payload = base64ToDataView(res?.data?.data)
    return new USBInTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      payload
    )
  }

  /**
   * @param {USBControlTransferParameters} setup
   * @param {BufferSource} [data]
   */
  async controlTransferOut (setup, data) {
    const normalizedSetup = toUSBSetup(setup, 'out')
    const payload = bufferSourceToUint8Array(data, 'data')
    ensurePayloadWithin(payload.length, MAX_CONTROL_TRANSFER_LENGTH, 'data')
    const res = await writeUSB(
      'usb.transfer.controlOut',
      {
        deviceId: this.#connectedId,
        setup: JSON.stringify(normalizedSetup)
      },
      payload
    )
    if (res?.err) {
      throw usbError(res.err, USBDevice.prototype.controlTransferOut)
    }
    return new USBOutTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      res?.data?.transferred ?? payload.length
    )
  }

  /**
   * @param {number} endpointNumber
   * @param {number} length
   */
  async transferIn (endpointNumber, length) {
    const requestLength = validateTransferLength(
      length,
      MAX_BULK_TRANSFER_LENGTH,
      'length',
      { allowZero: true }
    )
    const res = await requestUSB('usb.transfer.in', {
      deviceId: this.#connectedId,
      endpointNumber: Number(endpointNumber) || 0,
      length: requestLength
    })
    if (res?.err) throw usbError(res.err, USBDevice.prototype.transferIn)
    const payload = base64ToDataView(res?.data?.data)
    return new USBInTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      payload
    )
  }

  /**
   * @param {number} endpointNumber
   * @param {BufferSource} data
   */
  async transferOut (endpointNumber, data) {
    const payload = bufferSourceToUint8Array(data, 'data')
    ensurePayloadWithin(payload.length, MAX_BULK_TRANSFER_LENGTH, 'data')
    const res = await writeUSB(
      'usb.transfer.out',
      {
        deviceId: this.#connectedId,
        endpointNumber: Number(endpointNumber) || 0
      },
      payload
    )
    if (res?.err) throw usbError(res.err, USBDevice.prototype.transferOut)
    return new USBOutTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      res?.data?.transferred ?? payload.length
    )
  }

  /** @param {number} endpointNumber @param {number[]} packetLengths */
  async isochronousTransferIn (endpointNumber, packetLengths) {
    const lengths = packetSizes(packetLengths)
    const res = await requestUSB('usb.transfer.isochronousIn', {
      deviceId: this.#connectedId, endpointNumber, packetLengths: JSON.stringify(lengths)
    })
    if (res.err) throw usbError(res.err)
    const data = base64ToDataView(res.data.data)
    let offset = 0
    const packets = res.data.packets.map((packet, index) => {
      const view = packet.status === 'ok' ? new DataView(data.buffer, data.byteOffset + offset, packet.transferred) : null
      offset += lengths[index]
      return new USBIsochronousInTransferPacket(packet.status, view)
    })
    return new USBIsochronousInTransferResult(packets, data)
  }

  /** @param {number} endpointNumber @param {BufferSource} data @param {number[]} packetLengths */
  async isochronousTransferOut (endpointNumber, data, packetLengths) {
    const lengths = packetSizes(packetLengths)
    const payload = bufferSourceToUint8Array(data)
    if (payload.length !== lengths.reduce((sum, length) => sum + length, 0)) throw new RangeError('Packet lengths must match the payload')
    const res = await writeUSB('usb.transfer.isochronousOut', {
      deviceId: this.#connectedId, endpointNumber, packetLengths: JSON.stringify(lengths)
    }, payload)
    if (res.err) throw usbError(res.err)
    return new USBIsochronousOutTransferResult(res.data.packets.map(packet => new USBIsochronousOutTransferPacket(packet.status, packet.transferred)))
  }

  /**
   * @param {'in'|'out'} direction
   * @param {number} endpointNumber
   */
  async clearHalt (direction, endpointNumber) {
    const dir = String(direction).toLowerCase()
    if (dir !== 'in' && dir !== 'out') {
      throw new TypeError("direction must be either 'in' or 'out'")
    }
    const res = await requestUSB('usb.transfer.clearHalt', {
      deviceId: this.#connectedId,
      direction: dir,
      endpointNumber: Number(endpointNumber) || 0
    })
    if (res?.err) throw usbError(res.err, USBDevice.prototype.clearHalt)
  }

  async reset () {
    const res = await requestUSB('usb.device.reset', {
      deviceId: this.#connectedId
    })
    if (res?.err) throw usbError(res.err, USBDevice.prototype.reset)
  }
}

class NavigatorUSB extends EventTarget {
  constructor () {
    super()
    /** @type {Map<string, USBDevice>} */
    this._deviceCache = new Map()
    this._onNativeConnect = (event) => {
      try {
        const descriptor = event?.detail?.device
        if (!descriptor) return
        const device = this._createDevice(descriptor)
        if (!device) return
        this.dispatchEvent(new USBConnectionEvent('connect', { device }))
      } catch (err) {
        console.error(err)
      }
    }

    this._onNativeDisconnect = (event) => {
      try {
        const descriptor = event?.detail?.device
        const deviceId = descriptor?.deviceId ?? event?.detail?.deviceId
        if (!deviceId) return
        let device = this._deviceCache.get(String(deviceId))
        if (descriptor) {
          device = this._createDevice(descriptor)
        } else if (!device) {
          device = this._createDevice({ deviceId: String(deviceId) })
        }
        if (!device) return
        disconnectedDevices.add(device)
        device._claims.clear()
        device.opened = false
        this._deviceCache.delete(String(deviceId))
        this.dispatchEvent(new USBConnectionEvent('disconnect', { device }))
      } catch (err) {
        console.error(err)
      }
    }

    if (typeof globalThis.addEventListener === 'function') {
      globalThis.addEventListener('usb.deviceconnect', this._onNativeConnect)
      globalThis.addEventListener(
        'usb.devicedisconnect',
        this._onNativeDisconnect
      )
    }
  }

  addEventListener (type, listener, options) {
    super.addEventListener(type, listener, options)
    if ((type === 'connect' || type === 'disconnect') && !this._watching) {
      this._watching = true
      requestUSB('usb.watch').then(result => {
        if (result.err) this._watching = false
      }, () => { this._watching = false })
    }
  }

  get onconnect () { return this._connectHandler ?? null }
  set onconnect (handler) {
    if (this._connectHandler) this.removeEventListener('connect', this._connectHandler)
    this._connectHandler = typeof handler === 'function' ? handler : null
    if (this._connectHandler) this.addEventListener('connect', this._connectHandler)
  }

  get ondisconnect () { return this._disconnectHandler ?? null }
  set ondisconnect (handler) {
    if (this._disconnectHandler) this.removeEventListener('disconnect', this._disconnectHandler)
    this._disconnectHandler = typeof handler === 'function' ? handler : null
    if (this._disconnectHandler) this.addEventListener('disconnect', this._disconnectHandler)
  }

  /** @param {USBDeviceDescriptor} descriptor @returns {USBDevice|null} */
  _createDevice (descriptor) {
    if (!descriptor || typeof descriptor !== 'object') return null
    const id = String(descriptor.deviceId ?? '')
    let device = this._deviceCache.get(id)
    if (device) {
      device._applyDescriptor(descriptor)
      return device
    }
    device = new USBDevice(descriptor)
    this._deviceCache.set(id, device)
    return device
  }

  /** @returns {Promise<USBDevice[]>} */
  async getDevices () {
    const res = await requestUSB('usb.getDevices', {})
    if (res?.err) {
      throw usbError(res.err, NavigatorUSB.prototype.getDevices)
    }
    const list = Array.isArray(res?.data?.devices) ? res.data.devices : []
    return list
      .map((descriptor) => this._createDevice(descriptor))
      .filter(Boolean)
  }

  /** @param {USBDeviceRequestOptions} options @returns {Promise<USBDevice>} */
  async requestDevice (options = {}) {
    const normalized = normalizeRequestDeviceOptionsForIPC(options)
    if (!globalThis.document || !hasUSBActivation() ||
        !usbPolicyAllowsDocument()) {
      throw new DOMException('USB device selection requires an active user gesture in an allowed window', 'SecurityError')
    }
    const serializedOptions = JSON.stringify(normalized)
    const res = await requestUSB('usb.requestDevice', {
      options: serializedOptions
    })
    if (res?.err) {
      throw usbError(res.err, NavigatorUSB.prototype.requestDevice)
    }

    if (res?.data?.device) {
      return this._createDevice(res.data.device)
    }

    if (res?.data?.requiresSelection) {
      const devices = Array.isArray(res.data.devices)
        ? res.data.devices.map((d) => this._createDevice(d))
        : []
      if (!devices.length) {
        throw new DOMException('No USB devices available', 'NotFoundError')
      }
      return await this.#presentChooser(devices)
    }

    throw new DOMException(
      'Unexpected response from usb.requestDevice',
      'InvalidStateError'
    )
  }

  async cancelRequest () {
    const res = await requestUSB('usb.cancelRequest')
    if (res?.err) {
      throw usbError(res.err, NavigatorUSB.prototype.cancelRequest)
    }
  }

  async #presentChooser (devices) {
    const detail = this.#createChooserDetail(devices)
    const event = new CustomEvent('usb.chooserequest', {
      detail,
      cancelable: true
    })
    globalThis.dispatchEvent(event)

    if (!detail.handled && !event.defaultPrevented) {
      if (typeof document !== 'undefined') {
        return await presentDefaultChooser(detail)
      }
      await detail.cancel()
    }

    return await detail.result
  }

  #createChooserDetail (devices) {
    const validDevices = devices.filter(Boolean)
    let resolved = false
    let resolvePromise, rejectPromise
    const result = new Promise((resolve, reject) => {
      resolvePromise = resolve
      rejectPromise = reject
    })

    const ensurePending = () => {
      if (resolved) {
        throw new DOMException(
          'USB device chooser already resolved',
          'InvalidStateError'
        )
      }
    }

    const detail = {
      devices: validDevices,
      handled: false,
      result,
      markHandled () {
        this.handled = true
      }
    }

    detail.select = async (device) => {
      ensurePending()
      const match = validDevices.find((candidate) => candidate === device)
      if (!match) {
        throw new TypeError(
          'Selected device must be one of the provided candidates'
        )
      }
      resolved = true
      detail.handled = true
      try {
        const res = await requestUSB('usb.chooseDevice', {
          deviceId: match.deviceId
        })
        if (res?.err) {
          throw usbError(res.err, NavigatorUSB.prototype.requestDevice)
        }
        const descriptor = res?.data?.device
        if (!descriptor) {
          throw new DOMException(
            'Device selection returned no device',
            'InvalidStateError'
          )
        }
        resolved = true
        detail.handled = true
        resolvePromise(this._createDevice(descriptor))
      } catch (err) {
        resolved = true
        detail.handled = true
        rejectPromise(err)
      }
    }

    detail.cancel = async () => {
      ensurePending()
      resolved = true
      detail.handled = true
      try {
        const res = await requestUSB('usb.cancelRequest')
        if (res?.err) {
          throw usbError(res.err, NavigatorUSB.prototype.requestDevice)
        }
        resolved = true
        detail.handled = true
        rejectPromise(
          new DOMException('No USB device selected', 'NotFoundError')
        )
      } catch (err) {
        resolved = true
        detail.handled = true
        rejectPromise(err)
      }
    }

    return detail
  }
}

const presentDefaultChooser = async (detail) => {
  detail.markHandled?.()
  if (typeof document === 'undefined') {
    await detail.cancel()
    return detail.result
  }
  const overlay = ensureChooserOverlay()
  if (!overlay) {
    await detail.cancel()
    return detail.result
  }
  overlay.render(
    detail.devices,
    async (deviceId) => {
      const device = detail.devices.find((d) => d.deviceId === deviceId)
      if (!device) return
      await detail.select(device)
      overlay.close()
    },
    async () => {
      overlay.close()
      await detail.cancel()
    }
  )
  return detail.result
}

let chooserOverlay = null

const ensureChooserOverlay = () => {
  if (chooserOverlay) return chooserOverlay
  if (typeof document === 'undefined') return null

  const overlay = document.createElement('div')
  overlay.id = 'socket-webusb-chooser'
  overlay.style.cssText =
    'position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;justify-content:center;font-family:sans-serif;background:rgba(0,0,0,0.45)'

  const dialog = document.createElement('div')
  dialog.style.cssText =
    'min-width:320px;max-width:480px;width:90%;border-radius:12px;box-shadow:0 12px 40px rgba(0,0,0,0.25);overflow:hidden;backdrop-filter:blur(12px)'

  const header = document.createElement('div')
  header.style.cssText =
    'padding:16px 20px;font-size:16px;font-weight:600;border-bottom:1px solid rgba(0,0,0,0.05)'
  header.textContent = 'Select a USB device'

  const list = document.createElement('div')
  list.style.cssText =
    'max-height:320px;overflow:auto;display:flex;flex-direction:column;background:transparent'

  const footer = document.createElement('div')
  footer.style.cssText =
    'display:flex;gap:12px;justify-content:flex-end;padding:12px 16px;border-top:1px solid rgba(0,0,0,0.05);background:transparent'

  const cancelButton = document.createElement('button')
  cancelButton.type = 'button'
  cancelButton.textContent = 'Cancel'
  cancelButton.style.cssText =
    'padding:8px 16px;font-size:14px;border-radius:8px;border:1px solid transparent;background:#ffffff;color:#222;cursor:pointer;box-shadow:0 1px 3px rgba(0,0,0,0.15)'

  footer.append(cancelButton)
  dialog.append(header, list, footer)
  overlay.append(dialog)

  const applyTheme = () => {
    const dark =
      globalThis.matchMedia &&
      globalThis.matchMedia('(prefers-color-scheme: dark)').matches
    if (dark) {
      overlay.style.background = 'rgba(0,0,0,0.6)'
      dialog.style.background = 'rgba(32,32,34,0.9)'
      dialog.style.color = '#f3f3f4'
      header.style.borderBottom = '1px solid rgba(255,255,255,0.08)'
      footer.style.borderTop = '1px solid rgba(255,255,255,0.08)'
      cancelButton.style.background = 'rgba(255,255,255,0.08)'
      cancelButton.style.color = '#f3f3f4'
      cancelButton.style.border = '1px solid rgba(255,255,255,0.12)'
      list.style.background = 'transparent'
    } else {
      overlay.style.background = 'rgba(0,0,0,0.45)'
      dialog.style.background = '#ffffff'
      dialog.style.color = '#111111'
      header.style.borderBottom = '1px solid rgba(0,0,0,0.05)'
      footer.style.borderTop = '1px solid rgba(0,0,0,0.05)'
      cancelButton.style.background = '#ffffff'
      cancelButton.style.color = '#222222'
      cancelButton.style.border = '1px solid rgba(0,0,0,0.12)'
      list.style.background = 'transparent'
    }
  }

  applyTheme()
  if (globalThis.matchMedia) {
    const watcher = globalThis.matchMedia('(prefers-color-scheme: dark)')
    const update = () => applyTheme()
    try {
      watcher.addEventListener('change', update)
    } catch {
      watcher.addListener(update)
    }
    overlay.addEventListener(
      'remove',
      () => {
        try {
          watcher.removeEventListener('change', update)
        } catch {
          watcher.removeListener(update)
        }
      },
      { once: true }
    )
  }

  overlay.close = () => {
    overlay.remove()
    chooserOverlay = null
  }

  overlay.render = (devices, onSelect, onCancel) => {
    list.replaceChildren()
    if (!Array.isArray(devices) || devices.length === 0) {
      const empty = document.createElement('div')
      empty.textContent = 'No devices found'
      empty.style.cssText = 'padding:20px;text-align:center;color:#666'
      list.append(empty)
    } else {
      devices.forEach((device) => {
        const item = document.createElement('button')
        item.type = 'button'
        item.style.cssText =
          'display:flex;flex-direction:column;align-items:flex-start;gap:4px;padding:12px 16px;border:none;border-bottom:1px solid rgba(0,0,0,0.04);cursor:pointer;text-align:left;background:transparent;color:inherit'
        item.onmouseenter = () => {
          item.style.background = 'rgba(0,0,0,0.06)'
        }
        item.onmouseleave = () => {
          item.style.background = 'transparent'
        }
        const title = document.createElement('div')
        title.style.cssText = 'font-weight:600;font-size:14px;'
        title.textContent =
          device.productName ||
          `Device ${device.vendorId?.toString(16).padStart(4, '0')}:${device.productId?.toString(16).padStart(4, '0')}`
        const meta = document.createElement('div')
        meta.style.cssText = 'font-size:12px;color:inherit;opacity:0.66'
        meta.textContent = `VID 0x${device.vendorId.toString(16).padStart(4, '0')}  PID 0x${device.productId.toString(16).padStart(4, '0')}`
        item.append(title, meta)
        item.onclick = () => onSelect(device.deviceId)
        list.append(item)
      })
    }
    cancelButton.onclick = () => onCancel()
    document.body.append(overlay)
  }

  chooserOverlay = overlay
  return chooserOverlay
}

let navigatorUSBInstance = null
let lastActivation = -Infinity

function recordUSBActivation (event) {
  if (event.isTrusted && (event.type !== 'keydown' || event.key !== 'Escape')) {
    lastActivation = performance.now()
  }
}

function hasUSBActivation () {
  return navigator.userActivation?.isActive ?? (performance.now() - lastActivation < 5000)
}

/** Install the WebUSB API for this supported execution context.
 * @returns {NavigatorUSB|undefined}
 */
export function installNavigatorUSB () {
  if (typeof navigator === 'undefined' || globalThis.isSharedWorkerScope || os.platform() === 'ios') return
  if (globalThis.isSecureContext === false &&
      !(globalThis.location?.protocol === 'oro:' && globalThis.__oroUSBDocument)) return
  if (!usbPolicyAllowsDocument()) return
  if (navigatorUSBInstance) return navigatorUSBInstance

  if (globalThis.document && !navigator.userActivation) {
    for (const event of ['click', 'keydown']) document.addEventListener(event, recordUSBActivation, true)
  }
  navigatorUSBInstance = new NavigatorUSB()
  if (typeof document === 'undefined') Object.defineProperty(navigatorUSBInstance, 'requestDevice', { value: undefined })
  const constructors = { USB: NavigatorUSB, USBDevice, USBConfiguration, USBInterface, USBAlternateInterface, USBEndpoint, USBConnectionEvent, USBInTransferResult, USBOutTransferResult, USBIsochronousInTransferResult, USBIsochronousOutTransferResult, USBIsochronousInTransferPacket, USBIsochronousOutTransferPacket }
  for (const [name, constructor] of Object.entries(constructors)) {
    if (!globalThis[name]) Object.defineProperty(globalThis, name, { configurable: true, value: constructor })
  }
  try {
    Object.defineProperty(navigator, 'usb', {
      configurable: true,
      enumerable: true,
      value: navigatorUSBInstance
    })
  } catch {
    navigatorUSBInstance = navigator.usb
  }

  return navigatorUSBInstance
}

try {
  installNavigatorUSB()
  if (!navigatorUSBInstance && globalThis.location?.protocol === 'oro:' && os.platform() !== 'ios') {
    globalThis.addEventListener('oro-usb-document', installNavigatorUSB, { once: true })
  }
} catch {}

export {
  NavigatorUSB,
  USBDevice,
  USBConfiguration,
  USBInterface,
  USBAlternateInterface,
  USBEndpoint,
  USBIsochronousInTransferResult,
  USBIsochronousOutTransferResult,
  USBIsochronousInTransferPacket,
  USBIsochronousOutTransferPacket,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
}
