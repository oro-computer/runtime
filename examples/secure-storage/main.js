import * as secureStorage from 'oro:secure-storage'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleGrid,
  ExamplePanel,
  ExampleStack,
  StatusBanner,
  Badge,
  FormField,
  Input,
  Textarea,
  Select,
  Option,
  Button,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  EmptyState,
  LogViewer,
  Code,
  InlineCode
} from '../ui/index.js'

const { createElement: h, useState, useEffect, useCallback, useMemo } = React

const DEFAULT_SCOPE_LABEL =
  typeof globalThis.location?.origin === 'string'
    ? globalThis.location.origin
    : 'app://local'

const STORE_ENCODINGS = [
  { value: 'utf8', label: 'UTF-8 string' },
  { value: 'base64', label: 'Base64 string' },
  { value: 'hex', label: 'Hex string' }
]

const READ_ENCODINGS = [
  { value: 'utf8', label: 'UTF-8 string' },
  { value: 'base64', label: 'Base64 string' },
  { value: 'hex', label: 'Hex string' },
  { value: 'buffer', label: 'Uint8Array buffer' }
]

function formatError (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function describeScope (scope) {
  if (scope && scope.length) return `scope "${scope}"`
  return 'default origin scope'
}

function formatStoredValueForDisplay (value, encoding) {
  if (value == null) {
    return {
      text: '',
      hint: 'Entry not found in the active scope.'
    }
  }

  if (encoding === 'buffer' && value instanceof Uint8Array) {
    const head = Array.from(value.slice(0, 32))
      .map((byte) => byte.toString(16).padStart(2, '0'))
      .join(' ')
    const suffix = value.length > 32 ? ' …' : ''
    return {
      text: `Uint8Array(${value.byteLength}) [${head}${suffix}]`,
      hint:
        value.length > 0
          ? 'Binary payload rendered as a hex preview.'
          : 'Empty binary payload.'
    }
  }

  const label =
    encoding === 'utf8'
      ? 'UTF-8 string value.'
      : `${encoding.toUpperCase()} encoded string.`

  return {
    text: String(value),
    hint: label
  }
}

function SecureStorageVault () {
  const [scopeInput, setScopeInput] = useState('')
  const [scope, setScope] = useState(null)
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Ready to manage secrets',
    message:
      'Store, inspect, and clear values from the runtime-backed secure storage.'
  })
  const [keys, setKeys] = useState([])
  const [logs, setLogs] = useState([])
  const [pending, setPending] = useState({
    refresh: false,
    store: false,
    fetch: false,
    clear: false,
    remove: null
  })
  const [storeForm, setStoreForm] = useState({
    key: '',
    value: '',
    encoding: 'utf8'
  })
  const [inspect, setInspect] = useState({
    key: '',
    encoding: 'utf8',
    valueText: '',
    valueHint: ''
  })

  const appendLog = useCallback((message) => {
    setLogs((prev) => {
      const timestamp = new Date().toISOString().slice(11, 23)
      const entry = {
        id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
        text: `[${timestamp}] ${message}`
      }
      const next = [...prev, entry]
      return next.length > 200 ? next.slice(next.length - 200) : next
    })
  }, [])

  const refreshKeys = useCallback(async () => {
    const params = scope ? { scope } : {}
    setPending((prev) => ({ ...prev, refresh: true }))
    try {
      const list = await secureStorage.keys(params)
      const sorted = Array.isArray(list)
        ? [...list].sort((a, b) => a.localeCompare(b))
        : []
      setKeys(sorted)
      setInspect((prev) => {
        const nextKey = sorted.includes(prev.key) ? prev.key : sorted[0] || ''
        return {
          ...prev,
          key: nextKey,
          valueText: nextKey === prev.key ? prev.valueText : '',
          valueHint: nextKey === prev.key ? prev.valueHint : ''
        }
      })
      const scopeLabel = describeScope(scope)
      setStatus({
        variant: 'success',
        title: 'Keys refreshed',
        message: `Found ${sorted.length} entr${sorted.length === 1 ? 'y' : 'ies'} in the ${scopeLabel}.`
      })
      appendLog(`Refreshed ${sorted.length} key(s) in the ${scopeLabel}.`)
      return sorted
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Failed to refresh keys',
        message
      })
      appendLog(`Failed to refresh keys: ${message}`)
      throw error
    } finally {
      setPending((prev) => ({ ...prev, refresh: false }))
    }
  }, [scope, appendLog])

  const handleApplyScope = useCallback(
    (event) => {
      event.preventDefault()
      const trimmed = scopeInput.trim()
      setScope(trimmed.length ? trimmed : null)
    },
    [scopeInput]
  )

  useEffect(() => {
    setInspect((prev) => ({
      ...prev,
      valueText: '',
      valueHint: ''
    }))
    refreshKeys().catch(() => {})
  }, [scope, refreshKeys])

  const handleStore = useCallback(
    async (event) => {
      event.preventDefault()
      const key = storeForm.key.trim()
      if (!key) {
        setStatus({
          variant: 'warning',
          title: 'Missing key',
          message: 'Provide a key before storing a value.'
        })
        return
      }

      setPending((prev) => ({ ...prev, store: true }))
      try {
        const options = scope
          ? { scope, encoding: storeForm.encoding }
          : { encoding: storeForm.encoding }
        await secureStorage.setItem(key, storeForm.value, options)
        setStatus({
          variant: 'success',
          title: 'Value stored',
          message: `Saved "${key}" into the ${describeScope(scope)}.`
        })
        appendLog(`Stored key "${key}" (${storeForm.encoding}).`)
        setStoreForm((prev) => ({
          ...prev,
          key: '',
          value: ''
        }))
        await refreshKeys()
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to store value',
          message
        })
        appendLog(`Failed to store key "${key}": ${message}`)
      } finally {
        setPending((prev) => ({ ...prev, store: false }))
      }
    },
    [storeForm, scope, refreshKeys, appendLog]
  )

  const fetchValue = useCallback(
    async ({ key, encoding }) => {
      const trimmed = (key || '').trim()
      if (!trimmed) {
        setStatus({
          variant: 'warning',
          title: 'Select a key',
          message: 'Choose a stored entry to inspect its value.'
        })
        return
      }

      setPending((prev) => ({ ...prev, fetch: true }))
      try {
        const options = scope ? { scope, encoding } : { encoding }
        const value = await secureStorage.getItem(trimmed, options)

        if (value == null) {
          setInspect((prev) => ({
            ...prev,
            key: trimmed,
            valueText: '',
            valueHint: 'Key not found.'
          }))
          setStatus({
            variant: 'warning',
            title: 'Key not found',
            message: `"${trimmed}" is not present in the ${describeScope(scope)}.`
          })
          appendLog(`Attempted to read missing key "${trimmed}".`)
          await refreshKeys()
          return
        }

        const formatted = formatStoredValueForDisplay(value, encoding)
        setInspect((prev) => ({
          ...prev,
          key: trimmed,
          encoding,
          valueText: formatted.text,
          valueHint: formatted.hint
        }))
        setStatus({
          variant: 'success',
          title: 'Value retrieved',
          message: `Read "${trimmed}" using ${encoding} encoding.`
        })
        appendLog(`Fetched key "${trimmed}" (${encoding}).`)
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to read key',
          message
        })
        appendLog(`Failed to read key "${trimmed}": ${message}`)
      } finally {
        setPending((prev) => ({ ...prev, fetch: false }))
      }
    },
    [scope, appendLog, refreshKeys]
  )

  const handleFetch = useCallback(async () => {
    await fetchValue({ key: inspect.key, encoding: inspect.encoding })
  }, [fetchValue, inspect.key, inspect.encoding])

  const handleInspectFromList = useCallback(
    async (key) => {
      setInspect((prev) => ({ ...prev, key }))
      await fetchValue({ key, encoding: inspect.encoding })
    },
    [fetchValue, inspect.encoding]
  )

  const handleRemove = useCallback(
    async (key) => {
      const trimmed = (key || '').trim()
      if (!trimmed) return
      setPending((prev) => ({ ...prev, remove: trimmed }))
      try {
        const options = scope ? { scope } : {}
        await secureStorage.removeItem(trimmed, options)
        setStatus({
          variant: 'success',
          title: 'Key removed',
          message: `Deleted "${trimmed}" from the ${describeScope(scope)}.`
        })
        appendLog(`Removed key "${trimmed}".`)
        setInspect((prev) =>
          prev.key === trimmed
            ? { ...prev, key: '', valueText: '', valueHint: '' }
            : prev
        )
        await refreshKeys()
      } catch (error) {
        const message = formatError(error)
        setStatus({
          variant: 'danger',
          title: 'Failed to remove key',
          message
        })
        appendLog(`Failed to remove key "${trimmed}": ${message}`)
      } finally {
        setPending((prev) => ({ ...prev, remove: null }))
      }
    },
    [scope, appendLog, refreshKeys]
  )

  const handleClearScope = useCallback(async () => {
    setPending((prev) => ({ ...prev, clear: true }))
    try {
      const options = scope ? { scope } : {}
      await secureStorage.clear(options)
      setStatus({
        variant: 'info',
        title: 'Scope cleared',
        message: `All keys removed from the ${describeScope(scope)}.`
      })
      appendLog(`Cleared ${describeScope(scope)}.`)
      setInspect((prev) => ({ ...prev, key: '', valueText: '', valueHint: '' }))
      await refreshKeys()
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Failed to clear scope',
        message
      })
      appendLog(`Failed to clear scope: ${message}`)
    } finally {
      setPending((prev) => ({ ...prev, clear: false }))
    }
  }, [scope, appendLog, refreshKeys])

  const activeScopeLabel =
    scope && scope.length ? scope : `${DEFAULT_SCOPE_LABEL} (default)`

  const keysTable = useMemo(() => {
    if (!keys.length) {
      return h(EmptyState, {
        title: 'No stored entries',
        description:
          'Use the form to write a key/value pair into secure storage.'
      })
    }

    return h(
      Table,
      { className: 'secure-storage__keys' },
      h(
        TableHead,
        null,
        h(
          TableRow,
          null,
          h(TableHeader, null, 'Key'),
          h(TableHeader, null, 'Actions')
        )
      ),
      h(
        TableBody,
        null,
        keys.map((key) =>
          h(
            TableRow,
            { key },
            h(TableCell, null, key),
            h(
              TableCell,
              null,
              h(
                'div',
                { className: 'secure-storage__inline-actions' },
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'ghost',
                    onClick: () => {
                      handleInspectFromList(key).catch(() => {})
                    }
                  },
                  'Inspect'
                ),
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'ghost',
                    onClick: () => {
                      handleRemove(key).catch(() => {})
                    },
                    disabled: pending.remove === key
                  },
                  pending.remove === key ? 'Removing…' : 'Remove'
                )
              )
            )
          )
        )
      )
    )
  }, [keys, handleInspectFromList, handleRemove, pending.remove])

  return h(
    ExampleLayout,
    {
      title: 'Secure Storage Vault',
      description:
        'Interact with the encrypted key-value store supplied by the runtime, swap scopes, and verify persistence.',
      badge: h(Badge, { variant: 'info' }, 'Encrypted at rest')
    },
    h(StatusBanner, {
      variant: status.variant,
      title: status.title,
      message: status.message
    }),
    h(
      ExampleGrid,
      { columns: 2 },
      h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Scope & keys',
            description:
              'Scopes partition storage. Leave the field blank to default to the webview origin.'
          },
          h(
            'form',
            { onSubmit: handleApplyScope },
            h(
              ExampleStack,
              { gap: 'md' },
              h(
                FormField,
                {
                  label: 'Active scope',
                  description: `Current scope: ${describeScope(scope)}`
                },
                h(
                  'div',
                  { className: 'secure-storage__scope-row' },
                  h(Input, {
                    value: scopeInput,
                    placeholder: 'e.g. team-demo, customer-123',
                    onChange: (event) => setScopeInput(event.target.value)
                  }),
                  h(Button, { type: 'submit' }, 'Apply scope'),
                  h(
                    Button,
                    {
                      type: 'button',
                      variant: 'outline',
                      onClick: () => setScopeInput('')
                    },
                    'Reset field'
                  )
                )
              )
            )
          ),
          h(
            'div',
            { className: 'secure-storage__meta' },
            h('span', null, 'Active scope badge:'),
            h(Badge, { variant: 'neutral' }, activeScopeLabel),
            h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                onClick: () => {
                  refreshKeys().catch(() => {})
                },
                disabled: pending.refresh
              },
              pending.refresh ? 'Refreshing…' : 'Refresh keys'
            ),
            h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                onClick: () => {
                  handleClearScope().catch(() => {})
                },
                disabled: pending.clear
              },
              pending.clear ? 'Clearing…' : 'Clear scope'
            )
          ),
          keysTable
        ),
        h(
          ExamplePanel,
          {
            title: 'Store a value',
            description:
              'Values are encrypted by the runtime. Choose an encoding to control how data is serialised.'
          },
          h(
            'form',
            { onSubmit: handleStore },
            h(
              ExampleStack,
              { gap: 'md' },
              h(
                FormField,
                {
                  label: 'Key',
                  required: true
                },
                h(Input, {
                  value: storeForm.key,
                  placeholder: 'e.g. access-token',
                  onChange: (event) =>
                    setStoreForm((prev) => ({
                      ...prev,
                      key: event.target.value
                    }))
                })
              ),
              h(
                FormField,
                {
                  label: 'Value'
                },
                h(Textarea, {
                  rows: 4,
                  value: storeForm.value,
                  onChange: (event) =>
                    setStoreForm((prev) => ({
                      ...prev,
                      value: event.target.value
                    })),
                  placeholder:
                    'Paste a string, base64 payload, or hex-encoded value.'
                })
              ),
              h(
                FormField,
                {
                  label: 'Encoding'
                },
                h(
                  Select,
                  {
                    value: storeForm.encoding,
                    onChange: (event) =>
                      setStoreForm((prev) => ({
                        ...prev,
                        encoding: event.target.value
                      }))
                  },
                  STORE_ENCODINGS.map((option) =>
                    h(
                      Option,
                      { key: option.value, value: option.value },
                      option.label
                    )
                  )
                )
              ),
              h(
                Button,
                {
                  type: 'submit',
                  disabled: pending.store
                },
                pending.store ? 'Storing…' : 'Store value'
              )
            )
          )
        )
      ),
      h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Inspect & retrieve',
            description:
              'Pick a key, choose the read encoding, and fetch the stored payload.'
          },
          h(
            ExampleStack,
            { gap: 'md' },
            h(
              FormField,
              {
                label: 'Key',
                description: 'Select a key from the list or type one manually.',
                required: true
              },
              h(Input, {
                value: inspect.key,
                placeholder: 'e.g. access-token',
                onChange: (event) =>
                  setInspect((prev) => ({ ...prev, key: event.target.value }))
              })
            ),
            h(
              FormField,
              {
                label: 'Read encoding'
              },
              h(
                Select,
                {
                  value: inspect.encoding,
                  onChange: (event) => {
                    const nextEncoding = event.target.value
                    setInspect((prev) => ({ ...prev, encoding: nextEncoding }))
                    if (inspect.key) {
                      fetchValue({
                        key: inspect.key,
                        encoding: nextEncoding
                      }).catch(() => {})
                    }
                  }
                },
                READ_ENCODINGS.map((option) =>
                  h(
                    Option,
                    { key: option.value, value: option.value },
                    option.label
                  )
                )
              )
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => {
                  handleFetch().catch(() => {})
                },
                disabled: pending.fetch
              },
              pending.fetch ? 'Reading…' : 'Read value'
            ),
            h(
              'div',
              { className: 'secure-storage__value' },
              inspect.valueText
                ? h(
                  ExampleStack,
                  { gap: 'sm' },
                  h(
                    'p',
                    {
                      style: {
                        margin: 0,
                        fontSize: '13px',
                        color: 'var(--ui-foreground-muted)'
                      }
                    },
                    'Decoded value (',
                    inspect.encoding,
                    ')'
                  ),
                  h(Code, null, inspect.valueText),
                  inspect.valueHint &&
                      h(
                        'p',
                        {
                          style: {
                            margin: 0,
                            fontSize: '12px',
                            color: 'var(--ui-foreground-muted)'
                          }
                        },
                        inspect.valueHint
                      )
                )
                : h(EmptyState, {
                  title: 'No value loaded yet',
                  description:
                      'Fetch a stored entry to preview its contents.'
                })
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Activity log',
            description:
              'Monitor read/write operations dispatched to secure storage.'
          },
          h(LogViewer, {
            entries: logs,
            emptyState: () =>
              h('span', null, 'Storage operations will appear here.'),
            renderEntry: (entry) => entry.text
          }),
          h(
            'p',
            {
              style: {
                marginTop: '12px',
                fontSize: '13px',
                color: 'var(--ui-foreground-muted)'
              }
            },
            'Under the hood these calls map to the ',
            h(InlineCode, null, 'oro:secure-storage'),
            " module, backed by the runtime's encrypted storage service."
          )
        )
      )
    )
  )
}

mountExample(SecureStorageVault)
