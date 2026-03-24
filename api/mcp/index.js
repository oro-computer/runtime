/**
 * @module mcp
 *
 * Runtime MCP helpers for registering tools/resources and controlling the
 * native HTTP/SSE bridge.
 */

import { Buffer } from '../buffer.js'
import hooks from '../hooks.js'
import { Conduit } from '../conduit.js'
import { rand64 } from '../crypto.js'
import ipc, { IPCSearchParams, Result } from '../ipc.js'

/**
 * @typedef {object} MCPToolInvocationContext
 * @property {string} id Unique invocation identifier provided by the runtime.
 * @property {string} name Registered tool name.
 * @property {string} sessionId Identifier for the originating MCP session.
 * @property {Record<string, any>} arguments Parsed invocation arguments.
 */

/**
 * @typedef {object} MCPResourceDescriptor
 * @property {string} uri Unique resource URI.
 * @property {string} [name]
 * @property {string} [description]
 * @property {string} [mimeType]
 * @property {boolean} [subscribable]
 * @property {any} [metadata]
 */

/**
 * @typedef {object} MCPResourceContext
 * @property {string} id Server-provided identifier for the request/subscription.
 * @property {string} uri Resource URI.
 * @property {string} sessionId Identifier for the originating MCP session.
 * @property {Record<string, any>} params Additional parameters supplied by the client.
 * @property {MCPResourceDescriptor} descriptor Last-known descriptor for the resource.
 */

/**
 * @typedef {object} MCPAuthorizationRequest
 * @property {string} id Unique identifier for this authorization decision.
 * @property {string} method HTTP method used by the client.
 * @property {string} path Resolved path (including endpoint) for the request.
 * @property {string} remoteAddress Remote IP address observed by the runtime.
 * @property {number | null} remotePort Remote port or `null` when unavailable.
 * @property {Record<string, string | string[]>} headers Request headers keyed by name.
 * @property {Record<string, string | string[]>} query Query parameters keyed by name.
 * @property {string} [authorization] Full `Authorization` header when present.
 * @property {string} [body] Raw request body when supplied.
 */

/**
 * @typedef {object} MCPAuthorizationDecision
 * @property {boolean} allow Whether to accept the request.
 * @property {number} [status] Optional HTTP status to return when `allow` is false.
 * @property {string} [message] Optional response body when `allow` is false.
 */

/**
 * @typedef {object} MCPOAuthScreenOptions
 * @property {string} [html] Inline HTML string for the authorization screen.
 * @property {string} [file] Absolute path to a HTML file used for the authorization screen.
 */

/**
 * @typedef {object} MCPOAuthOptions
 * @property {boolean} [enabled] Enable the built-in OAuth flow (defaults to `true` when omitted).
 * @property {string} [issuer] Explicit issuer URL reported in discovery metadata.
 * @property {string} [authorizePath] Override for the authorization endpoint path.
 * @property {string} [tokenPath] Override for the token endpoint path.
 * @property {string} [metadataPath] Override for the OAuth discovery metadata path.
 * @property {number} [codeLifetimeSeconds] Authorization code lifetime override (seconds).
 * @property {number} [tokenLifetimeSeconds] Access token lifetime override (seconds).
 * @property {string} [defaultClientId] Optional client identifier shown on the default screen.
 * @property {string} [defaultScope] Optional scope displayed on the default screen.
 * @property {MCPOAuthScreenOptions} [screen] Custom authorization screen configuration.
 */

/**
 * @typedef {object} MCPRegisterToolOptions
 * @property {string} name
 * @property {string} [description]
 * @property {any} [metadata]
 * @property {Record<string, any>} [inputSchema]
 * @property {(context: MCPToolInvocationContext) => any | Promise<any>} [handler]
 */

/**
 * @typedef {object} MCPRegisterResourceOptions
 * @property {string} uri
 * @property {string} [name]
 * @property {string} [description]
 * @property {string} [mimeType]
 * @property {boolean} [subscribable]
 * @property {any} [metadata]
 * @property {(context: MCPResourceContext) => any | Promise<any>} [handler]
 * @property {(context: MCPResourceContext) => void | Promise<void>} [onSubscribe]
 * @property {(context: MCPResourceContext) => void | Promise<void>} [onUnsubscribe]
 */

/**
 * @typedef {object} MCPStartServerOptions
 * @property {string} [host]
 * @property {number} [port]
 * @property {string} [endpoint]
 * @property {string} [sse]
 * @property {string} [message]
 * @property {string} [token]
 * @property {number} [retry]
 * @property {(request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision>} [authorize]
 * @property {MCPOAuthOptions | boolean} [oauth]
 */

const toolHandlers = new Map()
const resourceHandlers = new Map()
let dataListenerDisposer = null
let authHandler = null
let authHandlerRegistered = false

const textDecoder = new TextDecoder()
const EMPTY_PAYLOAD = new Uint8Array(0)
const conduitPendingRequests = new Map()
let conduitClient = null
let conduitUnavailable = false

function rejectAllPending (error) {
  if (conduitPendingRequests.size === 0) return

  for (const [token, entry] of conduitPendingRequests) {
    conduitPendingRequests.delete(token)
    entry.reject(error)
  }
}

function ensureConduitClient () {
  if (conduitClient || conduitUnavailable) {
    return conduitClient
  }

  try {
    conduitClient = new Conduit({ id: String(rand64()) })
  } catch {
    conduitUnavailable = true
    conduitClient = null
    return conduitClient
  }

  conduitClient.receive((error, message) => {
    if (error) {
      rejectAllPending(
        error instanceof Error ? error : new Error('CONDUIT_CHANNEL_ERROR')
      )
      return
    }

    if (!message) {
      return
    }

    const token = message.options?.token || message.options?.['ipc-token']
    if (!token) {
      handleConduitNotificationMessage(message)
      return
    }

    const entry = conduitPendingRequests.get(token)
    if (!entry) {
      handleConduitNotificationMessage(message)
      return
    }

    conduitPendingRequests.delete(token)
    entry.resolve(
      decodeConduitResult(message.payload ?? EMPTY_PAYLOAD, entry.route)
    )
  })

  const handleChannelError = () => {
    rejectAllPending(new Error('CONDUIT_CHANNEL_ERROR'))
  }

  conduitClient.addEventListener('close', handleChannelError)
  conduitClient.addEventListener('error', handleChannelError)

  return conduitClient
}

function handleConduitNotificationMessage (message) {
  if (!message) return

  const route = message.options?.route || null
  const payload = message.payload ?? EMPTY_PAYLOAD
  const result = decodeConduitResult(payload, route)
  const source = result.source || route
  if (!source) return

  let raw = null
  if (payload instanceof Uint8Array && payload.length > 0) {
    try {
      const text = textDecoder.decode(payload)
      if (text.length > 0) {
        raw = JSON.parse(text)
      }
    } catch {}
  }

  const rawObject = raw && typeof raw === 'object' ? raw : null
  const rawData =
    rawObject && rawObject.data ? assertObject(rawObject.data) : null
  let params = { ...assertObject(rawData || result.data) }

  if (Object.keys(params).length === 0 && rawObject && rawObject.err) {
    const rawErr = assertObject(rawObject.err)
    params = { ...rawErr }
    const nested = rawErr?.error
    if (nested && typeof nested === 'object') {
      params.error = { ...assertObject(nested) }
    }
  }

  if (result.err) {
    const extra = {
      message: result.err.message ?? '',
      name: result.err.name ?? '',
      code: result.err.code ?? ''
    }
    const existingError = assertObject(params.error)
    const merged = {
      ...existingError,
      ...(extra.message ? { message: extra.message } : {}),
      ...(extra.name ? { name: extra.name } : {}),
      ...(extra.code ? { code: extra.code } : {})
    }
    if (Object.keys(merged).length > 0) {
      params.error = merged
      if (!params.err) {
        params.err = merged
      }
    }
  }

  if (Object.keys(params).length === 0) {
    return
  }

  switch (source) {
    case 'mcp.tool.invoke':
    case 'mcp.server.invokeTool':
    case 'mcp.server.registerTool':
      handleToolInvocation(params)
      return
    case 'mcp.server.registerResource':
    case 'mcp.resource.read':
      handleResourceRead(params)
      return
    case 'mcp.resource.subscribe':
      handleResourceSubscribe(params)
      return
    case 'mcp.resource.unsubscribe':
      handleResourceUnsubscribe(params)
      return
    case 'mcp.server.authorize':
      handleAuthorizationRequest(params)
      break

    default:
      break
  }
}

function waitForConduitOpen (client, timeoutMs = 1000) {
  if (client.isActive) {
    return Promise.resolve(true)
  }

  return new Promise((resolve, reject) => {
    let settled = false
    let timeoutId = null

    const cleanup = () => {
      if (timeoutId !== null) {
        clearTimeout(timeoutId)
      }
      client.removeEventListener('open', onOpen)
      client.removeEventListener('error', onError)
      client.removeEventListener('close', onClose)
    }

    const onOpen = () => {
      if (settled) return
      settled = true
      cleanup()
      resolve(true)
    }

    const onError = () => {
      if (settled) return
      settled = true
      cleanup()
      reject(new Error('CONDUIT_CHANNEL_ERROR'))
    }

    const onClose = () => {
      if (settled) return
      settled = true
      cleanup()
      reject(new Error('CONDUIT_CHANNEL_ERROR'))
    }

    client.addEventListener('open', onOpen, { once: true })
    client.addEventListener('error', onError, { once: true })
    client.addEventListener('close', onClose, { once: true })

    if (Number.isFinite(timeoutMs) && timeoutMs > 0) {
      timeoutId = setTimeout(() => {
        if (settled) return
        settled = true
        cleanup()
        reject(new Error('CONDUIT_TIMEOUT'))
      }, timeoutMs)

      if (typeof timeoutId?.unref === 'function') {
        timeoutId.unref()
      }
    }
  })
}

function buildRouteOptions (command, value, token, conduitId = null) {
  const params = new IPCSearchParams(value, Date.now())
  const options = { route: command, token }
  options['ipc-token'] = token
  if (conduitId !== null && conduitId !== undefined) {
    options.__conduit_id = String(conduitId)
  }
  for (const [key, paramValue] of params.entries()) {
    options[key] = paramValue
  }
  return options
}

function decodeConduitResult (payload, route) {
  if (!(payload instanceof Uint8Array) || payload.length === 0) {
    return Result.from(null, null, route)
  }

  try {
    const text = textDecoder.decode(payload)
    if (text.length === 0) {
      return Result.from(null, null, route)
    }

    try {
      const parsed = JSON.parse(text)
      return Result.from(parsed, null, route)
    } catch {
      return Result.from(text, null, route)
    }
  } catch {
    return Result.from(payload.slice(), null, route)
  }
}

async function runtimeRequest (command, value, options = null) {
  const fallback = () => ipc.request(command, value, options)

  const client = ensureConduitClient()
  if (!client) {
    return fallback()
  }

  let active = client.isActive
  if (!active) {
    active = await waitForConduitOpen(client).catch(() => false)
  }

  if (!active) {
    return fallback()
  }

  const token = String(rand64())
  const conduitId =
    client && typeof client.id !== 'undefined' && client.id !== null
      ? String(client.id)
      : null
  const routeOptions = buildRouteOptions(command, value, token, conduitId)

  const promise = new Promise((resolve, reject) => {
    const entry = {
      route: command,
      timeout: null,
      resolve: (result) => {
        if (entry.timeout !== null) {
          clearTimeout(entry.timeout)
        }
        resolve(result)
      },
      reject: (error) => {
        if (entry.timeout !== null) {
          clearTimeout(entry.timeout)
        }
        reject(error)
      }
    }

    const timeoutMs = options?.timeout ?? 10000
    if (Number.isFinite(timeoutMs) && timeoutMs > 0) {
      entry.timeout = setTimeout(() => {
        conduitPendingRequests.delete(token)
        entry.reject(new Error('CONDUIT_TIMEOUT'))
      }, timeoutMs)
      if (typeof entry.timeout?.unref === 'function') {
        entry.timeout.unref()
      }
    }

    conduitPendingRequests.set(token, entry)
  })

  let sent = client.send(routeOptions, EMPTY_PAYLOAD)

  if (!sent) {
    const reopened = await waitForConduitOpen(
      client,
      options?.timeout ?? 1000
    ).catch(() => false)
    if (reopened) {
      sent = client.send(routeOptions, EMPTY_PAYLOAD)
    }
  }

  if (!sent) {
    const entry = conduitPendingRequests.get(token)
    if (entry?.timeout !== null) {
      clearTimeout(entry.timeout)
    }
    conduitPendingRequests.delete(token)
    return fallback()
  }

  try {
    return await promise
  } catch (error) {
    if (
      error &&
      (error.message === 'CONDUIT_TIMEOUT' ||
        error.message === 'CONDUIT_CHANNEL_ERROR')
    ) {
      return fallback()
    }
    return fallback()
  }
}

function normalizeMetadata (value) {
  if (value === undefined) return undefined
  if (value === null) return null
  if (typeof value === 'string' && value.length > 0) {
    try {
      return JSON.parse(value)
    } catch {}
  }
  return value
}

function normalizeToolDefinition (tool) {
  const value = assertObject(tool)
  return {
    name: value?.name ?? '',
    description: value?.description ?? '',
    inputSchema: assertObject(value?.inputSchema),
    metadata: normalizeMetadata(value?.metadata)
  }
}

function normalizeDescriptor (descriptor) {
  const value = { ...assertObject(descriptor) }
  if (value.metadata !== undefined) {
    value.metadata = normalizeMetadata(value.metadata)
  }
  return value
}

function getResourceHandlerEntry (uri) {
  const entry = resourceHandlers.get(uri)
  if (!entry) return null
  if (typeof entry === 'function') {
    return {
      read: entry,
      subscribe: null,
      unsubscribe: null,
      descriptor: null
    }
  }
  return entry
}

function storeResourceHandlerEntry (uri, entry) {
  resourceHandlers.set(uri, entry)
  return entry
}

function updateResourceDescriptor (uri, descriptor) {
  const entry = getResourceHandlerEntry(uri)
  if (!entry) return
  entry.descriptor = normalizeDescriptor(descriptor)
  resourceHandlers.set(uri, entry)
}

function ensureDataListener () {
  if (dataListenerDisposer) return

  const remove = hooks.onData((event) => {
    const params = event?.detail?.params
    if (!params) return

    if (params.source === 'mcp.tool.invoke') {
      handleToolInvocation(params.data)
      return
    }

    if (params.source === 'mcp.server.invokeTool') {
      handleToolInvocation(params.data)
      return
    }

    if (params.source === 'mcp.server.registerTool') {
      handleToolInvocation(params.data)
      return
    }

    if (params.source === 'mcp.server.registerResource') {
      handleResourceRead(params.data)
      return
    }

    if (params.source === 'mcp.resource.read') {
      handleResourceRead(params.data)
      return
    }

    if (params.source === 'mcp.resource.subscribe') {
      handleResourceSubscribe(params.data)
      return
    }

    if (params.source === 'mcp.resource.unsubscribe') {
      handleResourceUnsubscribe(params.data)
    }

    if (params.source === 'mcp.server.authorize') {
      handleAuthorizationRequest(params.data)
    }
  })

  dataListenerDisposer = () => {
    try {
      remove?.()
    } catch {}
    dataListenerDisposer = null
  }
}

function cleanupDataListener () {
  if (
    toolHandlers.size === 0 &&
    resourceHandlers.size === 0 &&
    !authHandler &&
    dataListenerDisposer
  ) {
    dataListenerDisposer()
  }
}

function handleToolInvocation (payload) {
  const data = assertObject(payload)
  const invocationId = data.id
  const toolName = data.tool
  const sessionId = data.sessionId || ''
  const argumentsJson = data.arguments || ''

  if (!invocationId || !toolName) return

  const handler = toolHandlers.get(toolName)
  if (typeof handler !== 'function') {
    runtimeRequest('mcp.server.rejectInvocation', {
      id: invocationId,
      message: `No handler registered for tool: ${toolName}`
    }).catch(() => {})
    return
  }

  let args = {}
  if (typeof argumentsJson === 'string' && argumentsJson.length > 0) {
    try {
      args = JSON.parse(argumentsJson)
    } catch (error) {
      runtimeRequest('mcp.server.rejectInvocation', {
        id: invocationId,
        message: error?.message || 'Failed to parse tool arguments'
      }).catch(() => {})
      return
    }
  }

  const context = {
    id: invocationId,
    name: toolName,
    sessionId,
    arguments: args
  }

  Promise.resolve()
    .then(() => handler(context))
    .then((result) => {
      let resultJson = 'null'
      try {
        resultJson = result === undefined ? 'null' : JSON.stringify(result)
      } catch (error) {
        return runtimeRequest('mcp.server.rejectInvocation', {
          id: invocationId,
          message: error?.message || 'Failed to serialize tool result'
        })
      }

      return runtimeRequest('mcp.server.resolveInvocation', {
        id: invocationId,
        result: resultJson
      })
    })
    .catch((error) =>
      runtimeRequest('mcp.server.rejectInvocation', {
        id: invocationId,
        message: error?.message || String(error)
      })
    )
    .catch(() => {})
}

function handleResourceRead (payload) {
  const data = assertObject(payload)
  const invocationId = data.id
  const resourceUri = data.resource
  const sessionId = data.sessionId || ''
  if (!invocationId || !resourceUri) return

  const entry = getResourceHandlerEntry(resourceUri)
  const handler = entry?.read
  if (typeof handler !== 'function') {
    runtimeRequest('mcp.server.rejectResource', {
      id: invocationId,
      message: `No handler registered for resource: ${resourceUri}`
    }).catch(() => {})
    return
  }

  let params = {}
  if (typeof data.params === 'string') {
    try {
      params = JSON.parse(data.params)
    } catch {}
  } else if (data.params && typeof data.params === 'object') {
    params = data.params
  }

  const descriptor = normalizeDescriptor(data.descriptor)
  updateResourceDescriptor(resourceUri, descriptor)
  const context = {
    id: invocationId,
    uri: resourceUri,
    sessionId,
    params,
    descriptor
  }

  Promise.resolve()
    .then(() => handler(context))
    .then((result) => {
      const normalized = normalizeResourceResult(result, descriptor)
      return runtimeRequest('mcp.server.resolveResource', {
        id: invocationId,
        result: JSON.stringify(normalized)
      })
    })
    .catch((error) =>
      runtimeRequest('mcp.server.rejectResource', {
        id: invocationId,
        message: error?.message || String(error)
      })
    )
    .catch(() => {})
}

function handleResourceSubscribe (payload) {
  const data = assertObject(payload)
  const subscriptionId = data.id
  const resourceUri = data.resource
  const sessionId = data.sessionId || ''
  if (!subscriptionId || !resourceUri) return

  const entry = getResourceHandlerEntry(resourceUri)
  if (!entry) {
    return
  }

  let params = {}
  if (typeof data.params === 'string') {
    try {
      params = JSON.parse(data.params)
    } catch {}
  } else if (data.params && typeof data.params === 'object') {
    params = data.params
  }

  const descriptor = normalizeDescriptor(data.descriptor)
  updateResourceDescriptor(resourceUri, descriptor)

  if (typeof entry.subscribe === 'function') {
    const context = {
      id: subscriptionId,
      uri: resourceUri,
      sessionId,
      params,
      descriptor
    }

    Promise.resolve()
      .then(() => entry.subscribe(context))
      .catch(() => {})
  }
}

function handleResourceUnsubscribe (payload) {
  const data = assertObject(payload)
  const subscriptionId = data.id
  const resourceUri = data.resource
  const sessionId = data.sessionId || ''
  if (!subscriptionId || !resourceUri) return

  const entry = getResourceHandlerEntry(resourceUri)
  if (!entry) {
    return
  }

  if (typeof entry.unsubscribe === 'function') {
    let params = {}
    if (typeof data.params === 'string') {
      try {
        params = JSON.parse(data.params)
      } catch {}
    } else if (data.params && typeof data.params === 'object') {
      params = data.params
    }

    const descriptor = normalizeDescriptor(data.descriptor)

    const context = {
      id: subscriptionId,
      uri: resourceUri,
      sessionId,
      params,
      descriptor
    }

    Promise.resolve()
      .then(() => entry.unsubscribe(context))
      .catch(() => {})
  }
}

function normalizePairs (entries) {
  if (!Array.isArray(entries)) return {}

  const result = {}
  for (const entry of entries) {
    if (!entry || typeof entry !== 'object') continue
    const key =
      typeof entry.name === 'string'
        ? entry.name
        : typeof entry.key === 'string'
          ? entry.key
          : null
    if (!key) continue

    const value =
      entry.value === undefined || entry.value === null
        ? ''
        : String(entry.value)

    if (Object.prototype.hasOwnProperty.call(result, key)) {
      const existing = result[key]
      if (Array.isArray(existing)) {
        existing.push(value)
      } else {
        result[key] = [existing, value]
      }
    } else {
      result[key] = value
    }
  }

  return result
}

function handleAuthorizationRequest (payload) {
  const data = assertObject(payload)
  const id = data.id
  if (!id) return

  const headers = normalizePairs(data.headers)
  const query = normalizePairs(data.query)

  const context = {
    id,
    method: data.method || 'GET',
    path: data.path || '/',
    remoteAddress: data.remoteAddress || '',
    remotePort: typeof data.remotePort === 'number' ? data.remotePort : null,
    headers,
    query
  }

  if (typeof data.authorization === 'string') {
    context.authorization = data.authorization
  }
  if (typeof data.body === 'string') {
    context.body = data.body
  }

  const handler = authHandler
  if (typeof handler !== 'function') {
    runtimeRequest('mcp.server.resolveAuthorization', {
      id,
      allow: false,
      message: 'Unauthorized'
    }).catch(() => {})
    return
  }

  Promise.resolve()
    .then(() => handler(context))
    .then((result) => {
      let allow = Boolean(result)
      let status
      let message

      if (result && typeof result === 'object') {
        if ('allow' in result) allow = Boolean(result.allow)
        if (Number.isFinite(result.status)) status = String(result.status)
        if (typeof result.message === 'string') message = result.message
      }

      const payload = {
        id,
        allow
      }

      if (status !== undefined) payload.status = status
      if (message) payload.message = message

      return runtimeRequest('mcp.server.resolveAuthorization', payload)
    })
    .catch((error) =>
      runtimeRequest('mcp.server.resolveAuthorization', {
        id,
        allow: false,
        message: error?.message || 'Unauthorized'
      })
    )
    .catch(() => {})
}

function normalizeOAuthOptions (options) {
  if (options === undefined || options === null) {
    return null
  }

  if (options === false) {
    return { enabled: false }
  }

  const source = options === true ? {} : options
  if (typeof source !== 'object') {
    return null
  }

  const normalized = {}
  normalized.enabled =
    source.enabled !== undefined ? Boolean(source.enabled) : true

  const stringFields = [
    ['issuer', 'issuer'],
    ['authorizePath', 'authorizePath'],
    ['tokenPath', 'tokenPath'],
    ['metadataPath', 'metadataPath'],
    ['defaultClientId', 'defaultClientId'],
    ['defaultScope', 'defaultScope']
  ]

  for (const [key, target] of stringFields) {
    const value = source[key]
    if (typeof value === 'string' && value.length > 0) {
      normalized[target] = value
    }
  }

  const numberFields = [
    ['codeLifetimeSeconds', 'codeLifetimeSeconds'],
    ['tokenLifetimeSeconds', 'tokenLifetimeSeconds']
  ]

  for (const [key, target] of numberFields) {
    const value = source[key]
    if (Number.isFinite(value) && value > 0) {
      normalized[target] = Math.floor(value)
    }
  }

  const screen = source.screen
  if (screen && typeof screen === 'object') {
    const screenConfig = {}
    if (typeof screen.html === 'string' && screen.html.length > 0) {
      screenConfig.html = screen.html
    }
    if (typeof screen.file === 'string' && screen.file.length > 0) {
      screenConfig.file = screen.file
    }
    if (Object.keys(screenConfig).length > 0) {
      normalized.screen = screenConfig
    }
  }

  return normalized
}

function normalizeResourceResult (result, descriptor) {
  const normalizedDescriptor = normalizeDescriptor(descriptor)
  const mimeType = normalizedDescriptor?.mimeType || 'application/octet-stream'

  if (result == null) {
    return { contents: [] }
  }

  if (typeof result === 'string') {
    return {
      contents: [{ type: 'text', text: result }]
    }
  }

  if (Buffer.isBuffer(result) || result instanceof Uint8Array) {
    const base64 = Buffer.from(result).toString('base64')
    return {
      contents: [{ type: 'blob', blob: base64, mimeType }]
    }
  }

  if (Array.isArray(result)) {
    return { contents: result }
  }

  if (typeof result === 'object') {
    if (Array.isArray(result.contents)) {
      return { contents: result.contents }
    }

    if (typeof result.text === 'string') {
      return { contents: [{ type: 'text', text: result.text }] }
    }

    if (Buffer.isBuffer(result.blob) || result.blob instanceof Uint8Array) {
      const base64 = Buffer.from(result.blob).toString('base64')
      return {
        contents: [
          { type: 'blob', blob: base64, mimeType: result.mimeType || mimeType }
        ]
      }
    }

    if (typeof result.blob === 'string') {
      return {
        contents: [
          {
            type: 'blob',
            blob: result.blob,
            mimeType: result.mimeType || mimeType
          }
        ]
      }
    }
  }

  throw new TypeError('Unsupported resource handler result')
}

function assertObject (value, fallback = {}) {
  return value && typeof value === 'object' ? value : fallback
}

function resultData (result) {
  if (result?.err) throw result.err
  return assertObject(result?.data)
}

function serializeForIPC (value, label) {
  if (value === undefined) {
    return ''
  }

  try {
    return JSON.stringify(value)
  } catch (error) {
    throw new TypeError(error?.message || `Failed to serialize ${label}`)
  }
}

/**
 * Register a tool that can be invoked by MCP clients.
 * @param {MCPRegisterToolOptions} tool
 * @returns {Promise<number|null>}
 */
export async function registerTool (tool) {
  if (!tool || typeof tool.name !== 'string' || tool.name.length === 0) {
    throw new TypeError('tool.name must be a non-empty string')
  }

  const metadataJson = serializeForIPC(tool.metadata, 'tool metadata')
  const inputSchemaJson = serializeForIPC(tool.inputSchema, 'tool input schema')

  const payload = {
    name: tool.name,
    description: tool.description || '',
    metadata: metadataJson
  }

  if (inputSchemaJson.length > 0) {
    payload.inputSchema = inputSchemaJson
  }

  const result = await runtimeRequest('mcp.server.registerTool', payload)

  const data = resultData(result)
  if (typeof tool.handler === 'function') {
    ensureDataListener()
    toolHandlers.set(tool.name, tool.handler)
  } else {
    toolHandlers.delete(tool.name)
    cleanupDataListener()
  }
  return data.id ?? null
}

export async function unregisterTool (name) {
  if (typeof name !== 'string' || name.length === 0) {
    throw new TypeError('name must be a non-empty string')
  }

  const result = await runtimeRequest('mcp.server.unregisterTool', { name })
  const data = resultData(result)
  if (data.removed) {
    toolHandlers.delete(name)
    cleanupDataListener()
    return true
  }
  return false
}

export async function listTools () {
  const result = await runtimeRequest('mcp.server.listTools')
  const data = resultData(result)
  const tools = Array.isArray(data.tools) ? data.tools : []
  return tools.map(normalizeToolDefinition)
}

/**
 * Register a resource that can be read or subscribed to by MCP clients.
 * @param {MCPRegisterResourceOptions} resource
 * @returns {Promise<number|null>}
 */
export async function registerResource (resource) {
  if (
    !resource ||
    typeof resource.uri !== 'string' ||
    resource.uri.length === 0
  ) {
    throw new TypeError('resource.uri must be a non-empty string')
  }

  const metadataJson = serializeForIPC(resource.metadata, 'resource metadata')

  const hasReadHandler = typeof resource.handler === 'function'
  const hasSubscribeHandler = typeof resource.onSubscribe === 'function'
  const hasUnsubscribeHandler = typeof resource.onUnsubscribe === 'function'
  const subscribable =
    resource.subscribable === undefined
      ? hasSubscribeHandler
      : Boolean(resource.subscribable)

  const result = await runtimeRequest('mcp.server.registerResource', {
    uri: resource.uri,
    name: resource.name || '',
    description: resource.description || '',
    mimeType: resource.mimeType || '',
    subscribable: subscribable ? 'true' : 'false',
    metadata: metadataJson
  })

  const data = resultData(result)
  if (hasReadHandler || hasSubscribeHandler || hasUnsubscribeHandler) {
    ensureDataListener()
    storeResourceHandlerEntry(resource.uri, {
      read: hasReadHandler ? resource.handler : null,
      subscribe: hasSubscribeHandler ? resource.onSubscribe : null,
      unsubscribe: hasUnsubscribeHandler ? resource.onUnsubscribe : null,
      descriptor: normalizeDescriptor({
        uri: resource.uri,
        mimeType: resource.mimeType || '',
        metadata: resource.metadata
      })
    })
  } else {
    resourceHandlers.delete(resource.uri)
    cleanupDataListener()
  }
  return data.id ?? null
}

export async function unregisterResource (uri) {
  if (typeof uri !== 'string' || uri.length === 0) {
    throw new TypeError('uri must be a non-empty string')
  }

  const result = await runtimeRequest('mcp.server.unregisterResource', { uri })
  const data = resultData(result)
  if (data.removed) {
    resourceHandlers.delete(uri)
    cleanupDataListener()
    return true
  }
  return false
}

export async function listResources () {
  const result = await runtimeRequest('mcp.server.listResources')
  const data = resultData(result)
  const resources = Array.isArray(data.resources) ? data.resources : []
  return resources.map((resource) => ({
    uri: resource?.uri ?? '',
    name: resource?.name ?? '',
    description: resource?.description ?? '',
    mimeType: resource?.mimeType ?? '',
    subscribable: Boolean(resource?.subscribable),
    metadata: normalizeMetadata(resource?.metadata)
  }))
}

export async function invokeTool (name, args = {}, options = null) {
  if (typeof name !== 'string' || name.length === 0) {
    throw new TypeError('name must be a non-empty string')
  }

  let argumentsJson = ''
  if (args !== undefined) {
    try {
      argumentsJson = JSON.stringify(args)
    } catch (error) {
      throw new TypeError(
        error?.message || 'Failed to serialize tool arguments'
      )
    }
  }

  const payload = {
    name,
    sessionId: options?.sessionId || '',
    arguments: argumentsJson
  }

  const result = await runtimeRequest('mcp.server.invokeTool', payload)
  const data = resultData(result)
  return Boolean(data.accepted)
}

export async function publishResource (uri, result, options = null) {
  if (typeof uri !== 'string' || uri.length === 0) {
    throw new TypeError('uri must be a non-empty string')
  }

  const descriptor = getResourceHandlerEntry(uri)?.descriptor || null
  let normalized
  try {
    normalized = normalizeResourceResult(result, descriptor)
  } catch (error) {
    throw new TypeError(
      error?.message || 'Failed to normalize resource payload'
    )
  }

  let resultJson = ''
  try {
    resultJson = JSON.stringify(normalized)
  } catch (error) {
    throw new TypeError(
      error?.message || 'Failed to serialize resource payload'
    )
  }

  const payload = {
    uri,
    result: resultJson
  }

  if (options?.sessionId) {
    payload.sessionId = options.sessionId
  }

  if (options?.subscriptionId) {
    payload.subscriptionId = options.subscriptionId
  }

  const response = await runtimeRequest('mcp.server.publishResource', payload)
  const data = resultData(response)
  return Boolean(data.delivered)
}

/**
 * Configure a runtime authorization handler for incoming MCP HTTP requests.
 * Pass a function to enable dynamic authorization or `null`/`undefined` to clear.
 * The handler can return a boolean or an {@link MCPAuthorizationDecision} object.
 *
 * @param {(request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision> | null | undefined} handler
 * @returns {Promise<void>}
 */
export async function setAuthorizationHandler (handler) {
  if (
    handler !== undefined &&
    handler !== null &&
    typeof handler !== 'function'
  ) {
    throw new TypeError('handler must be a function, null, or undefined')
  }

  const previousHandler = authHandler
  const previouslyRegistered = authHandlerRegistered

  authHandler = typeof handler === 'function' ? handler : null

  try {
    if (authHandler && !authHandlerRegistered) {
      await runtimeRequest('mcp.server.setAuthHandler')
      authHandlerRegistered = true
    } else if (!authHandler && authHandlerRegistered) {
      await runtimeRequest('mcp.server.clearAuthHandler')
      authHandlerRegistered = false
    }
    if (authHandler) {
      ensureDataListener()
    } else {
      cleanupDataListener()
    }
  } catch (error) {
    authHandler = previousHandler
    authHandlerRegistered = previouslyRegistered
    throw error
  }
}

/**
 * Start the embedded MCP HTTP/SSE bridge.
 *
 * - Supplying `port: 0` binds to an ephemeral port and the resolved value is
 *   returned in the result.
 * - `authorize` registers a dynamic authorization handler for this server.
 *
 * @param {MCPStartServerOptions} [options]
 * @returns {Promise<{ running: boolean, host: string, port: number, endpoint: string, oauth?: { authorizePath?: string | null, tokenPath?: string | null, metadataPath?: string | null } }>}
 */
export async function startServer (options = null) {
  if (options && Object.prototype.hasOwnProperty.call(options, 'authorize')) {
    await setAuthorizationHandler(options.authorize)
  }

  const payload = {}
  if (options?.host) payload.host = options.host
  if (Number.isFinite(options?.port)) payload.port = String(options.port)
  if (options?.endpoint) payload.endpoint = options.endpoint
  if (options?.sse) payload.sse = options.sse
  if (options?.message) payload.message = options.message
  if (options?.token) payload.token = options.token
  if (Number.isFinite(options?.retry)) payload.retry = String(options.retry)
  const oauthOptions = normalizeOAuthOptions(options?.oauth)
  if (oauthOptions) {
    payload.oauth = JSON.stringify(oauthOptions)
  }

  const result = await runtimeRequest('mcp.server.start', payload)
  const data = resultData(result)
  const endpoint =
    data.endpoint || payload.endpoint || options?.endpoint || '/mcp'
  const host = data.host || payload.host || '127.0.0.1'
  const port = Number(data.port ?? options?.port ?? 8000)

  const resolveOAuthPath = (key, fallback) => {
    const value =
      typeof data[key] === 'string' && data[key].length > 0
        ? data[key]
        : typeof fallback === 'string' && fallback.length > 0
          ? fallback
          : null
    return value
  }

  const normalizedOAuth = oauthOptions || null
  const oauth = {
    authorizePath: resolveOAuthPath(
      'oauthAuthorizePath',
      normalizedOAuth?.authorizePath
    ),
    tokenPath: resolveOAuthPath('oauthTokenPath', normalizedOAuth?.tokenPath),
    metadataPath: resolveOAuthPath(
      'oauthMetadataPath',
      normalizedOAuth?.metadataPath
    )
  }
  const hasOAuthPaths = Object.values(oauth).some(
    (value) => typeof value === 'string' && value.length > 0
  )

  return {
    running: Boolean(data.running),
    host,
    port,
    endpoint,
    ...(hasOAuthPaths ? { oauth } : {})
  }
}

export async function stopServer () {
  const result = await runtimeRequest('mcp.server.stop')
  const data = resultData(result)
  return Boolean(data.running) === false
}

export async function serverStatus () {
  const result = await runtimeRequest('mcp.server.status')
  const data = resultData(result)
  return Boolean(data.running)
}

export default {
  registerTool,
  unregisterTool,
  listTools,
  registerResource,
  unregisterResource,
  listResources,
  invokeTool,
  publishResource,
  setAuthorizationHandler,
  startServer,
  stopServer,
  serverStatus
}
