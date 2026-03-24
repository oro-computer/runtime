/* global EventTarget, DOMException, CustomEvent */
// WebHID scaffold
// Spec reference: https://wicg.github.io/webhid/

import ipc, { maybeMakeError, write as ipcWrite } from '../ipc.js'
import { Buffer } from '../buffer.js'

const clampUnsignedShort = (value, label) => {
  const n = Number(value)
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 0 || n > 0xffff) {
    throw new TypeError(`${label} must be an integer between 0 and 65535`)
  }
  return n
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
  if (typeof encoded !== 'string' || encoded.length === 0) {
    return new DataView(new ArrayBuffer(0))
  }
  try {
    const bytes = Buffer?.from
      ? Buffer.from(encoded, 'base64')
      : Uint8Array.from(atob(encoded), (c) => c.charCodeAt(0))
    const array = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)
    const buffer = array.buffer.slice(
      array.byteOffset,
      array.byteOffset + array.byteLength
    )
    return new DataView(buffer)
  } catch {
    return new DataView(new ArrayBuffer(0))
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
      normalized.vendorId = clampUnsignedShort(
        filter.vendorId,
        'filters.vendorId'
      )
    }
    if ('productId' in filter) {
      normalized.productId = clampUnsignedShort(
        filter.productId,
        'filters.productId'
      )
    }
    if ('usagePage' in filter) {
      normalized.usagePage = clampUnsignedShort(
        filter.usagePage,
        'filters.usagePage'
      )
    }
    if ('usage' in filter) {
      normalized.usage = clampUnsignedShort(filter.usage, 'filters.usage')
    }
    if (!Object.keys(normalized).length) {
      throw new TypeError('Each filter must specify at least one constraint')
    }
    return normalized
  })
}

const normalizeRequestDeviceOptionsForIPC = (options) => {
  if (!options || typeof options !== 'object') options = {}
  const normalized = { acceptAllDevices: !!options.acceptAllDevices }
  normalized.filters = normalizeFilters(options.filters)
  if (!normalized.acceptAllDevices && normalized.filters.length === 0) {
    throw new TypeError(
      'filters must be non-empty when acceptAllDevices is false'
    )
  }
  return normalized
}

const parseCollections = (collections) => {
  if (!Array.isArray(collections)) return []
  return collections.map((collection) => ({
    usagePage: collection?.usagePage >>> 0,
    usage: collection?.usage >>> 0,
    type: typeof collection?.type === 'string' ? collection.type : 'collection',
    inputReports: Array.isArray(collection?.inputReports)
      ? collection.inputReports.map((report) => ({
        reportId: report?.reportId >>> 0,
        size: report?.size >>> 0
      }))
      : [],
    outputReports: Array.isArray(collection?.outputReports)
      ? collection.outputReports.map((report) => ({
        reportId: report?.reportId >>> 0,
        size: report?.size >>> 0
      }))
      : [],
    featureReports: Array.isArray(collection?.featureReports)
      ? collection.featureReports.map((report) => ({
        reportId: report?.reportId >>> 0,
        size: report?.size >>> 0
      }))
      : [],
    usesInputReportId: !!collection?.usesInputReportId,
    usesOutputReportId: !!collection?.usesOutputReportId,
    usesFeatureReportId: !!collection?.usesFeatureReportId
  }))
}

class HIDInputReportEvent extends Event {
  constructor (type, init) {
    super(type)
    this.device = init?.device ?? null
    this.reportId = init?.reportId >>> 0
    this.data =
      init?.data instanceof DataView
        ? init.data
        : new DataView(new ArrayBuffer(0))
  }
}

class HIDDevice extends EventTarget {
  #collections
  #authorized
  #opened

  constructor (descriptor, _navigatorHID) {
    super()
    this.#collections = []
    this.#authorized = false
    this.#opened = false
    this._applyDescriptor(descriptor)
  }

  _applyDescriptor (descriptor = {}) {
    this.deviceId = String(descriptor.deviceId ?? '')
    this.vendorId = descriptor.vendorId >>> 0
    this.productId = descriptor.productId >>> 0
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
    this.#authorized = !!descriptor.authorized
    this.#collections = parseCollections(descriptor.collections)
  }

  get collections () {
    return this.#collections
  }

  get opened () {
    return !!this.#opened
  }

  set opened (value) {
    this.#opened = !!value
  }

  get authorized () {
    return this.#authorized
  }

  async open () {
    if (this.opened) return
    const res = await ipc.request('hid.device.open', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, HIDDevice.prototype.open)
    const descriptor = res?.data?.device
    if (descriptor) this._applyDescriptor(descriptor)
    this.opened = true
  }

  async close () {
    if (!this.opened) return
    const res = await ipc.request('hid.device.close', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, HIDDevice.prototype.close)
    const descriptor = res?.data?.device
    if (descriptor) this._applyDescriptor(descriptor)
    this.opened = false
  }

  async forget () {
    const res = await ipc.request('hid.forgetDevice', {
      deviceId: this.deviceId
    })
    if (res?.err) throw maybeMakeError(res.err, HIDDevice.prototype.forget)
    this.#authorized = false
    this.opened = false
  }

  async sendReport (reportId, data) {
    const payload = bufferSourceToUint8Array(data, 'data')
    const res = await ipcWrite(
      'hid.device.sendReport',
      {
        deviceId: this.deviceId,
        reportId: Number(reportId) || 0
      },
      payload
    )
    if (res?.err) throw maybeMakeError(res.err, HIDDevice.prototype.sendReport)
  }

  async sendFeatureReport (reportId, data) {
    const payload = bufferSourceToUint8Array(data, 'data')
    const res = await ipcWrite(
      'hid.device.sendFeatureReport',
      {
        deviceId: this.deviceId,
        reportId: Number(reportId) || 0
      },
      payload
    )
    if (res?.err) {
      throw maybeMakeError(res.err, HIDDevice.prototype.sendFeatureReport)
    }
  }

  async receiveFeatureReport (reportId, length) {
    const res = await ipc.request('hid.device.receiveFeatureReport', {
      deviceId: this.deviceId,
      reportId: Number(reportId) || 0,
      length: Number(length) || 0
    })
    if (res?.err) {
      throw maybeMakeError(res.err, HIDDevice.prototype.receiveFeatureReport)
    }
    const view = base64ToDataView(res?.data?.data)
    return view
  }
}

class NavigatorHID extends EventTarget {
  #deviceCache
  #onNativeConnect
  #onNativeDisconnect
  #onNativeInputReport

  constructor () {
    super()
    this.#deviceCache = new Map()

    this.#onNativeConnect = (event) => {
      try {
        const descriptor = event?.detail?.device
        if (!descriptor) return
        const device = this.#createDevice(descriptor)
        if (!device) return
        this.dispatchEvent(new CustomEvent('connect', { detail: { device } }))
      } catch (err) {
        console.error(err)
      }
    }

    this.#onNativeDisconnect = (event) => {
      try {
        const descriptor = event?.detail?.device
        const deviceId = descriptor?.deviceId ?? event?.detail?.deviceId
        if (!deviceId) return
        const device = this.#createDevice(descriptor ?? { deviceId })
        if (!device) return
        device.opened = false
        this.dispatchEvent(
          new CustomEvent('disconnect', { detail: { device } })
        )
      } catch (err) {
        console.error(err)
      }
    }

    this.#onNativeInputReport = (event) => {
      try {
        const detail = event?.detail
        if (!detail) return
        const device = this.#createDevice({ deviceId: detail.deviceId })
        if (!device) return
        const dataView = base64ToDataView(detail.data)
        const reportEvent = new HIDInputReportEvent('inputreport', {
          device,
          reportId: Number(detail.reportId) || 0,
          data: dataView
        })
        device.dispatchEvent(reportEvent)
      } catch (err) {
        console.error(err)
      }
    }

    if (typeof globalThis.addEventListener === 'function') {
      globalThis.addEventListener('hid.deviceconnect', this.#onNativeConnect)
      globalThis.addEventListener(
        'hid.devicedisconnect',
        this.#onNativeDisconnect
      )
      globalThis.addEventListener('hid.inputreport', this.#onNativeInputReport)
    }
  }

  #createDevice (descriptor) {
    if (!descriptor || typeof descriptor !== 'object') return null
    const id = String(descriptor.deviceId ?? '')
    if (!id) return null
    let device = this.#deviceCache.get(id)
    if (device) {
      device._applyDescriptor(descriptor)
      return device
    }
    device = new HIDDevice(descriptor)
    this.#deviceCache.set(id, device)
    return device
  }

  async getDevices () {
    const res = await ipc.request('hid.getDevices', {})
    if (res?.err) {
      throw maybeMakeError(res.err, NavigatorHID.prototype.getDevices)
    }
    const list = Array.isArray(res?.data?.devices) ? res.data.devices : []
    return list
      .map((descriptor) => this.#createDevice(descriptor))
      .filter(Boolean)
  }

  async requestDevice (options = {}) {
    const normalized = normalizeRequestDeviceOptionsForIPC(options)
    const serializedOptions = JSON.stringify(normalized)
    const res = await ipc.request('hid.requestDevice', {
      options: serializedOptions
    })
    if (res?.err) {
      throw maybeMakeError(res.err, NavigatorHID.prototype.requestDevice)
    }

    if (res?.data?.device) {
      return this.#createDevice(res.data.device)
    }

    if (res?.data?.requiresSelection) {
      const descriptors = Array.isArray(res.data.devices)
        ? res.data.devices
        : []
      const devices = descriptors
        .map((d) => this.#createDevice(d))
        .filter(Boolean)
      if (!devices.length) {
        throw new DOMException('No HID devices available', 'NotFoundError')
      }
      return await this.#presentChooser(devices)
    }

    throw new DOMException(
      'Unexpected response from hid.requestDevice',
      'InvalidStateError'
    )
  }

  async cancelRequest () {
    const res = await ipc.request('hid.cancelRequest')
    if (res?.err) {
      throw maybeMakeError(res.err, NavigatorHID.prototype.cancelRequest)
    }
  }

  async #presentChooser (devices) {
    const detail = this.#createChooserDetail(devices)
    const event = new CustomEvent('hid.chooserequest', {
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
          'HID device chooser already resolved',
          'InvalidStateError'
        )
      }
    }

    const owner = this
    return {
      devices: validDevices,
      handled: false,
      result,
      markHandled () {
        this.handled = true
      },
      async select (device) {
        ensurePending()
        resolved = true
        const res = await ipc.request('hid.chooseDevice', {
          deviceId: device?.deviceId
        })
        if (res?.err) {
          throw maybeMakeError(res.err, NavigatorHID.prototype.requestDevice)
        }
        if (!res?.data?.device) {
          throw new DOMException('Device selection failed', 'InvalidStateError')
        }
        const normalized = owner.#createDevice(res.data.device)
        resolvePromise(normalized)
        return normalized
      },
      async cancel () {
        ensurePending()
        resolved = true
        await ipc.request('hid.cancelRequest')
        rejectPromise(
          new DOMException('User cancelled HID device request', 'AbortError')
        )
      }
    }
  }
}

const presentDefaultChooser = async (detail) => {
  detail.markHandled()
  return await new Promise((resolve, reject) => {
    const devices = detail.devices || []
    const overlay = document.createElement('div')
    overlay.style.position = 'fixed'
    overlay.style.inset = '0'
    overlay.style.zIndex = '2147483647'
    overlay.style.background = 'rgba(0,0,0,0.4)'
    overlay.style.display = 'flex'
    overlay.style.alignItems = 'center'
    overlay.style.justifyContent = 'center'

    const dialog = document.createElement('div')
    dialog.style.background = '#fff'
    dialog.style.borderRadius = '8px'
    dialog.style.minWidth = '320px'
    dialog.style.maxWidth = '90vw'
    dialog.style.boxShadow = '0 10px 40px rgba(0,0,0,0.25)'
    dialog.style.padding = '16px'
    dialog.style.fontFamily = 'sans-serif'

    const title = document.createElement('h2')
    title.textContent = 'Select a HID device'
    title.style.margin = '0 0 12px'
    title.style.fontSize = '18px'
    dialog.appendChild(title)

    const list = document.createElement('ul')
    list.style.listStyle = 'none'
    list.style.margin = '0'
    list.style.padding = '0'
    list.style.maxHeight = '240px'
    list.style.overflow = 'auto'
    dialog.appendChild(list)

    const renderDevice = (device) => {
      const item = document.createElement('li')
      item.style.margin = '0'
      item.style.padding = '8px'
      item.style.cursor = 'pointer'
      item.style.borderRadius = '6px'
      item.style.transition = 'background 0.2s'
      item.addEventListener('mouseenter', () => {
        item.style.background = 'rgba(0,0,0,0.05)'
      })
      item.addEventListener('mouseleave', () => {
        item.style.background = 'transparent'
      })
      item.textContent =
        device.productName ||
        `0x${device.vendorId.toString(16).padStart(4, '0')}:0x${device.productId.toString(16).padStart(4, '0')}`
      item.addEventListener('click', async () => {
        try {
          overlay.remove()
          const selected = await detail.select(device)
          resolve(selected)
        } catch (err) {
          reject(err)
        }
      })
      return item
    }

    devices.forEach((device) => list.appendChild(renderDevice(device)))

    const footer = document.createElement('div')
    footer.style.display = 'flex'
    footer.style.justifyContent = 'flex-end'
    footer.style.marginTop = '12px'

    const cancel = document.createElement('button')
    cancel.textContent = 'Cancel'
    cancel.style.padding = '6px 12px'
    cancel.style.cursor = 'pointer'
    cancel.addEventListener('click', async () => {
      overlay.remove()
      try {
        await detail.cancel()
      } catch (err) {
        reject(err)
        return
      }
      reject(
        new DOMException('User cancelled HID device request', 'AbortError')
      )
    })

    footer.appendChild(cancel)
    dialog.appendChild(footer)
    overlay.appendChild(dialog)
    document.body.appendChild(overlay)
  })
}

let navigatorHIDInstance = null

export const installNavigatorHID = () => {
  if (navigatorHIDInstance) return navigatorHIDInstance
  navigatorHIDInstance = new NavigatorHID()
  if (typeof navigator === 'object' && navigator) {
    Object.defineProperty(navigator, 'hid', {
      configurable: true,
      enumerable: true,
      get: () => navigatorHIDInstance
    })
  }
  return navigatorHIDInstance
}

try {
  installNavigatorHID()
} catch {}

export { NavigatorHID, HIDDevice, HIDInputReportEvent }
