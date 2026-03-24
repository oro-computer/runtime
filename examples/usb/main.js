import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleStack,
  ExampleSection,
  ExampleProse,
  StatusBanner,
  Button,
  Badge,
  FormField,
  Input,
  Switch,
  InlineCode,
  EmptyState,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  LogViewer
} from '../ui/index.js'

const {
  createElement: h,
  useState,
  useEffect,
  useMemo,
  useCallback,
  useRef
} = React

const READY_STATUS = {
  variant: 'info',
  title: 'Ready to enumerate USB devices',
  message: 'Refresh the authorized list or request a new grant to get started.'
}

const UNSUPPORTED_STATUS = {
  variant: 'danger',
  title: 'WebUSB not available',
  message: 'The runtime did not expose navigator.usb on this platform.'
}

function detectSupport () {
  return (
    typeof navigator !== 'undefined' &&
    navigator?.usb &&
    typeof navigator.usb.getDevices === 'function' &&
    typeof navigator.usb.requestDevice === 'function'
  )
}

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  if (error?.name && error?.message) {
    return `${error.name}: ${error.message}`
  }
  return String(error)
}

function formatHex (value, digits = 4) {
  if (value == null || Number.isNaN(value)) return '0x0000'
  const normalized = Number(value) >>> 0
  return `0x${normalized.toString(16).toUpperCase().padStart(digits, '0')}`
}

function describeDevice (device) {
  if (!device) return 'USB device'
  const vendor = formatHex(device.vendorId)
  const product = formatHex(device.productId)
  if (device.productName && device.manufacturerName) {
    return `${device.manufacturerName} ${device.productName} (${vendor}:${product})`
  }
  if (device.productName) {
    return `${device.productName} (${vendor}:${product})`
  }
  return `${vendor}:${product}`
}

function snapshotDevice (device) {
  return {
    deviceId: device.deviceId,
    vendorId: device.vendorId,
    productId: device.productId,
    productName:
      typeof device.productName === 'string' ? device.productName : null,
    manufacturerName:
      typeof device.manufacturerName === 'string'
        ? device.manufacturerName
        : null,
    opened: !!device.opened
  }
}

function parseUsbId (value, label) {
  if (!value || typeof value !== 'string') return null
  const trimmed = value.trim()
  if (!trimmed.length) return null
  const base = trimmed.startsWith('0x') || trimmed.startsWith('0X') ? 16 : 10
  const parsed = Number.parseInt(trimmed, base)
  if (!Number.isFinite(parsed) || parsed < 0 || parsed > 0xffff) {
    throw new TypeError(`${label} must be between 0x0000 and 0xFFFF`)
  }
  return parsed
}

function buildRequestOptions (options) {
  if (options.acceptAllDevices) {
    return { acceptAllDevices: true }
  }

  const vendorId = parseUsbId(options.vendorId, 'Vendor ID')
  const productId = parseUsbId(options.productId, 'Product ID')

  if (vendorId == null && productId == null) {
    throw new TypeError(
      'Specify a vendor or product ID, or enable "Accept all devices".'
    )
  }

  const filter = {}
  if (vendorId != null) filter.vendorId = vendorId
  if (productId != null) filter.productId = productId

  return { filters: [filter] }
}

function permissionLabel (permission) {
  if (!permission.available) return 'Unavailable'
  if (!permission.state) return 'Unknown'
  return permission.state.charAt(0).toUpperCase() + permission.state.slice(1)
}

function permissionVariant (permission) {
  if (!permission.available) return 'neutral'
  if (permission.state === 'granted') return 'success'
  if (permission.state === 'prompt') return 'warning'
  if (permission.state === 'denied') return 'danger'
  return 'neutral'
}

function requestSummary (options) {
  if (options.acceptAllDevices) {
    return 'acceptAllDevices: true'
  }
  const parts = []
  if (options.vendorId) parts.push(`vendorId=${options.vendorId}`)
  if (options.productId) parts.push(`productId=${options.productId}`)
  return parts.length ? parts.join(', ') : 'custom filters'
}

function sortDevices (devices = []) {
  return [...devices].sort((a, b) => {
    const vendor = (a.vendorId >>> 0) - (b.vendorId >>> 0)
    if (vendor !== 0) return vendor
    const product = (a.productId >>> 0) - (b.productId >>> 0)
    if (product !== 0) return product
    return a.deviceId.localeCompare(b.deviceId)
  })
}

function WebUSBControlCenter () {
  const [support, setSupport] = useState(() => detectSupport())
  const [status, setStatus] = useState(() =>
    support ? READY_STATUS : UNSUPPORTED_STATUS
  )
  const [logs, setLogs] = useState([])
  const [devices, setDevices] = useState([])
  const [pending, setPending] = useState({ refresh: false, request: false })
  const [devicePending, setDevicePending] = useState({})
  const [permission, setPermission] = useState({
    state: null,
    available: false
  })
  const [requestOptions, setRequestOptions] = useState({
    acceptAllDevices: true,
    vendorId: '',
    productId: ''
  })
  const [useCustomChooser, setUseCustomChooser] = useState(true)
  const [chooser, setChooser] = useState(null)

  const chooserRef = useRef(null)
  const customChooserRef = useRef(useCustomChooser)
  const permissionStatusRef = useRef(null)

  const appendLog = useCallback((message) => {
    setLogs((prev) => {
      const timestamp = new Date().toISOString().slice(11, 23)
      const next = [...prev, `[${timestamp}] ${message}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const markDevicePending = useCallback((deviceId, action) => {
    setDevicePending((prev) => {
      const next = { ...prev }
      if (!action) {
        delete next[deviceId]
      } else {
        next[deviceId] = action
      }
      return next
    })
  }, [])

  const refreshPermissionStatus = useCallback(
    async ({ silent = false } = {}) => {
      if (!support) {
        setPermission({ state: null, available: false })
        return
      }
      const permissions = navigator.permissions
      if (!permissions || typeof permissions.query !== 'function') {
        setPermission({ state: null, available: false })
        return
      }
      try {
        const statusObject = await permissions.query({ name: 'usb' })
        if (permissionStatusRef.current) {
          permissionStatusRef.current.onchange = null
        }
        permissionStatusRef.current = statusObject
        const apply = (origin = 'initial') => {
          setPermission({ state: statusObject.state, available: true })
          if (origin === 'change') {
            appendLog(`Permission state changed: ${statusObject.state}`)
          } else if (!silent) {
            appendLog(`Permission state: ${statusObject.state}`)
          }
        }
        apply('initial')
        statusObject.onchange = () => apply('change')
      } catch (error) {
        const message = formatError(error)
        setPermission({ state: null, available: false })
        appendLog(`Failed to query permissions: ${message}`)
      }
    },
    [support, appendLog]
  )

  const refreshDevices = useCallback(
    async ({ silent = false } = {}) => {
      if (!support) return []
      if (!silent) {
        setPending((prev) => ({ ...prev, refresh: true }))
      }
      try {
        const list = await navigator.usb.getDevices()
        const sorted = sortDevices(list)
        setDevices(sorted)
        if (!silent) {
          setStatus({
            variant: 'success',
            title: 'Enumerated USB devices',
            message: `Found ${sorted.length} authorized device${sorted.length === 1 ? '' : 's'}.`
          })
          appendLog(
            `Fetched ${sorted.length} authorized device${sorted.length === 1 ? '' : 's'}.`
          )
        }
        return sorted
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to enumerate devices',
          message
        })
        appendLog(`Failed to enumerate devices: ${message}`)
        throw error
      } finally {
        if (!silent) {
          setPending((prev) => ({ ...prev, refresh: false }))
        }
      }
    },
    [support, appendLog]
  )

  const handleRequestDevice = useCallback(async () => {
    if (!support) return
    let options
    try {
      options = buildRequestOptions(requestOptions)
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'warning',
        title: 'Invalid request filters',
        message
      })
      appendLog(`Rejected requestDevice call: ${message}`)
      return
    }

    setPending((prev) => ({ ...prev, request: true }))
    appendLog(
      `Requesting device (${requestSummary({ ...requestOptions, ...options })})`
    )

    try {
      const device = await navigator.usb.requestDevice(options)
      const label = describeDevice(device)
      setStatus({
        variant: 'success',
        title: 'Device authorization granted',
        message: `Authorized ${label}.`
      })
      appendLog(`Authorized ${label}.`)
      await refreshDevices({ silent: true })
      await refreshPermissionStatus({ silent: true })
    } catch (error) {
      const message = formatError(error)
      const variant = error?.name === 'AbortError' ? 'warning' : 'danger'
      const title =
        error?.name === 'AbortError'
          ? 'Device selection cancelled'
          : 'Failed to authorize device'
      setStatus({ variant, title, message })
      appendLog(`requestDevice failed: ${message}`)
    } finally {
      setPending((prev) => ({ ...prev, request: false }))
    }
  }, [
    support,
    requestOptions,
    refreshDevices,
    refreshPermissionStatus,
    appendLog
  ])

  const handleOpenDevice = useCallback(
    async (device) => {
      if (!device) return
      const id = device.deviceId
      markDevicePending(id, 'open')
      try {
        await device.open()
        setStatus({
          variant: 'success',
          title: 'Device opened',
          message: `Opened ${describeDevice(device)}.`
        })
        appendLog(`Opened ${describeDevice(device)}.`)
        setDevices((prev) =>
          prev.map((candidate) => {
            return candidate.deviceId === id ? device : candidate
          })
        )
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to open device',
          message
        })
        appendLog(`Failed to open ${describeDevice(device)}: ${message}`)
      } finally {
        markDevicePending(id, null)
      }
    },
    [appendLog, markDevicePending]
  )

  const handleCloseDevice = useCallback(
    async (device) => {
      if (!device) return
      const id = device.deviceId
      markDevicePending(id, 'close')
      try {
        await device.close()
        setStatus({
          variant: 'info',
          title: 'Device closed',
          message: `Closed ${describeDevice(device)}.`
        })
        appendLog(`Closed ${describeDevice(device)}.`)
        setDevices((prev) =>
          prev.map((candidate) => {
            return candidate.deviceId === id ? device : candidate
          })
        )
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to close device',
          message
        })
        appendLog(`Failed to close ${describeDevice(device)}: ${message}`)
      } finally {
        markDevicePending(id, null)
      }
    },
    [appendLog, markDevicePending]
  )

  const handleForgetDevice = useCallback(
    async (device) => {
      if (!device) return
      const id = device.deviceId
      markDevicePending(id, 'forget')
      try {
        await device.forget()
        setStatus({
          variant: 'info',
          title: 'Grant removed',
          message: `Removed stored permission for ${describeDevice(device)}.`
        })
        appendLog(`Forgot grant for ${describeDevice(device)}.`)
        setDevices((prev) =>
          prev.filter((candidate) => candidate.deviceId !== id)
        )
        await refreshPermissionStatus({ silent: true })
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to remove grant',
          message
        })
        appendLog(`Failed to forget ${describeDevice(device)}: ${message}`)
      } finally {
        markDevicePending(id, null)
      }
    },
    [appendLog, markDevicePending, refreshPermissionStatus]
  )

  const handleChooserSelect = useCallback(
    async (deviceId) => {
      const record = chooserRef.current
      if (!record) return
      const entry = record.devices.find(
        (candidate) => candidate.snapshot.deviceId === deviceId
      )
      if (!entry) return
      chooserRef.current = { ...record, busy: `select:${deviceId}` }
      setChooser((prev) =>
        prev ? { ...prev, busy: `select:${deviceId}` } : prev
      )
      try {
        await record.detail.select(entry.device)
        const label = describeDevice(entry.snapshot)
        setStatus({
          variant: 'success',
          title: 'Device selected',
          message: `Authorized ${label} via custom chooser.`
        })
        appendLog(`Custom chooser selected ${label}.`)
        chooserRef.current = null
        setChooser(null)
        await refreshDevices({ silent: true })
        await refreshPermissionStatus({ silent: true })
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Chooser selection failed',
          message
        })
        appendLog(`Chooser selection failed: ${message}`)
        chooserRef.current = null
        setChooser(null)
      }
    },
    [refreshDevices, refreshPermissionStatus, appendLog]
  )

  const handleChooserCancel = useCallback(async () => {
    const record = chooserRef.current
    if (!record) return
    chooserRef.current = { ...record, busy: 'cancel' }
    setChooser((prev) => (prev ? { ...prev, busy: 'cancel' } : prev))
    try {
      await record.detail.cancel()
      setStatus({
        variant: 'warning',
        title: 'Chooser cancelled',
        message: 'User cancelled device selection.'
      })
      appendLog('Custom chooser cancelled the request.')
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Failed to cancel chooser',
        message
      })
      appendLog(`Failed to cancel chooser: ${message}`)
    } finally {
      chooserRef.current = null
      setChooser(null)
    }
  }, [appendLog])

  const handleChooserEvent = useCallback(
    (event) => {
      appendLog(
        `Chooser requested (${event.detail.devices.length} device${event.detail.devices.length === 1 ? '' : 's'})`
      )
      if (!customChooserRef.current) {
        return
      }
      event.preventDefault()
      event.detail.markHandled?.()
      const devices = event.detail.devices.map((device) => ({
        device,
        snapshot: snapshotDevice(device)
      }))
      const record = { detail: event.detail, devices, busy: null }
      chooserRef.current = record
      setChooser({
        devices: devices.map((entry) => entry.snapshot),
        busy: null
      })
    },
    [appendLog]
  )

  useEffect(() => {
    customChooserRef.current = useCustomChooser
    if (!useCustomChooser) {
      chooserRef.current = null
      setChooser(null)
    }
  }, [useCustomChooser])

  useEffect(() => {
    if (!support) return
    refreshPermissionStatus({ silent: true }).catch(() => {})
    refreshDevices({ silent: true }).catch(() => {})
  }, [support, refreshDevices, refreshPermissionStatus])

  useEffect(() => {
    if (!support) return
    const onConnect = (event) => {
      appendLog(`Device connected: ${describeDevice(event.device)}`)
      setDevices((prev) => {
        const existing = prev.find(
          (candidate) => candidate.deviceId === event.device.deviceId
        )
        if (existing) {
          return prev.map((candidate) =>
            candidate.deviceId === event.device.deviceId
              ? event.device
              : candidate
          )
        }
        return sortDevices([...prev, event.device])
      })
    }
    const onDisconnect = (event) => {
      appendLog(`Device disconnected: ${describeDevice(event.device)}`)
      setDevices((prev) =>
        prev.filter((candidate) => candidate.deviceId !== event.device.deviceId)
      )
    }
    const onChooser = (event) => handleChooserEvent(event)

    navigator.usb.addEventListener('connect', onConnect)
    navigator.usb.addEventListener('disconnect', onDisconnect)
    globalThis.addEventListener('usb.chooserequest', onChooser)

    return () => {
      navigator.usb.removeEventListener('connect', onConnect)
      navigator.usb.removeEventListener('disconnect', onDisconnect)
      globalThis.removeEventListener('usb.chooserequest', onChooser)
    }
  }, [support, handleChooserEvent, appendLog])

  useEffect(() => {
    return () => {
      if (permissionStatusRef.current) {
        permissionStatusRef.current.onchange = null
      }
      if (chooserRef.current) {
        chooserRef.current.detail.cancel().catch(() => {})
        chooserRef.current = null
      }
    }
  }, [])

  const statusBanner = useMemo(
    () =>
      h(StatusBanner, {
        variant: status.variant,
        title: status.title,
        message: status.message
      }),
    [status]
  )

  const permissionBadge = useMemo(
    () =>
      h(
        Badge,
        {
          variant: permissionVariant(permission)
        },
        permissionLabel(permission)
      ),
    [permission]
  )

  useEffect(() => {
    if (support) return
    let active = true
    let timeoutId = null
    const poll = () => {
      if (!active) return
      if (detectSupport()) {
        setSupport(true)
        return
      }
      timeoutId = setTimeout(poll, 200)
    }
    poll()
    return () => {
      active = false
      if (timeoutId !== null) clearTimeout(timeoutId)
    }
  }, [support])

  useEffect(() => {
    if (!support) {
      setStatus((prev) =>
        prev.title === READY_STATUS.title ? UNSUPPORTED_STATUS : prev
      )
      return
    }
    setStatus((prev) => {
      if (
        prev.title === UNSUPPORTED_STATUS.title &&
        prev.message === UNSUPPORTED_STATUS.message
      ) {
        return READY_STATUS
      }
      return prev
    })
  }, [support])

  return h(
    ExampleLayout,
    {
      title: 'WebUSB Control Center',
      description:
        'Enumerate authorized devices, customise chooser flows, and drive control transfers with the Socket WebUSB bridge.'
    },
    h(
      ExampleStack,
      { gap: 'xl' },
      h(
        ExamplePanel,
        {
          title: 'Runtime status'
        },
        h(
          ExampleStack,
          { gap: 'lg' },
          statusBanner,
          h(
            ExampleSection,
            {
              title: 'Environment checks'
            },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                'div',
                null,
                h('strong', null, 'navigator.usb support: '),
                h(
                  Badge,
                  { variant: support ? 'success' : 'danger' },
                  support ? 'Supported' : 'Unavailable'
                )
              ),
              h(
                'div',
                null,
                h('strong', null, 'USB permission: '),
                permissionBadge
              ),
              h(
                'div',
                null,
                h('strong', null, 'Custom chooser: '),
                h(
                  Badge,
                  { variant: useCustomChooser ? 'success' : 'neutral' },
                  useCustomChooser ? 'Enabled' : 'Disabled'
                )
              )
            )
          ),
          h(
            ExampleProse,
            null,
            h(
              'p',
              null,
              'Use the panels below to refresh authorized devices, request new permissions, or intercept chooser requests with your own UI.'
            ),
            h(
              'p',
              null,
              'The runtime mirrors the browser WebUSB spec, so existing client code can often be reused with minimal changes.'
            )
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Authorized devices',
          description:
            'Inspect and manage the devices previously approved for this runtime.'
        },
        h(
          ExampleStack,
          { gap: 'lg' },
          h(
            ExampleSection,
            {
              title: 'Request permissions',
              description:
                'Provide optional USB identifiers or accept all devices to surface the runtime chooser.',
              actions: h(
                ExampleStack,
                { gap: 'sm' },
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'secondary',
                    size: 'sm',
                    disabled: pending.refresh || !support,
                    onClick: () => refreshDevices().catch(() => {})
                  },
                  pending.refresh ? 'Refreshing…' : 'Refresh devices'
                ),
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'primary',
                    size: 'sm',
                    disabled: pending.request || !support,
                    onClick: () => handleRequestDevice().catch(() => {})
                  },
                  pending.request ? 'Requesting…' : 'Request device'
                )
              )
            },
            h(
              ExampleStack,
              { gap: 'md' },
              h(
                FormField,
                {
                  label: 'Accept all devices',
                  description:
                    'Forward every device to the chooser without applying vendor/product filters.',
                  htmlFor: 'usb-accept-all'
                },
                h(Switch, {
                  id: 'usb-accept-all',
                  label: 'Accept all USB devices',
                  checked: requestOptions.acceptAllDevices,
                  onCheckedChange: (checked) => {
                    setRequestOptions((prev) => ({
                      ...prev,
                      acceptAllDevices: checked
                    }))
                  },
                  disabled: !support
                })
              ),
              h(
                FormField,
                {
                  label: 'Vendor ID',
                  description: 'Hex (0x1234) or decimal vendor identifier.',
                  htmlFor: 'usb-vendor-id',
                  hint: requestOptions.acceptAllDevices
                    ? 'Filters disabled while accepting all devices.'
                    : null
                },
                h(Input, {
                  id: 'usb-vendor-id',
                  placeholder: '0x1234',
                  value: requestOptions.vendorId,
                  disabled: requestOptions.acceptAllDevices || !support,
                  inputMode: 'text',
                  onChange: (event) => {
                    const value = event.target.value
                    setRequestOptions((prev) => ({ ...prev, vendorId: value }))
                  }
                })
              ),
              h(
                FormField,
                {
                  label: 'Product ID',
                  description: 'Hex (0x0001) or decimal product identifier.',
                  htmlFor: 'usb-product-id',
                  hint: requestOptions.acceptAllDevices
                    ? 'Filters disabled while accepting all devices.'
                    : null
                },
                h(Input, {
                  id: 'usb-product-id',
                  placeholder: '0x0001',
                  value: requestOptions.productId,
                  disabled: requestOptions.acceptAllDevices || !support,
                  inputMode: 'text',
                  onChange: (event) => {
                    const value = event.target.value
                    setRequestOptions((prev) => ({ ...prev, productId: value }))
                  }
                })
              )
            )
          ),
          h(
            ExampleSection,
            {
              title: 'Authorized device cache',
              description:
                'Devices returned by navigator.usb.getDevices(). Newly approved devices appear here automatically.'
            },
            devices.length
              ? h(
                Table,
                null,
                h(
                  TableHead,
                  null,
                  h(
                    TableRow,
                    null,
                    h(TableHeader, null, 'Device'),
                    h(TableHeader, null, 'Identifiers'),
                    h(TableHeader, null, 'Status'),
                    h(TableHeader, null, 'Actions')
                  )
                ),
                h(
                  TableBody,
                  null,
                  devices.map((device) => {
                    const pendingAction =
                        devicePending[device.deviceId] || null
                    return h(
                      TableRow,
                      { key: device.deviceId },
                      h(
                        TableCell,
                        null,
                        h(
                          'div',
                          null,
                          device.productName || 'Unknown product'
                        ),
                        device.manufacturerName &&
                            h('div', null, device.manufacturerName),
                        h('div', null, h(InlineCode, null, device.deviceId))
                      ),
                      h(
                        TableCell,
                        null,
                        h(
                          'div',
                          null,
                          'Vendor: ',
                          h(InlineCode, null, formatHex(device.vendorId))
                        ),
                        h(
                          'div',
                          null,
                          'Product: ',
                          h(InlineCode, null, formatHex(device.productId))
                        )
                      ),
                      h(
                        TableCell,
                        null,
                        h(
                          Badge,
                          { variant: device.opened ? 'success' : 'neutral' },
                          device.opened ? 'Opened' : 'Closed'
                        )
                      ),
                      h(
                        TableCell,
                        null,
                        h(
                          ExampleStack,
                          { gap: 'sm' },
                          h(
                            Button,
                            {
                              type: 'button',
                              size: 'sm',
                              variant: 'secondary',
                              disabled:
                                  !support || device.opened || pendingAction,
                              onClick: () =>
                                handleOpenDevice(device).catch(() => {})
                            },
                            pendingAction === 'open' ? 'Opening…' : 'Open'
                          ),
                          h(
                            Button,
                            {
                              type: 'button',
                              size: 'sm',
                              variant: 'secondary',
                              disabled:
                                  !support || !device.opened || pendingAction,
                              onClick: () =>
                                handleCloseDevice(device).catch(() => {})
                            },
                            pendingAction === 'close' ? 'Closing…' : 'Close'
                          ),
                          h(
                            Button,
                            {
                              type: 'button',
                              size: 'sm',
                              variant: 'ghost',
                              disabled: !support || pendingAction,
                              onClick: () =>
                                handleForgetDevice(device).catch(() => {})
                            },
                            pendingAction === 'forget'
                              ? 'Removing…'
                              : 'Forget grant'
                          )
                        )
                      )
                    )
                  })
                )
              )
              : h(EmptyState, {
                title: 'No authorized devices yet',
                description:
                    'Use requestDevice or connect hardware that you previously approved to populate this list.',
                actions: h(
                  Button,
                  {
                    type: 'button',
                    variant: 'primary',
                    size: 'sm',
                    disabled: pending.request || !support,
                    onClick: () => handleRequestDevice().catch(() => {})
                  },
                  'Request access'
                )
              })
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Chooser flow',
          description:
            'Intercept chooser requests and drive device selection from your own UI.'
        },
        h(
          ExampleStack,
          { gap: 'lg' },
          h(
            ExampleSection,
            {
              title: 'Chooser configuration'
            },
            h(
              FormField,
              {
                label: 'Enable custom chooser',
                description:
                  'Handle usb.chooserequest events yourself. Disable to fall back to the runtime overlay.',
                htmlFor: 'usb-custom-chooser'
              },
              h(Switch, {
                id: 'usb-custom-chooser',
                label: 'Enable custom chooser',
                checked: useCustomChooser,
                onCheckedChange: (checked) => setUseCustomChooser(checked),
                disabled: !support
              })
            )
          ),
          h(
            ExampleSection,
            {
              title: 'Pending chooser',
              description: useCustomChooser
                ? 'When requestDevice needs a manual selection, the runtime fires usb.chooserequest. Use the controls below to pick a device.'
                : 'Custom chooser disabled. The runtime overlay handles selection automatically.'
            },
            chooser && chooser.devices.length
              ? h(
                Table,
                null,
                h(
                  TableHead,
                  null,
                  h(
                    TableRow,
                    null,
                    h(TableHeader, null, 'Device'),
                    h(TableHeader, null, 'Identifiers'),
                    h(TableHeader, null, 'Actions')
                  )
                ),
                h(
                  TableBody,
                  null,
                  chooser.devices.map((device) =>
                    h(
                      TableRow,
                      { key: device.deviceId },
                      h(
                        TableCell,
                        null,
                        h(
                          'div',
                          null,
                          device.productName || 'Unknown product'
                        ),
                        device.manufacturerName &&
                            h('div', null, device.manufacturerName)
                      ),
                      h(
                        TableCell,
                        null,
                        h(
                          'div',
                          null,
                          'Vendor: ',
                          h(InlineCode, null, formatHex(device.vendorId))
                        ),
                        h(
                          'div',
                          null,
                          'Product: ',
                          h(InlineCode, null, formatHex(device.productId))
                        )
                      ),
                      h(
                        TableCell,
                        null,
                        h(
                          ExampleStack,
                          { gap: 'sm' },
                          h(
                            Button,
                            {
                              type: 'button',
                              size: 'sm',
                              variant: 'primary',
                              disabled:
                                  chooser.busy &&
                                  chooser.busy !== `select:${device.deviceId}`,
                              onClick: () =>
                                handleChooserSelect(device.deviceId).catch(
                                  () => {}
                                )
                            },
                            chooser.busy === `select:${device.deviceId}`
                              ? 'Selecting…'
                              : 'Select'
                          ),
                          h(
                            Button,
                            {
                              type: 'button',
                              size: 'sm',
                              variant: 'ghost',
                              disabled:
                                  chooser.busy && chooser.busy !== 'cancel',
                              onClick: () =>
                                handleChooserCancel().catch(() => {})
                            },
                            chooser.busy === 'cancel'
                              ? 'Cancelling…'
                              : 'Cancel request'
                          )
                        )
                      )
                    )
                  )
                )
              )
              : h(EmptyState, {
                title: useCustomChooser
                  ? 'Awaiting chooser request'
                  : 'Runtime chooser active',
                description: useCustomChooser
                  ? 'Call requestDevice with multiple candidates to populate this panel.'
                  : 'Use requestDevice to surface the default runtime chooser overlay.'
              })
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Event log',
          description:
            'USB permissions, chooser requests, and lifecycle events.'
        },
        h(
          ExampleSection,
          {
            title: 'Activity log',
            actions: h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                size: 'sm',
                onClick: () => setLogs([])
              },
              'Clear log'
            )
          },
          h(LogViewer, {
            entries: logs,
            emptyState:
              'Call requestDevice or connect hardware to populate the log.'
          })
        )
      )
    )
  )
}

mountExample(WebUSBControlCenter)
