/* global EventTarget */
// Web Bluetooth (scaffold)
// Spec: https://bluetoothcg.github.io/web-bluetooth/

import ipc, { maybeMakeError } from '../ipc.js'

const clampByte = (value, label) => {
  const n = Number(value)
  if (!Number.isFinite(n) || !Number.isInteger(n) || n < 0 || n > 255) {
    throw new TypeError(`${label} entries must be integers between 0 and 255`)
  }
  return n
}

const toByteArray = (value, label) => {
  if (value == null) return undefined
  if (Array.isArray(value)) {
    return value.map((entry) => clampByte(entry, label))
  }
  if (ArrayBuffer.isView(value)) {
    return Array.from(
      new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
    )
  }
  if (
    value instanceof ArrayBuffer ||
    (typeof SharedArrayBuffer === 'function' &&
      value instanceof SharedArrayBuffer)
  ) {
    return Array.from(new Uint8Array(value))
  }
  throw new TypeError(
    `${label} must be an ArrayBuffer, TypedArray, or array of numbers`
  )
}

const normalizeManufacturerDataEntry = (entry) => {
  if (!entry || typeof entry !== 'object') {
    throw new TypeError('manufacturerData entries must be objects')
  }
  const id = Number(entry.companyIdentifier)
  if (!Number.isInteger(id) || id < 0 || id > 0xffff) {
    throw new TypeError(
      'manufacturerData.companyIdentifier must be an integer between 0 and 65535'
    )
  }
  const normalized = { companyIdentifier: id }
  const prefix = toByteArray(entry.dataPrefix, 'manufacturerData.dataPrefix')
  if (prefix && prefix.length > 0) normalized.dataPrefix = prefix
  const mask = toByteArray(entry.dataMask, 'manufacturerData.dataMask')
  if (mask && mask.length > 0) {
    if (prefix && mask.length !== prefix.length) {
      throw new TypeError(
        'manufacturerData.dataMask length must match dataPrefix length'
      )
    }
    normalized.dataMask = mask
  }
  return normalized
}

const normalizeDeviceFilter = (filter) => {
  if (!filter || typeof filter !== 'object') {
    throw new TypeError('BluetoothDeviceFilter must be an object')
  }
  const normalized = {}
  if (Array.isArray(filter.services)) {
    normalized.services = filter.services.slice()
  }
  if (typeof filter.name === 'string') normalized.name = filter.name
  if (typeof filter.namePrefix === 'string') {
    normalized.namePrefix = filter.namePrefix
  }
  if (
    Array.isArray(filter.manufacturerData) &&
    filter.manufacturerData.length > 0
  ) {
    normalized.manufacturerData = filter.manufacturerData.map(
      normalizeManufacturerDataEntry
    )
  }
  return normalized
}

const normalizeRequestDeviceOptionsForIPC = (options) => {
  const normalized = {}
  normalized.acceptAllDevices = !!options.acceptAllDevices
  if (Array.isArray(options.filters)) {
    normalized.filters = options.filters.map(normalizeDeviceFilter)
  }
  if (Array.isArray(options.optionalServices)) {
    normalized.optionalServices = options.optionalServices.slice()
  }
  if (options.servicesMatch != null) {
    const mode = String(options.servicesMatch).toLowerCase()
    if (mode !== 'all' && mode !== 'any') {
      throw new TypeError("servicesMatch must be 'all' or 'any'")
    }
    normalized.servicesMatch = mode
  }
  if (options.timeoutMs != null) {
    const timeout = Number(options.timeoutMs)
    if (!Number.isFinite(timeout) || timeout <= 0) {
      throw new TypeError('timeoutMs must be a positive number')
    }
    normalized.timeoutMs = Math.trunc(timeout)
  }
  return normalized
}

const decodeBase64ToArrayBuffer = (value) => {
  if (typeof value !== 'string') return null
  try {
    if (typeof atob === 'function') {
      const binary = atob(value)
      const buffer = new ArrayBuffer(binary.length)
      const view = new Uint8Array(buffer)
      for (let i = 0; i < binary.length; ++i) view[i] = binary.charCodeAt(i)
      return buffer
    }
    if (typeof Buffer === 'function' && typeof Buffer.from === 'function') {
      const buf = Buffer.from(value, 'base64')
      const arrayBuffer = new ArrayBuffer(buf.length)
      const view = new Uint8Array(arrayBuffer)
      for (let i = 0; i < buf.length; ++i) view[i] = buf[i]
      return arrayBuffer
    }
    return null
  } catch {
    return null
  }
}

class BluetoothManufacturerDataMap extends Map {
  constructor (entries) {
    super()
    if (entries && typeof entries[Symbol.iterator] === 'function') {
      for (const [key, value] of entries) {
        this.set(key, value)
      }
    }
  }

  set (key, value) {
    const companyId = Number(key)
    if (!Number.isInteger(companyId)) {
      throw new TypeError('manufacturerData keys must be integers')
    }
    if (!(value instanceof DataView)) {
      throw new TypeError('manufacturerData values must be DataView instances')
    }
    return super.set(companyId, value)
  }

  get (key) {
    return super.get(Number(key))
  }

  has (key) {
    return super.has(Number(key))
  }
}

const createManufacturerMap = (entries) => {
  const map = new BluetoothManufacturerDataMap()
  if (!Array.isArray(entries)) return map
  for (const entry of entries) {
    const companyId = Number(entry?.companyId ?? entry?.companyIdentifier)
    if (!Number.isInteger(companyId)) continue
    const encoding =
      typeof entry?.encoding === 'string'
        ? entry.encoding.toLowerCase()
        : 'base64'
    if (encoding !== 'base64') continue
    const buffer = decodeBase64ToArrayBuffer(entry?.data ?? '')
    if (!buffer) continue
    map.set(companyId, new DataView(buffer))
  }
  return map
}

const updateDeviceMetadataFromDetail = (device, detail) => {
  if (!device || !detail) return
  if (typeof detail.name === 'string' && detail.name.length) {
    try {
      device.name = detail.name
    } catch {}
  }
  if (Array.isArray(detail.services)) {
    device.uuids = detail.services.map((s) => String(s).toLowerCase())
  }
  if (Array.isArray(detail.manufacturerData)) {
    const map = createManufacturerMap(detail.manufacturerData)
    if (map.size > 0) device.manufacturerData = map
  } else if (
    detail.manufacturerDataMap instanceof Map &&
    detail.manufacturerDataMap.size > 0
  ) {
    device.manufacturerData = detail.manufacturerDataMap
  }
}

class BluetoothRemoteGATTCharacteristic extends EventTarget {
  constructor (service, uuid) {
    super()
    this.service = service
    this.uuid = String(uuid).toLowerCase()
    this.properties = Object.freeze({
      broadcast: false,
      read: false,
      writeWithoutResponse: false,
      write: false,
      notify: false,
      indicate: false,
      authenticatedSignedWrites: false,
      reliableWrite: false,
      writableAuxiliaries: false
    })
  }

  async readValue () {
    const res = await ipc.request(
      'bluetooth.characteristic.readValue',
      {
        deviceId: this.service?.device?.id,
        service: this.service?.uuid,
        characteristic: this.uuid
      },
      { responseType: 'arraybuffer' }
    )
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTCharacteristic.prototype.readValue
      )
    }
    // Expect ArrayBuffer or { data } with buffer; normalize to DataView per spec
    const buf =
      res?.data instanceof Uint8Array
        ? res.data.buffer
        : res?.data?.buffer || res?.data
    const dv = new DataView(buf || new ArrayBuffer(0))
    try {
      this.value = dv
    } catch {}
    return dv
  }

  async writeValue (value) {
    const res = await ipc.request(
      'bluetooth.characteristic.writeValue',
      {
        deviceId: this.service?.device?.id,
        service: this.service?.uuid,
        characteristic: this.uuid
      },
      value
    )
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTCharacteristic.prototype.writeValue
      )
    }
  }

  async writeValueWithResponse (value) {
    return this.writeValue(value)
  }

  async writeValueWithoutResponse (value) {
    const res = await ipc.request(
      'bluetooth.characteristic.writeValue',
      {
        deviceId: this.service?.device?.id,
        service: this.service?.uuid,
        characteristic: this.uuid,
        writeWithoutResponse: true
      },
      value
    )
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTCharacteristic.prototype.writeValueWithoutResponse
      )
    }
  }

  async startNotifications () {
    const res = await ipc.request(
      'bluetooth.characteristic.startNotifications',
      {
        deviceId: this.service?.device?.id,
        service: this.service?.uuid,
        characteristic: this.uuid
      }
    )
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTCharacteristic.prototype.startNotifications
      )
    }
    return this
  }

  async stopNotifications () {
    const res = await ipc.request(
      'bluetooth.characteristic.stopNotifications',
      {
        deviceId: this.service?.device?.id,
        service: this.service?.uuid,
        characteristic: this.uuid
      }
    )
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTCharacteristic.prototype.stopNotifications
      )
    }
    return this
  }
}

class BluetoothRemoteGATTService extends EventTarget {
  constructor (server, uuid, primary = true) {
    super()
    this.device = server.device
    this.uuid = String(uuid).toLowerCase()
    this.isPrimary = !!primary
  }

  async getCharacteristic (uuid) {
    const res = await ipc.request('bluetooth.service.getCharacteristic', {
      deviceId: this.device?.id,
      service: this.uuid,
      characteristic: uuid
    })
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTService.prototype.getCharacteristic
      )
    }
    const ch = new BluetoothRemoteGATTCharacteristic(this, uuid)
    const pb = res?.data?.propertiesByCharacteristic
    if (pb && pb[uuid]) {
      try {
        ch.properties = Object.freeze({
          broadcast: !!pb[uuid].broadcast,
          read: !!pb[uuid].read,
          writeWithoutResponse: !!pb[uuid].writeWithoutResponse,
          write: !!pb[uuid].write,
          notify: !!pb[uuid].notify,
          indicate: !!pb[uuid].indicate,
          authenticatedSignedWrites: !!pb[uuid].authenticatedSignedWrites,
          reliableWrite: !!pb[uuid].reliableWrite,
          writableAuxiliaries: !!pb[uuid].writableAuxiliaries
        })
      } catch {}
    }
    return ch
  }

  async getCharacteristics (uuid) {
    const res = await ipc.request('bluetooth.service.getCharacteristics', {
      deviceId: this.device?.id,
      service: this.uuid,
      characteristic: uuid ?? ''
    })
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTService.prototype.getCharacteristics
      )
    }
    const list = Array.isArray(res?.data?.characteristics)
      ? res.data.characteristics
      : uuid
        ? [uuid]
        : []
    const pb = res?.data?.propertiesByCharacteristic || {}
    return list.map((id) => {
      const ch = new BluetoothRemoteGATTCharacteristic(this, id)
      if (pb && pb[id]) {
        try {
          ch.properties = Object.freeze({
            broadcast: !!pb[id].broadcast,
            read: !!pb[id].read,
            writeWithoutResponse: !!pb[id].writeWithoutResponse,
            write: !!pb[id].write,
            notify: !!pb[id].notify,
            indicate: !!pb[id].indicate,
            authenticatedSignedWrites: !!pb[id].authenticatedSignedWrites,
            reliableWrite: !!pb[id].reliableWrite,
            writableAuxiliaries: !!pb[id].writableAuxiliaries
          })
        } catch {}
      }
      return ch
    })
  }
}

class BluetoothRemoteGATTServer extends EventTarget {
  constructor (device) {
    super()
    this.device = device
    this.connected = false
  }

  async connect () {
    const res = await ipc.request('bluetooth.gatt.connect', {
      deviceId: this.device?.id
    })
    if (res?.err) {
      throw maybeMakeError(res.err, BluetoothRemoteGATTServer.prototype.connect)
    }
    this.connected = true
    return this
  }

  disconnect () {
    ipc
      .request('bluetooth.gatt.disconnect', { deviceId: this.device?.id })
      .catch(() => {})
    this.connected = false
  }

  async getPrimaryService (uuid) {
    const res = await ipc.request('bluetooth.gatt.getPrimaryService', {
      deviceId: this.device?.id,
      service: uuid
    })
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTServer.prototype.getPrimaryService
      )
    }
    return new BluetoothRemoteGATTService(this, uuid, true)
  }

  async getPrimaryServices (uuid) {
    const res = await ipc.request('bluetooth.gatt.getPrimaryServices', {
      deviceId: this.device?.id,
      service: uuid ?? ''
    })
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothRemoteGATTServer.prototype.getPrimaryServices
      )
    }
    const services = Array.isArray(res?.data?.services)
      ? res.data.services
      : uuid
        ? [uuid]
        : []
    return services.map((s) => new BluetoothRemoteGATTService(this, s, true))
  }
}

class BluetoothDevice extends EventTarget {
  constructor ({ id = '', name = '', services = [], manufacturerData } = {}) {
    super()
    this.id = String(id)
    this.name = name || null
    this.gatt = new BluetoothRemoteGATTServer(this)
    this.gattServer = this.gatt // legacy alias seen in the wild
    this.uuids = Array.isArray(services)
      ? services.map((s) => String(s).toLowerCase())
      : []
    this.manufacturerData = createManufacturerMap(manufacturerData)
  }

  async watchAdvertisements () {
    const res = await ipc.request('bluetooth.device.watchAdvertisements', {
      deviceId: this.id
    })
    if (res?.err) {
      throw maybeMakeError(
        res.err,
        BluetoothDevice.prototype.watchAdvertisements
      )
    }
    return this
  }

  async forget () {
    const res = await ipc.request('bluetooth.device.forget', {
      deviceId: this.id
    })
    if (res?.err) {
      throw maybeMakeError(res.err, BluetoothDevice.prototype.forget)
    }
    return this
  }
}

class Bluetooth extends EventTarget {
  async requestDevice (options = {}) {
    // Spec validation (https://bluetoothcg.github.io/web-bluetooth/#dom-bluetooth-requestdevice)
    if (options == null || typeof options !== 'object') {
      throw new TypeError(
        'Failed to execute "requestDevice" on "Bluetooth": options must be an object'
      )
    }

    const { filters, optionalServices, acceptAllDevices } = options

    if (acceptAllDevices && Array.isArray(filters) && filters.length > 0) {
      throw new TypeError(
        'Failed to execute "requestDevice" on "Bluetooth": filters may not be used with acceptAllDevices'
      )
    }

    if (!acceptAllDevices) {
      if (!Array.isArray(filters) || filters.length === 0) {
        throw new TypeError(
          'Failed to execute "requestDevice" on "Bluetooth": either filters must be non-empty or acceptAllDevices must be true'
        )
      }

      for (const f of filters) {
        if (typeof f !== 'object' || f == null) {
          throw new TypeError('BluetoothDeviceFilter must be an object')
        }
        const hasServices = Array.isArray(f.services) && f.services.length > 0
        const hasName = typeof f.name === 'string' && f.name.length > 0
        const hasNamePrefix =
          typeof f.namePrefix === 'string' && f.namePrefix.length > 0
        const hasManufacturerData =
          Array.isArray(f.manufacturerData) && f.manufacturerData.length > 0
        if (
          !hasServices &&
          !hasName &&
          !hasNamePrefix &&
          !hasManufacturerData
        ) {
          throw new TypeError(
            'BluetoothDeviceFilter must include at least one of services, name, namePrefix, or manufacturerData'
          )
        }
      }
    }

    if (Array.isArray(optionalServices)) {
      for (const s of optionalServices) {
        if (typeof s !== 'string' && typeof s !== 'number') {
          throw new TypeError(
            'optionalServices entries must be UUID strings or numbers'
          )
        }
      }
    }

    const normalizedOptions = normalizeRequestDeviceOptionsForIPC(options)
    const res = await ipc.request('bluetooth.requestDevice', {
      options: JSON.stringify(normalizedOptions)
    })
    if (res?.err) {
      throw maybeMakeError(res.err, Bluetooth.prototype.requestDevice)
    }
    return new BluetoothDevice(res?.data?.device)
  }

  async getDevices () {
    const res = await ipc.request('bluetooth.getDevices', {})
    if (res?.err) throw maybeMakeError(res.err, Bluetooth.prototype.getDevices)
    const list = Array.isArray(res?.data?.devices) ? res.data.devices : []
    const registry =
      this && this._deviceRegistry instanceof Map ? this._deviceRegistry : null
    const devices = []
    for (const detail of list) {
      if (!detail || typeof detail !== 'object') continue
      const id = detail.id != null ? String(detail.id) : ''
      if (!id) continue
      let device = registry?.get(id)
      if (!device) {
        device = new BluetoothDevice(detail)
        if (registry) {
          try {
            registry.set(id, device)
          } catch {}
        }
      } else {
        updateDeviceMetadataFromDetail(device, detail)
      }
      devices.push(device)
    }
    return devices
  }

  async getAvailability () {
    // Allow flipping to true when a backend lands per platform
    const res = await ipc.request('bluetooth.getAvailability')
    if (res?.err) return false
    return !!res?.data?.available
  }
}

function installNavigatorBluetooth () {
  // If the engine already supports Web Bluetooth, don’t override it.
  if (globalThis?.navigator && 'bluetooth' in globalThis.navigator) return

  const bluetooth = new Bluetooth()

  Object.defineProperty(globalThis.navigator, 'bluetooth', {
    configurable: true,
    enumerable: true,
    get: () => bluetooth
  })

  // Characteristic instance registry for routing notifications
  const makeKey = (d, s, c) =>
    `${d}|${String(s).toLowerCase()}|${String(c).toLowerCase()}`
  const characteristicRegistry = new Map()
  const deviceRegistry = new Map()
  const pendingDeviceMetadata = new Map()
  const chooserRows = new Map()
  const discoveryCache = new Map()
  const DEVICE_CACHE_TTL_MS = 45000 // reuse discoveries for 45 seconds to improve UX
  const MAX_CACHED_DEVICES = 20
  let chooserActive = false
  let chooserOverlay = null

  bluetooth._deviceRegistry = deviceRegistry

  const originalGetCharacteristic =
    BluetoothRemoteGATTService.prototype.getCharacteristic
  BluetoothRemoteGATTService.prototype.getCharacteristic = async function (
    uuid
  ) {
    const ch = await originalGetCharacteristic.call(this, uuid)
    try {
      characteristicRegistry.set(
        makeKey(this.device.id, this.uuid, ch.uuid),
        ch
      )
    } catch {}
    return ch
  }

  const originalGetCharacteristics =
    BluetoothRemoteGATTService.prototype.getCharacteristics
  BluetoothRemoteGATTService.prototype.getCharacteristics = async function (
    uuid
  ) {
    const list = await originalGetCharacteristics.call(this, uuid)
    for (const ch of list) {
      try {
        characteristicRegistry.set(
          makeKey(this.device.id, this.uuid, ch.uuid),
          ch
        )
      } catch {}
    }
    return list
  }

  // Bridge native characteristic updates to instances
  globalThis.addEventListener('bluetooth.characteristicvaluechanged', (e) => {
    const { deviceId, service, characteristic, value, encoding } =
      e.detail || {}
    const ch = characteristicRegistry.get(
      makeKey(deviceId, service, characteristic)
    )
    if (!ch) return

    let buf = new ArrayBuffer(0)
    if (typeof value === 'string' && encoding === 'base64') {
      const bin = atob(value)
      const arr = new Uint8Array(bin.length)
      for (let i = 0; i < bin.length; ++i) arr[i] = bin.charCodeAt(i)
      buf = arr.buffer
    }
    try {
      ch.value = new DataView(buf)
    } catch {}
    ch.dispatchEvent(new Event('characteristicvaluechanged'))
  })

  // Track devices created via requestDevice to dispatch standard events
  const originalRequestDevice = bluetooth.requestDevice.bind(bluetooth)
  bluetooth.requestDevice = async function (options) {
    pendingDeviceMetadata.clear()
    const dev = await originalRequestDevice(options)
    const key = String(dev.id)
    try {
      deviceRegistry.set(key, dev)
    } catch {}
    if (pendingDeviceMetadata.has(key)) {
      updateDeviceMetadataFromDetail(dev, pendingDeviceMetadata.get(key))
      pendingDeviceMetadata.delete(key)
    }
    pendingDeviceMetadata.clear()
    return dev
  }

  // Map native disconnect events to spec event on device
  globalThis.addEventListener('bluetooth.gattserverdisconnected', (e) => {
    const { deviceId } = e.detail || {}
    const dev = deviceRegistry.get(String(deviceId))
    if (!dev) return
    try {
      dev.gatt.connected = false
    } catch {}
    dev.dispatchEvent(new Event('gattserverdisconnected'))
  })

  const getPreferredColorScheme = () => {
    try {
      return globalThis.matchMedia?.('(prefers-color-scheme: dark)')?.matches
        ? 'dark'
        : 'light'
    } catch {
      return 'light'
    }
  }

  const applyChooserRowTheme = (row, theme) => {
    const hoverBackground =
      theme === 'dark' ? 'rgba(255,255,255,0.08)' : '#f8f8f8'
    const baseBackground = 'transparent'
    row.dataset.theme = theme
    row.style.borderTop =
      theme === 'dark'
        ? '1px solid rgba(255,255,255,0.08)'
        : '1px solid #f3f3f3'
    row.style.background = baseBackground
    row.style.color = theme === 'dark' ? '#f3f3f4' : '#111111'
    row.onmouseenter = () => {
      row.style.background = hoverBackground
    }
    row.onmouseleave = () => {
      row.style.background = baseBackground
    }
  }

  const applyButtonTheme = (button, theme) => {
    if (theme === 'dark') {
      button.style.background = 'rgba(255,255,255,0.08)'
      button.style.color = '#f3f3f4'
      button.style.borderColor = 'rgba(255,255,255,0.15)'
    } else {
      button.style.background = '#ffffff'
      button.style.color = '#222222'
      button.style.borderColor = 'rgba(0,0,0,0.12)'
    }
  }

  const ensurePlaceholderControls = (overlay, list) => {
    let placeholder = list.querySelector('#socket-bluetooth-placeholder')
    if (!placeholder) {
      placeholder = globalThis.document.createElement('div')
      placeholder.id = 'socket-bluetooth-placeholder'
      placeholder.style.cssText =
        'padding:24px;text-align:center;opacity:0.7;font-size:14px;display:flex;flex-direction:column;gap:12px;align-items:center'
      const message = globalThis.document.createElement('div')
      message.dataset.role = 'message'
      message.textContent = 'Scanning for nearby Bluetooth devices…'
      const actions = globalThis.document.createElement('div')
      actions.style.cssText =
        'display:flex;gap:12px;align-items:center;justify-content:center'
      const button = globalThis.document.createElement('button')
      button.type = 'button'
      button.dataset.role = 'refresh'
      button.style.cssText =
        'padding:8px 16px;border-radius:6px;border:1px solid rgba(0,0,0,0.15);cursor:pointer;font:inherit;transition:filter .2s ease'
      actions.append(button)
      placeholder.append(message, actions)
      list.replaceChildren(placeholder)
    }
    const message = placeholder.querySelector('[data-role=message]')
    const button = placeholder.querySelector('[data-role=refresh]')
    applyButtonTheme(button, overlay.dataset.theme || getPreferredColorScheme())
    button.onmouseenter = () => {
      if (!button.disabled) button.style.filter = 'brightness(0.96)'
    }
    button.onmouseleave = () => {
      button.style.filter = 'none'
    }
    return { placeholder, message, button }
  }

  const startRefreshTimer = (overlay, list, controls) => {
    if (!overlay || !list || !chooserActive) return
    if (overlay.dataset.refreshTimer) {
      globalThis.clearTimeout(Number(overlay.dataset.refreshTimer))
      overlay.dataset.refreshTimer = ''
    }
    const timerId = globalThis.setTimeout(() => {
      if (!overlay.isConnected || chooserRows.size > 0 || !chooserActive) return
      const ctrl = controls ?? ensurePlaceholderControls(overlay, list)
      if (ctrl.message) ctrl.message.textContent = 'No devices found.'
      if (ctrl.button) {
        ctrl.button.disabled = false
        ctrl.button.style.opacity = '1'
        ctrl.button.textContent = 'Refresh'
        applyButtonTheme(
          ctrl.button,
          overlay.dataset.theme || getPreferredColorScheme()
        )
      }
    }, 30000)
    overlay.dataset.refreshTimer = String(timerId)
  }

  const triggerDiscoveryRefresh = async (overlay, list, controls) => {
    if (!overlay || !list || !controls || !chooserActive) return
    const { message, button } = controls
    if (message) message.textContent = 'Scanning for nearby Bluetooth devices…'
    chooserRows.forEach(({ row }) => {
      try {
        row.remove()
      } catch {}
    })
    chooserRows.clear()
    if (button) {
      button.disabled = true
      button.style.opacity = '0.6'
      button.textContent = 'Refreshing…'
    }
    startRefreshTimer(overlay, list, controls)
    try {
      const res = await ipc.request('bluetooth.restartDiscovery')
      if (res?.err || res?.data?.scheduled === false) {
        throw new Error('refresh failed')
      }
      if (button) {
        button.disabled = false
        button.style.opacity = '1'
        button.textContent = 'Refresh'
        applyButtonTheme(
          button,
          overlay.dataset.theme || getPreferredColorScheme()
        )
      }
    } catch {
      if (message) message.textContent = 'Unable to refresh scan'
      if (button) {
        button.disabled = false
        button.style.opacity = '1'
        button.textContent = 'Retry'
        applyButtonTheme(
          button,
          overlay.dataset.theme || getPreferredColorScheme()
        )
      }
    }
  }

  function ensureChooser (forceCreate = false) {
    if (!globalThis.document) return null
    if (chooserOverlay && chooserOverlay.isConnected) return chooserOverlay
    if (!forceCreate) {
      return chooserOverlay && chooserOverlay.isConnected
        ? chooserOverlay
        : null
    }
    chooserRows.forEach(({ row }) => {
      try {
        row.remove()
      } catch {}
    })
    chooserRows.clear()

    const doc = globalThis.document
    const overlay = doc.createElement('div')
    overlay.id = 'socket-bluetooth-chooser'
    overlay.style.cssText =
      'position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;justify-content:center;font-family:system-ui,sans-serif;transition:background-color .2s ease'
    const panel = doc.createElement('div')
    panel.style.cssText =
      'min-width:320px;max-width:480px;max-height:70vh;overflow:auto;border-radius:8px;box-shadow:0 12px 40px rgba(0,0,0,.3);transition:background-color .2s ease,color .2s ease'
    const header = doc.createElement('div')
    header.textContent = 'Select a Bluetooth device'
    header.style.cssText =
      'padding:12px 16px;font-weight:600;transition:border-color .2s ease'
    const list = doc.createElement('div')
    list.id = 'socket-bluetooth-chooser-list'
    list.style.cssText = 'display:flex;flex-direction:column'
    const actions = doc.createElement('div')
    actions.style.cssText =
      'display:flex;gap:8px;justify-content:flex-end;padding:12px 16px;transition:border-color .2s ease'
    const cancel = doc.createElement('button')
    cancel.type = 'button'
    cancel.textContent = 'Cancel'
    cancel.style.cssText =
      'padding:8px 16px;border-radius:6px;border:1px solid transparent;cursor:pointer;font:inherit;transition:background-color .2s ease,color .2s ease,border-color .2s ease'
    let cancelling = false
    const handleCancel = async () => {
      if (cancelling) return
      cancelling = true
      cancel.dataset.busy = '1'
      cancel.style.opacity = '0.6'
      cancel.textContent = 'Cancelling…'
      try {
        const res = await ipc.request('bluetooth.cancelRequest')
        if (res?.err) throw res.err
      } catch {}
      chooserActive = false
      overlay.remove()
    }
    cancel.onclick = () => {
      handleCancel().catch(() => {})
    }

    actions.appendChild(cancel)
    panel.append(header, list, actions)
    overlay.append(panel)
    overlay.addEventListener('click', (e) => {
      if (e.target === overlay) handleCancel().catch(() => {})
    })
    const body = doc.body
    const previousOverflow = body ? body.style.overflow : ''
    overlay.dataset.prevOverflow = previousOverflow ?? ''
    if (body) body.style.overflow = 'hidden'
    doc.body.append(overlay)

    const applyTheme = () => {
      const theme = getPreferredColorScheme()
      overlay.dataset.theme = theme
      if (theme === 'dark') {
        overlay.style.background = 'rgba(0,0,0,0.6)'
        panel.style.background = '#1f1f20'
        panel.style.color = '#f3f3f4'
        header.style.borderBottom = '1px solid rgba(255,255,255,0.1)'
        actions.style.borderTop = '1px solid rgba(255,255,255,0.1)'
        cancel.style.background = 'rgba(255,255,255,0.08)'
        cancel.style.color = '#f3f3f4'
        cancel.style.borderColor = 'rgba(255,255,255,0.15)'
      } else {
        overlay.style.background = 'rgba(0,0,0,0.35)'
        panel.style.background = '#ffffff'
        panel.style.color = '#111111'
        header.style.borderBottom = '1px solid #eee'
        actions.style.borderTop = '1px solid #eee'
        cancel.style.background = '#ffffff'
        cancel.style.color = '#222222'
        cancel.style.borderColor = 'rgba(0,0,0,0.12)'
      }
      chooserRows.forEach(({ row }) => applyChooserRowTheme(row, theme))
      const placeholderButton = list.querySelector(
        '#socket-bluetooth-placeholder [data-role=refresh]'
      )
      if (placeholderButton) applyButtonTheme(placeholderButton, theme)
    }

    const watcher = globalThis.matchMedia?.('(prefers-color-scheme: dark)')
    const updateTheme = () => applyTheme()
    if (watcher) {
      try {
        watcher.addEventListener('change', updateTheme)
      } catch {
        watcher.addListener(updateTheme)
      }
    }

    let cleaned = false
    const cleanup = () => {
      if (cleaned) return
      cleaned = true
      if (watcher) {
        try {
          watcher.removeEventListener('change', updateTheme)
        } catch {
          watcher.removeListener(updateTheme)
        }
      }
      if (overlay.dataset.refreshTimer) {
        globalThis.clearTimeout(Number(overlay.dataset.refreshTimer))
        overlay.dataset.refreshTimer = ''
      }
      if (body) {
        body.style.overflow = overlay.dataset.prevOverflow ?? ''
      }
      chooserRows.clear()
      chooserOverlay = null
      chooserActive = false
    }
    const originalRemove = overlay.remove.bind(overlay)
    overlay.remove = () => {
      cleanup()
      originalRemove()
    }

    applyTheme()
    chooserOverlay = overlay
    return chooserOverlay
  }

  const pruneDiscoveryCache = () => {
    const now = Date.now()
    for (const [key, entry] of discoveryCache) {
      if (!entry || !entry.expires || entry.expires <= now) {
        discoveryCache.delete(key)
      }
    }
  }

  const recordDiscoveryInCache = (id, name, rssi) => {
    if (!id || !name) return
    pruneDiscoveryCache()
    const now = Date.now()
    const entry = {
      id,
      name,
      rssi: typeof rssi === 'number' ? rssi : null,
      lastSeen: now,
      expires: now + DEVICE_CACHE_TTL_MS
    }
    if (discoveryCache.has(id)) discoveryCache.delete(id)
    discoveryCache.set(id, entry)
    if (discoveryCache.size > MAX_CACHED_DEVICES) {
      const oldest = discoveryCache.keys().next().value
      if (oldest) discoveryCache.delete(oldest)
    }
  }

  const renderChooserRow = (detail, overlay, list) => {
    if (!overlay || !list || !detail) return false
    const id = detail.id != null ? String(detail.id) : ''
    if (!id) return false
    const label = typeof detail.name === 'string' ? detail.name.trim() : ''
    if (!label) return false
    const strength = typeof detail.rssi === 'number' ? `${detail.rssi} dBm` : ''

    const placeholder = list.querySelector('#socket-bluetooth-placeholder')
    if (placeholder) placeholder.remove()

    if (chooserRows.has(id)) {
      const entry = chooserRows.get(id)
      entry.name.textContent = label
      entry.rssi.textContent = strength
      return true
    }

    const row = globalThis.document.createElement('button')
    row.type = 'button'
    row.style.cssText =
      'display:flex;justify-content:space-between;align-items:center;width:100%;padding:12px 16px;border:none;cursor:pointer;text-align:left;font:inherit;background:transparent;transition:background-color .2s ease,color .2s ease'
    row.dataset.deviceId = id
    const left = globalThis.document.createElement('div')
    left.textContent = label
    const right = globalThis.document.createElement('div')
    right.textContent = strength
    applyChooserRowTheme(
      row,
      overlay.dataset.theme || getPreferredColorScheme()
    )
    row.append(left, right)

    let selecting = false
    row.onclick = async () => {
      if (selecting || !chooserActive) return
      selecting = true
      row.disabled = true
      row.style.opacity = '0.6'
      try {
        const res = await ipc.request('bluetooth.chooseDevice', {
          deviceId: id
        })
        if (res?.err) throw res.err
        chooserActive = false
        overlay.remove()
      } catch {
        row.disabled = false
        row.style.opacity = '1'
        selecting = false
      }
    }

    chooserRows.set(id, { row, name: left, rssi: right })
    list.append(row)
    return true
  }

  const seedChooserFromCache = (overlay, list, controls) => {
    pruneDiscoveryCache()
    const entries = Array.from(discoveryCache.values()).sort(
      (a, b) => (b.lastSeen || 0) - (a.lastSeen || 0)
    )
    let seeded = false
    for (const entry of entries) {
      if (renderChooserRow(entry, overlay, list)) seeded = true
    }
    if (seeded && controls) {
      if (controls.message) {
        controls.message.textContent =
          'Select a previously seen device or refresh to rescan.'
      }
      if (controls.button) {
        controls.button.disabled = false
        controls.button.style.opacity = '1'
        controls.button.textContent = 'Refresh'
        applyButtonTheme(
          controls.button,
          overlay.dataset.theme || getPreferredColorScheme()
        )
      }
    }
    return seeded
  }

  const showScanningState = () => {
    chooserActive = true
    const overlay = ensureChooser(true)
    if (!overlay) return
    const list = overlay.querySelector('#socket-bluetooth-chooser-list')
    if (!list) return
    chooserRows.forEach(({ row }) => {
      try {
        row.remove()
      } catch {}
    })
    chooserRows.clear()
    const controls = ensurePlaceholderControls(overlay, list)
    if (controls.message) {
      controls.message.textContent = 'Scanning for nearby Bluetooth devices…'
    }
    if (controls.button) {
      controls.button.disabled = true
      controls.button.style.opacity = '0.6'
      controls.button.textContent = 'Refresh'
      setTimeout(() => {
        if (!controls.button.isConnected || !chooserActive) return
        controls.button.disabled = false
        controls.button.style.opacity = '1'
      }, 2000)
      controls.button.onclick = () => {
        if (!chooserActive) return
        triggerDiscoveryRefresh(overlay, list, controls)
      }
    }
    const seeded = seedChooserFromCache(overlay, list, controls)
    if (!seeded && controls.button) {
      controls.button.disabled = true
      controls.button.style.opacity = '0.6'
      controls.button.textContent = 'Refresh'
    }
    startRefreshTimer(overlay, list, controls)
  }

  globalThis.addEventListener('bluetooth.chooserequest', showScanningState)
  globalThis.addEventListener('bluetooth.discoverystarted', showScanningState)
  globalThis.addEventListener('bluetooth.devicefound', (e) => {
    const detail = e.detail || {}
    const id = detail.id != null ? String(detail.id) : ''
    const name = typeof detail.name === 'string' ? detail.name : ''
    const rssi = detail.rssi
    if (id) {
      pendingDeviceMetadata.set(id, detail)
      const device = deviceRegistry.get(id)
      if (device) {
        updateDeviceMetadataFromDetail(device, detail)
        pendingDeviceMetadata.delete(id)
      }
    }
    const manufacturerMap = createManufacturerMap(detail.manufacturerData)
    if (manufacturerMap.size > 0) {
      detail.manufacturerDataMap = manufacturerMap
    }
    if (Array.isArray(detail.services)) {
      detail.services = detail.services.map((s) => String(s).toLowerCase())
    }

    const label = name && name.trim ? name.trim() : ''
    if (!label) return
    if (!id) return

    recordDiscoveryInCache(id, label, rssi)

    if (!chooserActive) return
    const overlay = ensureChooser()
    if (!overlay) return
    const list = overlay.querySelector('#socket-bluetooth-chooser-list')
    if (!list) return
    if (overlay.dataset.refreshTimer) {
      globalThis.clearTimeout(Number(overlay.dataset.refreshTimer))
      overlay.dataset.refreshTimer = ''
    }
    renderChooserRow({ id, name: label, rssi }, overlay, list)
    startRefreshTimer(overlay, list)
  })
}

try {
  installNavigatorBluetooth()
} catch {}

export {
  Bluetooth,
  BluetoothDevice,
  BluetoothRemoteGATTServer,
  BluetoothRemoteGATTService,
  BluetoothRemoteGATTCharacteristic,
  BluetoothManufacturerDataMap
}
