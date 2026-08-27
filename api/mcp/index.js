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
 * @typedef {object} MCPIcon
 * @property {string} src URI or data URI for the icon.
 * @property {string} [mimeType] MIME type of the icon.
 * @property {string[]} [sizes] Available sizes, such as `48x48` or `any`.
 * @property {'light'|'dark'} [theme] Optional color-scheme hint.
 */

/**
 * @typedef {object} MCPToolAnnotations
 * @property {string} [title]
 * @property {boolean} [readOnlyHint]
 * @property {boolean} [destructiveHint]
 * @property {boolean} [idempotentHint]
 * @property {boolean} [openWorldHint]
 */

/**
 * @typedef {object} MCPAnnotations
 * @property {('user'|'assistant')[]} [audience]
 * @property {number} [priority] Importance from 0 through 1.
 * @property {string} [lastModified] ISO 8601 last-modified timestamp.
 */

/**
 * @typedef {object} MCPTextContent
 * @property {'text'} type
 * @property {string} text
 * @property {MCPAnnotations} [annotations]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {object} MCPBinaryContent
 * @property {'image'|'audio'} type
 * @property {string} data Base64-encoded content.
 * @property {string} mimeType
 * @property {MCPAnnotations} [annotations]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {object} MCPResourceLinkContent
 * @property {'resource_link'} type
 * @property {string} name
 * @property {string} uri
 * @property {string} [title]
 * @property {string} [description]
 * @property {string} [mimeType]
 * @property {number} [size]
 * @property {MCPIcon[]} [icons]
 * @property {MCPAnnotations} [annotations]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {object} MCPResourceContents
 * @property {string} uri
 * @property {string} [text]
 * @property {string} [blob] Base64-encoded bytes.
 * @property {string} [mimeType]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {object} MCPResourceContentInput
 * @property {string} [uri] Defaults to the registered resource URI.
 * @property {string} [text]
 * @property {string|Uint8Array} [blob] Base64 string or bytes.
 * @property {string} [mimeType]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {object} MCPEmbeddedResourceContent
 * @property {'resource'} type
 * @property {MCPResourceContents} resource
 * @property {MCPAnnotations} [annotations]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {MCPTextContent|MCPBinaryContent|MCPResourceLinkContent|MCPEmbeddedResourceContent} MCPContentBlock
 */

/**
 * @typedef {string|number|boolean|null|MCPJSONValue[]|{[key: string]: MCPJSONValue}} MCPJSONValue
 */

/**
 * @typedef {object} MCPToolResult
 * @property {MCPContentBlock[]} content
 * @property {MCPJSONValue} [structuredContent]
 * @property {boolean} [isError]
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {MCPToolResult|MCPJSONValue|undefined} MCPToolHandlerResult
 * A handler may return a complete MCP result or any JSON value. Direct JSON
 * values become `structuredContent` and receive a serialized text content block.
 */

/**
 * @typedef {object} MCPResourceDescriptor
 * @property {string} uri Unique resource URI.
 * @property {string} [name]
 * @property {string} [title]
 * @property {string} [description]
 * @property {string} [mimeType]
 * @property {MCPIcon[]} [icons]
 * @property {MCPAnnotations} [annotations]
 * @property {number} [size] Size of the raw resource content in bytes.
 * @property {Record<string, any>} [_meta] Protocol and application metadata.
 * @property {boolean} [subscribable]
 * @property {any} [metadata] Deprecated alias for `_meta`.
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
 * @property {string} [html] Inline HTML for the authorization screen. Approval forms must submit `decision` and the `{{AUTHORIZATION_REQUEST}}` placeholder value as `authorization_request`.
 * @property {string} [file] Absolute path to an HTML authorization screen with the same one-time request handling as `html`.
 */

/**
 * @typedef {object} MCPOAuthOptions
 * @property {boolean} [enabled] Enable the built-in OAuth flow (defaults to `true` when omitted).
 * @property {string} [issuer] Explicit authorization-server issuer URL reported in discovery metadata. Required when exposing a non-loopback server through a proxy.
 * @property {string} [resource] Canonical public URI of the MCP endpoint. Required when its public URI differs from the bound host and endpoint.
 * @property {string} [authorizePath] Override for the authorization endpoint path.
 * @property {string} [tokenPath] Override for the token endpoint path.
 * @property {string} [metadataPath] Override for the OAuth discovery metadata path.
 * @property {number} [codeLifetimeSeconds] Authorization code lifetime override (seconds).
 * @property {number} [tokenLifetimeSeconds] Access token lifetime override (seconds).
 * @property {string} [defaultClientId] Pre-registered client identifier. Required when OAuth is enabled.
 * @property {string} [defaultScope] Optional scope displayed on the default screen.
 * @property {string[]} [redirectUris] Exact pre-registered redirect URIs accepted for the client. At least one is required when OAuth is enabled.
 * @property {MCPOAuthScreenOptions} [screen] Custom authorization screen configuration.
 */

/**
 * @typedef {object} MCPRegisterToolOptions
 * @property {string} name
 * @property {string} [title]
 * @property {string} [description]
 * @property {Record<string, any>} [inputSchema]
 * @property {Record<string, any>} [outputSchema] A valid JSON Schema. Defaults to dialect 2020-12 when `$schema` is omitted.
 * @property {MCPIcon[]} [icons]
 * @property {MCPToolAnnotations} [annotations]
 * @property {Record<string, any>} [_meta] Protocol and application metadata.
 * @property {any} [metadata] Deprecated alias for `_meta`.
 * @property {(context: MCPToolInvocationContext) => MCPToolHandlerResult | Promise<MCPToolHandlerResult>} [handler]
 */

/**
 * @typedef {object} MCPToolDescriptor
 * @property {string} name
 * @property {string} [title]
 * @property {string} [description]
 * @property {Record<string, any>} inputSchema
 * @property {Record<string, any>} [outputSchema]
 * @property {MCPIcon[]} [icons]
 * @property {MCPToolAnnotations} [annotations]
 * @property {Record<string, any>} [_meta]
 * @property {Record<string, any>} [metadata] Deprecated alias for `_meta`.
 */

/**
 * @typedef {object} MCPRegisterResourceOptions
 * @property {string} uri
 * @property {string} [name]
 * @property {string} [title]
 * @property {string} [description]
 * @property {string} [mimeType]
 * @property {MCPIcon[]} [icons]
 * @property {MCPAnnotations} [annotations]
 * @property {number} [size] Size of the raw resource content in bytes.
 * @property {boolean} [subscribable]
 * @property {Record<string, any>} [_meta] Protocol and application metadata.
 * @property {any} [metadata] Deprecated alias for `_meta`.
 * @property {(context: MCPResourceContext) => MCPResourceHandlerResult | Promise<MCPResourceHandlerResult>} [handler]
 * @property {(context: MCPResourceContext) => void | Promise<void>} [onSubscribe]
 * @property {(context: MCPResourceContext) => void | Promise<void>} [onUnsubscribe]
 */

/**
 * @typedef {object} MCPResourceHandlerResultObject
 * @property {MCPResourceContentInput[]} contents
 * @property {Record<string, any>} [_meta]
 */

/**
 * @typedef {MCPResourceHandlerResultObject|MCPResourceContentInput|MCPResourceContentInput[]|string|Uint8Array|null|undefined} MCPResourceHandlerResult
 */

/**
 * @typedef {object} MCPInvocationOptions
 * @property {string} [sessionId]
 */

/**
 * @typedef {object} MCPPublishResourceOptions
 * @property {string} [sessionId]
 * @property {string} [subscriptionId]
 */

/**
 * @typedef {object} MCPStartServerOptions
 * @property {string} [host]
 * @property {number} [port] TCP port from 0 through 65535. Use 0 to select an available port.
 * @property {string} [endpoint]
 * @property {string} [sse]
 * @property {string} [message]
 * @property {string} [token]
 * @property {number} [retry] Positive 32-bit SSE retry interval in milliseconds.
 * @property {number} [sessionTtlSeconds=600] Seconds to retain an inactive legacy session.
 * @property {number} [maxRequestBytes=16777216] Maximum HTTP request body size.
 * @property {number} [maxSessions=1024] Maximum concurrent HTTP session contexts.
 * @property {number} [maxQueuedEvents=1024] Maximum queued events per SSE stream.
 * @property {number} [maxQueuedBytes=8388608] Maximum queued event bytes per SSE stream.
 * @property {boolean} [replaceSseStreamOnReconnect=false] Allow a reconnect to replace an existing legacy SSE stream for the same session.
 * @property {(request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision>} [authorize]
 * @property {MCPOAuthOptions | boolean} [oauth]
 */

/**
 * @typedef {object} MCPOAuthServerEndpoints
 * @property {string|null} [authorizePath]
 * @property {string|null} [tokenPath]
 * @property {string|null} [metadataPath]
 * @property {string|null} [protectedResourceMetadataPath]
 */

/**
 * @typedef {object} MCPStartServerResult
 * @property {boolean} running
 * @property {string} host
 * @property {number} port
 * @property {string} endpoint
 * @property {MCPOAuthServerEndpoints} [oauth]
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
    const client = conduitClient
    conduitUnavailable = true
    conduitClient = null
    client?.close()
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

  return await promise
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
  const metadata = normalizeMetadata(value?._meta ?? value?.metadata)
  return {
    name: value?.name ?? '',
    title: value?.title ?? '',
    description: value?.description ?? '',
    inputSchema: assertObject(value?.inputSchema),
    outputSchema: value?.outputSchema,
    icons: Array.isArray(value?.icons) ? value.icons : undefined,
    annotations: assertObject(value?.annotations, undefined),
    _meta: metadata,
    metadata
  }
}

function normalizeDescriptor (descriptor) {
  const value = { ...assertObject(descriptor) }
  const metadata = normalizeMetadata(value._meta ?? value.metadata)
  if (metadata !== undefined) {
    value._meta = metadata
    value.metadata = metadata
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

  const resolve = (result) => {
    let resultJson
    try {
      resultJson = JSON.stringify(normalizeToolResult(result))
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
  }

  Promise.resolve()
    .then(() => handler(context))
    .then(
      (result) => resolve(result),
      (error) =>
        resolve({
          content: [
            {
              type: 'text',
              text: error?.message || String(error)
            }
          ],
          isError: true
        })
    )
    .catch(() => {})
}

function isValidBase64 (value) {
  return (
    typeof value === 'string' &&
    value.length % 4 === 0 &&
    /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(
      value
    )
  )
}

function validateContentBlock (content, index) {
  const label = `Tool result content[${index}]`
  if (!content || typeof content !== 'object' || Array.isArray(content)) {
    throw new TypeError(`${label} must be an object`)
  }
  if (typeof content.type !== 'string') {
    throw new TypeError(`${label}.type must be a string`)
  }
  validateMetadata(content._meta, `${label}._meta`)
  validateResourceAnnotations(content.annotations, `${label}.annotations`)

  if (content.type === 'text') {
    if (typeof content.text !== 'string') {
      throw new TypeError(`${label}.text must be a string`)
    }
    return
  }

  if (content.type === 'image' || content.type === 'audio') {
    if (!isValidBase64(content.data)) {
      throw new TypeError(`${label}.data must be valid base64`)
    }
    if (typeof content.mimeType !== 'string' || content.mimeType.length === 0) {
      throw new TypeError(`${label}.mimeType must be a non-empty string`)
    }
    return
  }

  if (content.type === 'resource_link') {
    if (typeof content.name !== 'string' || content.name.length === 0) {
      throw new TypeError(`${label}.name must be a non-empty string`)
    }
    if (typeof content.uri !== 'string' || content.uri.length === 0) {
      throw new TypeError(`${label}.uri must be a non-empty string`)
    }
    validateOptionalString(content.title, `${label}.title`)
    validateOptionalString(content.description, `${label}.description`)
    validateOptionalString(content.mimeType, `${label}.mimeType`)
    validateIcons(content.icons, `${label}.icons`)
    if (
      content.size !== undefined &&
      (!Number.isSafeInteger(content.size) || content.size < 0)
    ) {
      throw new TypeError(`${label}.size must be a non-negative safe integer`)
    }
    return
  }

  if (content.type === 'resource') {
    const resource = content.resource
    if (!resource || typeof resource !== 'object' || Array.isArray(resource)) {
      throw new TypeError(`${label}.resource must be an object`)
    }
    if (typeof resource.uri !== 'string' || resource.uri.length === 0) {
      throw new TypeError(`${label}.resource.uri must be a non-empty string`)
    }
    const hasText = typeof resource.text === 'string'
    const hasBlob = typeof resource.blob === 'string'
    if (hasText === hasBlob) {
      throw new TypeError(
        `${label}.resource must contain exactly one string field: text or blob`
      )
    }
    if (hasBlob && !isValidBase64(resource.blob)) {
      throw new TypeError(`${label}.resource.blob must be valid base64`)
    }
    validateOptionalString(resource.mimeType, `${label}.resource.mimeType`)
    validateMetadata(resource._meta, `${label}.resource._meta`)
    return
  }

  throw new TypeError(`${label}.type is not a supported MCP content type`)
}

function normalizeToolResult (result) {
  if (
    result &&
    typeof result === 'object' &&
    Array.isArray(result.content)
  ) {
    result.content.forEach(validateContentBlock)
    if (result.isError !== undefined && typeof result.isError !== 'boolean') {
      throw new TypeError('Tool result isError must be a boolean')
    }
    if (result.structuredContent !== undefined) {
      validateJSONValue(
        result.structuredContent,
        'Tool result structuredContent'
      )
    }
    validateMetadata(result._meta, 'Tool result _meta')
    if (
      result.structuredContent !== undefined &&
      (result.structuredContent === null ||
        typeof result.structuredContent !== 'object' ||
        Array.isArray(result.structuredContent))
    ) {
      const serialized = JSON.stringify(result.structuredContent)
      if (
        !result.content.some(
          (entry) => entry.type === 'text' && entry.text === serialized
        )
      ) {
        return {
          ...result,
          content: [...result.content, { type: 'text', text: serialized }]
        }
      }
    }
    return result
  }

  if (result === undefined) {
    return { content: [] }
  }

  validateJSONValue(result, 'Tool result')
  const text = JSON.stringify(result)
  if (typeof text !== 'string') {
    throw new TypeError('Tool result must be JSON serializable')
  }
  return {
    content: [{ type: 'text', text }],
    structuredContent: result
  }
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
  if (!source || typeof source !== 'object' || Array.isArray(source)) {
    throw new TypeError('oauth must be a boolean or an options object')
  }

  const normalized = {}
  if (source.enabled !== undefined && typeof source.enabled !== 'boolean') {
    throw new TypeError('oauth.enabled must be a boolean')
  }
  normalized.enabled = source.enabled ?? true

  const stringFields = [
    ['issuer', 'issuer'],
    ['resource', 'resource'],
    ['authorizePath', 'authorizePath'],
    ['tokenPath', 'tokenPath'],
    ['metadataPath', 'metadataPath'],
    ['defaultClientId', 'defaultClientId'],
    ['defaultScope', 'defaultScope']
  ]

  for (const [key, target] of stringFields) {
    const value = source[key]
    if (value === undefined) continue
    if (typeof value !== 'string' || value.length === 0) {
      throw new TypeError(`oauth.${key} must be a non-empty string`)
    }
    normalized[target] = value
  }

  if (source.redirectUris !== undefined) {
    if (
      !Array.isArray(source.redirectUris) ||
      source.redirectUris.some(
        (value) => typeof value !== 'string' || value.length === 0
      )
    ) {
      throw new TypeError(
        'oauth.redirectUris must be an array of non-empty strings'
      )
    }
    normalized.redirectUris = [...source.redirectUris]
  }

  const numberFields = [
    ['codeLifetimeSeconds', 'codeLifetimeSeconds'],
    ['tokenLifetimeSeconds', 'tokenLifetimeSeconds']
  ]

  for (const [key, target] of numberFields) {
    const value = source[key]
    if (value === undefined) continue
    if (
      !Number.isSafeInteger(value) ||
      value <= 0 ||
      value > 0xffffffff
    ) {
      throw new RangeError(
        `oauth.${key} must be a positive 32-bit safe integer`
      )
    }
    normalized[target] = value
  }

  const screen = source.screen
  if (screen !== undefined) {
    if (!screen || typeof screen !== 'object' || Array.isArray(screen)) {
      throw new TypeError('oauth.screen must be an options object')
    }
    const screenConfig = {}
    for (const key of ['html', 'file']) {
      const value = screen[key]
      if (value === undefined) continue
      if (typeof value !== 'string' || value.length === 0) {
        throw new TypeError(`oauth.screen.${key} must be a non-empty string`)
      }
      screenConfig[key] = value
    }
    if (screenConfig.html && screenConfig.file) {
      throw new TypeError(
        'oauth.screen must specify either html or file, not both'
      )
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
  const uri = normalizedDescriptor?.uri || ''

  if (result == null) {
    return { contents: [] }
  }

  const normalizeContent = (content) => {
    if (typeof content === 'string') {
      content = { text: content }
    } else if (Buffer.isBuffer(content) || content instanceof Uint8Array) {
      content = { blob: Buffer.from(content).toString('base64') }
    }

    if (!content || typeof content !== 'object' || Array.isArray(content)) {
      throw new TypeError('Resource contents must be objects, strings, or bytes')
    }

    const contentUri =
      typeof content.uri === 'string' && content.uri.length > 0
        ? content.uri
        : uri
    if (!contentUri) {
      throw new TypeError('Resource content requires a non-empty uri')
    }

    const hasText = typeof content.text === 'string'
    const binary = Buffer.isBuffer(content.blob) ||
      content.blob instanceof Uint8Array
    const blob = binary
      ? Buffer.from(content.blob).toString('base64')
      : content.blob
    const hasBlob = typeof blob === 'string'
    if (hasText === hasBlob) {
      throw new TypeError(
        'Resource content must contain exactly one string field: text or blob'
      )
    }

    if (hasBlob && !isValidBase64(blob)) {
      throw new TypeError('Resource blob must be valid base64')
    }

    const normalized = {
      uri: contentUri,
      ...(hasText ? { text: content.text } : { blob })
    }
    const contentMimeType = content.mimeType ?? (hasBlob ? mimeType : undefined)
    if (contentMimeType !== undefined) {
      if (typeof contentMimeType !== 'string' || contentMimeType.length === 0) {
        throw new TypeError('Resource content mimeType must be a non-empty string')
      }
      normalized.mimeType = contentMimeType
    }
    if (content._meta !== undefined) {
      if (
        !content._meta ||
        typeof content._meta !== 'object' ||
        Array.isArray(content._meta)
      ) {
        throw new TypeError('Resource content _meta must be an object')
      }
      normalized._meta = content._meta
    }
    return normalized
  }

  if (typeof result === 'string') {
    return { contents: [normalizeContent(result)] }
  }

  if (Buffer.isBuffer(result) || result instanceof Uint8Array) {
    return { contents: [normalizeContent(result)] }
  }

  if (Array.isArray(result)) {
    return { contents: result.map(normalizeContent) }
  }

  if (typeof result === 'object') {
    if (Array.isArray(result.contents)) {
      const normalized = { contents: result.contents.map(normalizeContent) }
      if (result._meta !== undefined) {
        if (
          !result._meta ||
          typeof result._meta !== 'object' ||
          Array.isArray(result._meta)
        ) {
          throw new TypeError('Resource result _meta must be an object')
        }
        normalized._meta = result._meta
      }
      return normalized
    }

    if (
      typeof result.text === 'string' ||
      typeof result.blob === 'string' ||
      Buffer.isBuffer(result.blob) ||
      result.blob instanceof Uint8Array
    ) {
      return { contents: [normalizeContent(result)] }
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
    const serialized = JSON.stringify(value)
    if (typeof serialized !== 'string') {
      throw new TypeError(`${label} must be JSON serializable`)
    }
    return serialized
  } catch (error) {
    throw new TypeError(error?.message || `Failed to serialize ${label}`)
  }
}

function validateOptionalString (value, label) {
  if (value !== undefined && typeof value !== 'string') {
    throw new TypeError(`${label} must be a string`)
  }
}

function validateMetadata (value, label) {
  if (
    value !== undefined &&
    (!value || typeof value !== 'object' || Array.isArray(value))
  ) {
    throw new TypeError(`${label} must be an object`)
  }
}

function validateJSONValue (value, label) {
  try {
    const serialized = JSON.stringify(value, (key, nested) => {
      if (typeof nested === 'number' && !Number.isFinite(nested)) {
        throw new TypeError(`${label} numbers must be finite`)
      }
      if (
        nested === undefined ||
        typeof nested === 'bigint' ||
        typeof nested === 'function' ||
        typeof nested === 'symbol'
      ) {
        throw new TypeError(`${label} must contain only JSON values`)
      }
      return nested
    })
    if (typeof serialized !== 'string') {
      throw new TypeError(`${label} must be JSON serializable`)
    }
  } catch (error) {
    throw new TypeError(error?.message || `${label} must be JSON serializable`)
  }
}

function validateIcons (icons, label) {
  if (icons === undefined) return
  if (!Array.isArray(icons)) {
    throw new TypeError(`${label} must be an array`)
  }
  for (let index = 0; index < icons.length; index++) {
    const icon = icons[index]
    const prefix = `${label}[${index}]`
    if (!icon || typeof icon !== 'object' || Array.isArray(icon)) {
      throw new TypeError(`${prefix} must be an object`)
    }
    if (typeof icon.src !== 'string' || icon.src.length === 0) {
      throw new TypeError(`${prefix}.src must be a non-empty string`)
    }
    validateOptionalString(icon.mimeType, `${prefix}.mimeType`)
    if (
      icon.sizes !== undefined &&
      (!Array.isArray(icon.sizes) ||
        icon.sizes.some(
          (size) => typeof size !== 'string' || size.length === 0
        ))
    ) {
      throw new TypeError(
        `${prefix}.sizes must be an array of non-empty strings`
      )
    }
    if (
      icon.theme !== undefined &&
      icon.theme !== 'light' &&
      icon.theme !== 'dark'
    ) {
      throw new TypeError(`${prefix}.theme must be 'light' or 'dark'`)
    }
  }
}

function validateToolAnnotations (annotations) {
  if (annotations === undefined) return
  validateMetadata(annotations, 'tool.annotations')
  validateOptionalString(annotations.title, 'tool.annotations.title')
  for (const key of [
    'readOnlyHint',
    'destructiveHint',
    'idempotentHint',
    'openWorldHint'
  ]) {
    if (annotations[key] !== undefined && typeof annotations[key] !== 'boolean') {
      throw new TypeError(`tool.annotations.${key} must be a boolean`)
    }
  }
}

function validateResourceAnnotations (
  annotations,
  label = 'resource.annotations'
) {
  if (annotations === undefined) return
  validateMetadata(annotations, label)
  if (
    annotations.audience !== undefined &&
    (!Array.isArray(annotations.audience) ||
      annotations.audience.some(
        (role) => role !== 'user' && role !== 'assistant'
      ))
  ) {
    throw new TypeError(
      `${label}.audience must contain only 'user' or 'assistant'`
    )
  }
  if (
    annotations.priority !== undefined &&
    (!Number.isFinite(annotations.priority) ||
      annotations.priority < 0 ||
      annotations.priority > 1)
  ) {
    throw new RangeError(
      `${label}.priority must be a number from 0 through 1`
    )
  }
  validateOptionalString(
    annotations.lastModified,
    `${label}.lastModified`
  )
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

  const inputSchema = tool.inputSchema ?? { type: 'object' }
  if (
    !inputSchema ||
    typeof inputSchema !== 'object' ||
    Array.isArray(inputSchema) ||
    inputSchema.type !== 'object'
  ) {
    throw new TypeError("tool.inputSchema must declare type 'object' at its root")
  }
  if (
    tool.outputSchema !== undefined &&
    (!tool.outputSchema ||
      typeof tool.outputSchema !== 'object' ||
      Array.isArray(tool.outputSchema))
  ) {
    throw new TypeError('tool.outputSchema must be a JSON object')
  }
  validateOptionalString(tool.title, 'tool.title')
  validateOptionalString(tool.description, 'tool.description')
  validateIcons(tool.icons, 'tool.icons')
  validateToolAnnotations(tool.annotations)
  validateMetadata(tool._meta ?? tool.metadata, 'tool metadata')
  if (tool.handler !== undefined && typeof tool.handler !== 'function') {
    throw new TypeError('tool.handler must be a function')
  }

  const metadataJson = serializeForIPC(
    tool._meta ?? tool.metadata,
    'tool metadata'
  )
  const inputSchemaJson = serializeForIPC(inputSchema, 'tool input schema')
  const outputSchemaJson = serializeForIPC(
    tool.outputSchema,
    'tool output schema'
  )
  const iconsJson = serializeForIPC(tool.icons, 'tool icons')
  const annotationsJson = serializeForIPC(
    tool.annotations,
    'tool annotations'
  )

  const payload = {
    name: tool.name,
    title: tool.title || '',
    description: tool.description || '',
    metadata: metadataJson,
    inputSchema: inputSchemaJson
  }

  if (outputSchemaJson.length > 0) payload.outputSchema = outputSchemaJson
  if (iconsJson.length > 0) payload.icons = iconsJson
  if (annotationsJson.length > 0) payload.annotations = annotationsJson

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

/**
 * Unregister a tool by name.
 * @param {string} name
 * @returns {Promise<boolean>} Whether a registered tool was removed.
 */
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

/**
 * List the currently registered tool descriptors.
 * @returns {Promise<MCPToolDescriptor[]>}
 */
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

  validateOptionalString(resource.name, 'resource.name')
  validateOptionalString(resource.title, 'resource.title')
  validateOptionalString(resource.description, 'resource.description')
  validateOptionalString(resource.mimeType, 'resource.mimeType')
  validateIcons(resource.icons, 'resource.icons')
  validateResourceAnnotations(resource.annotations)
  validateMetadata(resource._meta ?? resource.metadata, 'resource metadata')
  if (
    resource.subscribable !== undefined &&
    typeof resource.subscribable !== 'boolean'
  ) {
    throw new TypeError('resource.subscribable must be a boolean')
  }
  for (const key of ['handler', 'onSubscribe', 'onUnsubscribe']) {
    if (resource[key] !== undefined && typeof resource[key] !== 'function') {
      throw new TypeError(`resource.${key} must be a function`)
    }
  }
  if (
    resource.size !== undefined &&
    (!Number.isSafeInteger(resource.size) || resource.size < 0)
  ) {
    throw new TypeError('resource.size must be a non-negative safe integer')
  }

  const metadataJson = serializeForIPC(
    resource._meta ?? resource.metadata,
    'resource metadata'
  )
  const iconsJson = serializeForIPC(resource.icons, 'resource icons')
  const annotationsJson = serializeForIPC(
    resource.annotations,
    'resource annotations'
  )

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
    title: resource.title || '',
    description: resource.description || '',
    mimeType: resource.mimeType || '',
    icons: iconsJson,
    annotations: annotationsJson,
    size: resource.size === undefined ? '' : String(resource.size),
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
        name: resource.name || resource.uri,
        title: resource.title || '',
        description: resource.description || '',
        mimeType: resource.mimeType || '',
        icons: resource.icons,
        annotations: resource.annotations,
        size: resource.size,
        _meta: resource._meta ?? resource.metadata
      })
    })
  } else {
    resourceHandlers.delete(resource.uri)
    cleanupDataListener()
  }
  return data.id ?? null
}

/**
 * Unregister a resource by URI.
 * @param {string} uri
 * @returns {Promise<boolean>} Whether a registered resource was removed.
 */
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

/**
 * List the currently registered resource descriptors.
 * @returns {Promise<MCPResourceDescriptor[]>}
 */
export async function listResources () {
  const result = await runtimeRequest('mcp.server.listResources')
  const data = resultData(result)
  const resources = Array.isArray(data.resources) ? data.resources : []
  return resources.map((resource) => ({
    uri: resource?.uri ?? '',
    name: resource?.name ?? '',
    title: resource?.title ?? '',
    description: resource?.description ?? '',
    mimeType: resource?.mimeType ?? '',
    icons: Array.isArray(resource?.icons) ? resource.icons : undefined,
    annotations: assertObject(resource?.annotations, undefined),
    size: Number.isSafeInteger(resource?.size) ? resource.size : undefined,
    subscribable: Boolean(resource?.subscribable),
    _meta: normalizeMetadata(resource?._meta ?? resource?.metadata),
    metadata: normalizeMetadata(resource?._meta ?? resource?.metadata)
  }))
}

/**
 * Invoke a registered tool through the local MCP service.
 * @param {string} name
 * @param {Record<string, any>} [args]
 * @param {MCPInvocationOptions} [options]
 * @returns {Promise<boolean>} Whether the invocation was accepted.
 */
export async function invokeTool (name, args = {}, options = null) {
  if (typeof name !== 'string' || name.length === 0) {
    throw new TypeError('name must be a non-empty string')
  }
  if (!args || typeof args !== 'object' || Array.isArray(args)) {
    throw new TypeError('args must be an object')
  }
  if (
    options !== null &&
    (typeof options !== 'object' || Array.isArray(options))
  ) {
    throw new TypeError('options must be an object')
  }
  if (options?.sessionId !== undefined && typeof options.sessionId !== 'string') {
    throw new TypeError('options.sessionId must be a string')
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

/**
 * Publish an update to active subscriptions for a registered resource.
 * @param {string} uri
 * @param {MCPResourceHandlerResult} result
 * @param {MCPPublishResourceOptions} [options]
 * @returns {Promise<boolean>} Whether at least one matching stream received the update.
 */
export async function publishResource (uri, result, options = null) {
  if (typeof uri !== 'string' || uri.length === 0) {
    throw new TypeError('uri must be a non-empty string')
  }
  if (
    options !== null &&
    (typeof options !== 'object' || Array.isArray(options))
  ) {
    throw new TypeError('options must be an object')
  }
  for (const key of ['sessionId', 'subscriptionId']) {
    if (options?.[key] !== undefined && typeof options[key] !== 'string') {
      throw new TypeError(`options.${key} must be a string`)
    }
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
 * @returns {Promise<MCPStartServerResult>}
 */
export async function startServer (options = null) {
  if (
    options !== null &&
    (typeof options !== 'object' || Array.isArray(options))
  ) {
    throw new TypeError('options must be an object')
  }

  const payload = {}
  const setString = (key, allowEmpty = false) => {
    const value = options?.[key]
    if (value === undefined) return
    if (typeof value !== 'string' || (!allowEmpty && value.length === 0)) {
      throw new TypeError(
        `${key} must be ${allowEmpty ? 'a string' : 'a non-empty string'}`
      )
    }
    payload[key] = value
  }
  const setPositiveInteger = (key) => {
    const value = options?.[key]
    if (value === undefined) return
    if (!Number.isSafeInteger(value) || value <= 0) {
      throw new RangeError(`${key} must be a positive safe integer`)
    }
    payload[key] = String(value)
  }
  setString('host')
  if (options?.port !== undefined) {
    if (
      !Number.isSafeInteger(options.port) ||
      options.port < 0 ||
      options.port > 65535
    ) {
      throw new RangeError('port must be a safe integer from 0 through 65535')
    }
    payload.port = String(options.port)
  }
  setString('endpoint')
  setString('sse')
  setString('message')
  setString('token', true)
  if (options?.retry !== undefined) {
    if (
      !Number.isSafeInteger(options.retry) ||
      options.retry <= 0 ||
      options.retry > 0xffffffff
    ) {
      throw new RangeError('retry must be a positive 32-bit safe integer')
    }
    payload.retry = String(options.retry)
  }
  setPositiveInteger('sessionTtlSeconds')
  setPositiveInteger('maxRequestBytes')
  setPositiveInteger('maxSessions')
  setPositiveInteger('maxQueuedEvents')
  setPositiveInteger('maxQueuedBytes')
  if (options?.replaceSseStreamOnReconnect !== undefined) {
    if (typeof options.replaceSseStreamOnReconnect !== 'boolean') {
      throw new TypeError('replaceSseStreamOnReconnect must be a boolean')
    }
    payload.replaceSseStreamOnReconnect = String(
      options.replaceSseStreamOnReconnect
    )
  }
  const oauthOptions = normalizeOAuthOptions(options?.oauth)
  if (oauthOptions?.enabled) {
    if (
      typeof oauthOptions.defaultClientId !== 'string' ||
      oauthOptions.defaultClientId.length === 0
    ) {
      throw new TypeError(
        'oauth.defaultClientId is required when OAuth is enabled'
      )
    }
    if (
      !Array.isArray(oauthOptions.redirectUris) ||
      oauthOptions.redirectUris.length === 0
    ) {
      throw new TypeError(
        'oauth.redirectUris must contain at least one pre-registered URI'
      )
    }
  }
  if (oauthOptions) {
    payload.oauth = JSON.stringify(oauthOptions)
  }

  const updatesAuthorization = Boolean(
    options && Object.prototype.hasOwnProperty.call(options, 'authorize')
  )
  const previousAuthorizationHandler = authHandler
  if (updatesAuthorization) {
    await setAuthorizationHandler(options.authorize)
  }

  let result
  try {
    result = await runtimeRequest('mcp.server.start', payload)
  } catch (error) {
    if (updatesAuthorization) {
      await setAuthorizationHandler(previousAuthorizationHandler).catch(() => {})
    }
    throw error
  }
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
    ),
    protectedResourceMetadataPath: resolveOAuthPath(
      'oauthProtectedResourceMetadataPath',
      null
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

/**
 * Stop the embedded MCP server.
 * @returns {Promise<boolean>} Whether the server is stopped.
 */
export async function stopServer () {
  const result = await runtimeRequest('mcp.server.stop')
  const data = resultData(result)
  return Boolean(data.running) === false
}

/**
 * Report whether the embedded MCP server is running.
 * @returns {Promise<boolean>}
 */
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
