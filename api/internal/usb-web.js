/* global EventTarget, CustomEvent, DOMException */
// WebUSB scaffold
// Spec reference: https://wicg.github.io/webusb/

import ipc, { maybeMakeError, write as ipcWrite } from '../ipc.js'
import { Buffer } from '../buffer.js'

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
const MAX_BULK_TRANSFER_LENGTH = 0x7fffffff

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
    !('request' in setup)
  ) {
    throw new TypeError(
      'setup must include requestType, recipient, and request'
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
  if (typeof encoded !== 'string' || encoded.length === 0) return null
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
  constructor (status, dataView) {
    this.status = status
    this.data = dataView ?? null
  }
}

class USBOutTransferResult {
  constructor (status, bytesWritten) {
    this.status = status
    this.bytesWritten = bytesWritten >>> 0
  }
}

class USBConnectionEvent extends Event {
  constructor (type, init) {
    super(type)
    this.device = init?.device ?? null
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
    if (!Object.keys(normalized).length) {
      throw new TypeError(
        'Each filter must specify vendorId/productId or class code constraints'
      )
    }
    return normalized
  })
}

const normalizeRequestDeviceOptionsForIPC = (options) => {
  const normalized = { acceptAllDevices: !!options.acceptAllDevices }
  if (!normalized.acceptAllDevices) {
    if (!Array.isArray(options.filters) || options.filters.length === 0) {
      throw new TypeError(
        'Either filters must be non-empty or acceptAllDevices must be true'
      )
    }
  }
  normalized.filters = normalizeFilters(options.filters)
  return normalized
}

class USBDevice extends EventTarget {
  constructor (descriptor) {
    super()
    this._applyDescriptor(descriptor)
  }

  _applyDescriptor (descriptor = {}) {
    this.deviceId = String(descriptor.deviceId ?? '')
    this.vendorId = descriptor.vendorId >>> 0
    this.productId = descriptor.productId >>> 0
    this.deviceClass = descriptor.classCode >>> 0
    this.deviceSubclass = descriptor.subclassCode >>> 0
    this.deviceProtocol = descriptor.protocolCode >>> 0
    this.productName =
      typeof descriptor.productName === 'string' ? descriptor.productName : null
    this.manufacturerName =
      typeof descriptor.manufacturerName === 'string'
        ? descriptor.manufacturerName
        : null
    this.serialNumber =
      typeof descriptor.serialNumber === 'string'
        ? descriptor.serialNumber
        : null
    this.opened = !!descriptor.opened
    this._authorized = !!descriptor.authorized
    this.configurations = Array.isArray(descriptor.interfaces)
      ? descriptor.interfaces.map((iface) => ({
        interfaceNumber: iface.interfaceNumber >>> 0,
        alternateSetting: iface.alternateSetting >>> 0,
        interfaceClass: iface.classCode >>> 0,
        interfaceSubclass: iface.subclassCode >>> 0,
        interfaceProtocol: iface.protocolCode >>> 0
      }))
      : []
  }

  async open () {
    if (this.opened) return
    const res = await ipc.request('usb.device.open', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.open)
    this.opened = true
  }

  async close () {
    if (!this.opened) return
    const res = await ipc.request('usb.device.close', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.close)
    this.opened = false
  }

  async forget () {
    const res = await ipc.request('usb.forgetDevice', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.forget)
    this._authorized = false
    this.opened = false
  }

  async selectConfiguration (configurationValue) {
    const res = await ipc.request('usb.device.selectConfiguration', {
      deviceId: this.deviceId,
      configurationValue: Number(configurationValue) || 0
    })
    if (res?.err) {
      throw maybeMakeError(res.err, USBDevice.prototype.selectConfiguration)
    }
  }

  async claimInterface (interfaceNumber) {
    const res = await ipc.request('usb.device.claimInterface', {
      deviceId: this.deviceId,
      interfaceNumber: Number(interfaceNumber) || 0
    })
    if (res?.err) {
      throw maybeMakeError(res.err, USBDevice.prototype.claimInterface)
    }
  }

  async releaseInterface (interfaceNumber) {
    const res = await ipc.request('usb.device.releaseInterface', {
      deviceId: this.deviceId,
      interfaceNumber: Number(interfaceNumber) || 0
    })
    if (res?.err) {
      throw maybeMakeError(res.err, USBDevice.prototype.releaseInterface)
    }
  }

  async selectAlternateInterface (interfaceNumber, alternateSetting) {
    const res = await ipc.request('usb.device.selectAlternateInterface', {
      deviceId: this.deviceId,
      interfaceNumber: Number(interfaceNumber) || 0,
      alternateSetting: Number(alternateSetting) || 0
    })
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        USBDevice.prototype.selectAlternateInterface
      )
    }
  }

  async controlTransferIn (setup, length) {
    const normalizedSetup = toUSBSetup(setup, 'in')
    const requestLength = validateTransferLength(
      length,
      MAX_CONTROL_TRANSFER_LENGTH,
      'length'
    )
    const res = await ipc.request('usb.transfer.controlIn', {
      deviceId: this.deviceId,
      setup: JSON.stringify(normalizedSetup),
      length: requestLength
    })
    if (res?.err) {
      throw maybeMakeError(res.err, USBDevice.prototype.controlTransferIn)
    }
    const payload = res?.data?.data ? base64ToDataView(res.data.data) : null
    return new USBInTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      payload
    )
  }

  async controlTransferOut (setup, data) {
    const normalizedSetup = toUSBSetup(setup, 'out')
    const payload = bufferSourceToUint8Array(data, 'data')
    ensurePayloadWithin(payload.length, MAX_CONTROL_TRANSFER_LENGTH, 'data')
    const res = await ipcWrite(
      'usb.transfer.controlOut',
      {
        deviceId: this.deviceId,
        setup: JSON.stringify(normalizedSetup)
      },
      payload
    )
    if (res?.err) {
      throw maybeMakeError(res.err, USBDevice.prototype.controlTransferOut)
    }
    return new USBOutTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      res?.data?.transferred ?? payload.length
    )
  }

  async transferIn (endpointNumber, length) {
    const requestLength = validateTransferLength(
      length,
      MAX_BULK_TRANSFER_LENGTH,
      'length'
    )
    const res = await ipc.request('usb.transfer.in', {
      deviceId: this.deviceId,
      endpointNumber: Number(endpointNumber) || 0,
      length: requestLength
    })
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.transferIn)
    const payload = res?.data?.data ? base64ToDataView(res.data.data) : null
    return new USBInTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      payload
    )
  }

  async transferOut (endpointNumber, data) {
    const payload = bufferSourceToUint8Array(data, 'data')
    ensurePayloadWithin(payload.length, MAX_BULK_TRANSFER_LENGTH, 'data')
    const res = await ipcWrite(
      'usb.transfer.out',
      {
        deviceId: this.deviceId,
        endpointNumber: Number(endpointNumber) || 0
      },
      payload
    )
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.transferOut)
    return new USBOutTransferResult(
      res?.data?.status ?? TRANSFER_STATUS_OK,
      res?.data?.transferred ?? payload.length
    )
  }

  async clearHalt (direction, endpointNumber) {
    const dir = String(direction).toLowerCase()
    if (dir !== 'in' && dir !== 'out') {
      throw new TypeError("direction must be either 'in' or 'out'")
    }
    const res = await ipc.request('usb.transfer.clearHalt', {
      deviceId: this.deviceId,
      direction: dir,
      endpointNumber: Number(endpointNumber) || 0
    })
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.clearHalt)
  }

  async reset () {
    const res = await ipc.request('usb.device.reset', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, USBDevice.prototype.reset)
  }
}

class NavigatorUSB extends EventTarget {
  constructor () {
    super()
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
        device.opened = false
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

  _createDevice (descriptor) {
    if (!descriptor || typeof descriptor !== 'object') return null
    const id = String(descriptor.deviceId ?? '')
    let device = this._deviceCache.get(id)
    if (device) {
      device._applyDescriptor(descriptor)
      return device
    }
    device = new USBDevice(descriptor, this)
    this._deviceCache.set(id, device)
    return device
  }

  async getDevices () {
    const res = await ipc.request('usb.getDevices', {})
    if (res?.err) {
      throw maybeMakeError(res.err, NavigatorUSB.prototype.getDevices)
    }
    const list = Array.isArray(res?.data?.devices) ? res.data.devices : []
    return list
      .map((descriptor) => this._createDevice(descriptor))
      .filter(Boolean)
  }

  async requestDevice (options = {}) {
    const normalized = normalizeRequestDeviceOptionsForIPC(options)
    const serializedOptions = JSON.stringify(normalized)
    const res = await ipc.request('usb.requestDevice', {
      options: serializedOptions
    })
    if (res?.err) {
      throw maybeMakeError(res.err, NavigatorUSB.prototype.requestDevice)
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
    const res = await ipc.request('usb.cancelRequest')
    if (res?.err) {
      throw maybeMakeError(res.err, NavigatorUSB.prototype.cancelRequest)
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
      try {
        const res = await ipc.request('usb.chooseDevice', {
          deviceId: match.deviceId
        })
        if (res?.err) {
          throw maybeMakeError(res.err, NavigatorUSB.prototype.requestDevice)
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
      try {
        const res = await ipc.request('usb.cancelRequest')
        if (res?.err) {
          throw maybeMakeError(res.err, NavigatorUSB.prototype.requestDevice)
        }
        resolved = true
        detail.handled = true
        rejectPromise(
          new DOMException('User cancelled device selection', 'AbortError')
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

export const installNavigatorUSB = () => {
  if (typeof navigator === 'undefined') return
  if (navigatorUSBInstance) return navigatorUSBInstance

  navigatorUSBInstance = new NavigatorUSB()
  try {
    Object.defineProperty(navigator, 'usb', {
      configurable: true,
      enumerable: true,
      value: navigatorUSBInstance
    })
  } catch {}

  return navigatorUSBInstance
}

try {
  installNavigatorUSB()
} catch {}

export {
  NavigatorUSB,
  USBDevice,
  USBInTransferResult,
  USBOutTransferResult,
  USBConnectionEvent
}
