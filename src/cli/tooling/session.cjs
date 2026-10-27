'use strict'

const { Messages, encode } = require('./lsp.cjs')
const { spawnManaged, operation } = require('./process.cjs')

const maximumBuffer = 16 * 1024 * 1024

function offset (text, position) {
  if (!Number.isInteger(position.line) || !Number.isInteger(position.character) || position.line < 0 || position.character < 0) throw new Error('Invalid document position')
  let start = 0
  for (let line = 0; line < position.line; line++) {
    const end = text.indexOf('\n', start)
    if (end < 0) throw new Error('Document edit line is outside the buffer')
    start = end + 1
  }
  let end = text.indexOf('\n', start)
  if (end < 0) end = text.length
  if (text[end - 1] === '\r') end--
  if (position.character > end - start) throw new Error('Document edit character is outside the buffer')
  return start + position.character
}

/**
 * Apply ordered LSP UTF-16 changes to a retained, unsaved document.
 * @param {object} document LSP TextDocumentItem containing the retained text.
 * @param {object} params LSP DidChangeTextDocumentParams.
 * @returns {object} Updated TextDocumentItem.
 */
function updateDocument (document, params) {
  if (params.textDocument.version <= document.version) throw new Error('Document versions must increase')
  let text = document.text
  for (const change of params.contentChanges) {
    if (!change.range) text = change.text
    else {
      const start = offset(text, change.range.start)
      const end = offset(text, change.range.end)
      if (end < start) throw new Error('Invalid document edit range')
      text = text.slice(0, start) + change.text + text.slice(end)
    }
    if (Buffer.byteLength(text) > maximumBuffer) throw new Error('Open document exceeds 16 MiB')
  }
  return { ...document, text, version: params.textDocument.version }
}

function staticCapabilities (value) {
  if (!value || typeof value !== 'object') return value
  if (Array.isArray(value)) return value.map(staticCapabilities)
  return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, key === 'dynamicRegistration' ? false : staticCapabilities(item)]))
}

/**
 * Own one editor connection across backend refreshes, retaining unsaved buffers.
 * @param {import('./lsp.cjs').InitializedClient} client
 * @param {Function} prepare Returns a launch descriptor; nested build tools inherit cancellation.
 * @param {object} [options]
 * @returns {Promise<void>}
 */
async function serve (client, prepare, options = {}) {
  const output = options.output || process.stdout
  const timeout = options.startupTimeout || 300000
  const documents = new Map()
  const pending = new Map()
  const serverRequests = new Map()
  const diagnostics = new Set()
  const initialize = structuredClone(client.message)
  const token = initialize.params.workDoneToken
  let descriptor
  let backend
  let controller
  let initialized = false
  let initialFailure = false
  let ready = false
  let closed = false
  let refreshing = false
  let shutdown = false
  let configuration
  let signatures
  let generation = 0
  let sequence = 0
  let initialCapabilities
  let preparation
  let refreshTimer
  let startupTimer
  let watchTimer = null
  let disconnectTimer
  let resolveDone
  let retryTimes = []
  const done = new Promise(resolve => { resolveDone = resolve })
  const send = message => {
    if (closed || output.destroyed) return
    if (output.writableLength > maximumBuffer) { finish(); return }
    output.write(encode({ jsonrpc: '2.0', ...message }))
  }
  const report = (message, error = false) => send({ method: 'window/logMessage', params: { type: error ? 1 : 3, message } })
  const progress = (kind, message) => {
    if (!initialized && token != null) send({ method: '$/progress', params: { token, value: { kind, ...(kind === 'begin' ? { title: 'Oro Language Server', cancellable: true } : {}), message } } })
    else report(message)
  }
  const failPending = () => {
    for (const { id } of pending.values()) send({ id, error: { code: -32802, message: 'Oro is refreshing the editor model; retry the request.' } })
    pending.clear()
    for (const id of serverRequests.keys()) send({ method: '$/cancelRequest', params: { id } })
    serverRequests.clear()
  }
  const clearDiagnostics = () => {
    for (const uri of diagnostics) send({ method: 'textDocument/publishDiagnostics', params: { uri, diagnostics: [] } })
    diagnostics.clear()
  }
  const finish = () => {
    if (closed) return
    closed = true
    controller?.abort(new Error('Editor disconnected'))
    backend?.stop()
    clearTimeout(startupTimer)
    clearTimeout(refreshTimer)
    clearTimeout(disconnectTimer)
    clearInterval(watchTimer)
    resolveDone()
  }
  const writeBackend = message => {
    if (!backend || backend.child.stdin.destroyed) return
    if (backend.child.stdin.writableLength > maximumBuffer) throw new Error('Backend is not reading LSP input')
    backend.child.stdin.write(encode({ jsonrpc: '2.0', ...message }))
  }
  const forward = message => {
    const mapped = descriptor.transform ? descriptor.transform(structuredClone(message)) : [message]
    for (const item of mapped) {
      if (item.id != null && item.method) {
        if (pending.size >= 1024) { send({ id: item.id, error: { code: -32000, message: 'Too many pending editor requests' } }); continue }
        const id = `oro:client:${generation}:${++sequence}`
        pending.set(id, { id: item.id, method: item.method, uri: message.params?.textDocument?.uri, version: documents.get(message.params?.textDocument?.uri)?.version })
        writeBackend({ ...item, id })
      } else writeBackend(item)
    }
  }
  const failure = error => {
    if (closed || shutdown || initialFailure) return
    if (!initialized) initialFailure = true
    ready = false
    controller?.abort(error)
    backend?.stop()
    failPending()
    clearDiagnostics()
    clearTimeout(startupTimer)
    clearTimeout(refreshTimer)
    refreshTimer = null
    progress('end', error.message)
    if (!initialized) {
      send({ id: initialize.id, error: { code: -32002, message: error.message, data: { retry: false } } })
      disconnectTimer = setTimeout(finish, 1000)
    } else {
      send({ method: 'window/showMessage', params: { type: 1, message: `Oro Language Server: ${error.message}` } })
    }
  }
  const scheduleRefresh = (reason, crash = false) => {
    if (closed || shutdown || refreshTimer || refreshing) return
    if (crash) {
      retryTimes = retryTimes.filter(time => Date.now() - time < 60000)
      if (retryTimes.length >= 3) { failure(new Error('Backend repeatedly exited. Inspect its log and restart the language client after fixing the cause.')); return }
      retryTimes.push(Date.now())
    }
    refreshTimer = setTimeout(() => {
      refreshTimer = null
      preparation = restart(reason).catch(failure)
    }, crash ? 500 * retryTimes.length : 250)
  }
  const handleBackend = (message, current) => {
    if (current !== generation || closed || controller.signal.aborted) return
    if (descriptor.initializationProgressTitle && message.method === 'window/workDoneProgress/create') {
      if (backend.progressTokens.size >= 256) throw new Error('Too many backend progress tokens')
      backend.progressTokens.add(message.params.token)
      writeBackend({ id: message.id, result: null })
      return []
    }
    if (descriptor.initializationProgressTitle && message.method === '$/progress' && backend.progressTokens.has(message.params.token)) {
      const { token, value } = message.params
      if (value.kind === 'begin' && value.title === descriptor.initializationProgressTitle) {
        backend.indexTokens.add(token)
        progress('report', 'Indexing Kotlin dependencies')
        if (ready) { ready = false; failPending(); scheduleRefresh('Refreshing Kotlin dependency index') }
      } else if (value.kind === 'end') {
        backend.progressTokens.delete(token)
        if (backend.indexTokens.delete(token) && !backend.indexTokens.size) {
          backend.indexReady = true
          if (backend.initializeResult) handleBackend(backend.initializeResult, current)
        }
      }
      return []
    }
    if (message.id === `oro:initialize:${current}`) {
      if (message.error) throw new Error(message.error.message || 'Backend initialization failed')
      if (!message.result?.capabilities) throw new Error('Backend returned an invalid initialize result')
      // fwcd's background dependency index shares compiler locks with hover.
      // Wait for its progress end before exposing analysis to the editor.
      if (descriptor.initializationProgressTitle && !backend.indexReady) {
        backend.initializeResult = message
        return []
      }
      backend.initializeResult = null
      clearTimeout(startupTimer)
      const capabilities = message.result.capabilities
      capabilities.workspace = { ...capabilities.workspace, workspaceFolders: { supported: false, changeNotifications: false } }
      // The wrapper accepts incremental changes and replays complete buffers.
      const backendSync = typeof capabilities.textDocumentSync === 'object' ? capabilities.textDocumentSync : { change: capabilities.textDocumentSync }
      descriptor.fullSync = backendSync.change === 1
      capabilities.textDocumentSync = { ...backendSync, openClose: true, change: 2 }
      capabilities.positionEncoding = 'utf-16'
      const serialized = JSON.stringify(capabilities)
      if (initialized && serialized !== initialCapabilities) throw new Error('Backend capabilities changed. Restart the language client to negotiate the new capabilities.')
      progress('end', 'Editor model ready')
      if (!initialized) {
        initialCapabilities = serialized
        initialized = true
        send({ id: initialize.id, result: { ...message.result, capabilities, serverInfo: { name: `Oro Language Server (${descriptor.language})` } } })
      }
      ready = true
      writeBackend({ method: 'initialized', params: {} })
      if (configuration) forward(configuration)
      if (descriptor.configuration) writeBackend(descriptor.configuration)
      for (const document of documents.values()) {
        try { forward({ method: 'textDocument/didOpen', params: { textDocument: document } }) } catch (error) { report(error.message, true) }
      }
      const earlyDiagnostics = [...backend.earlyDiagnostics.values()]
      backend.earlyDiagnostics.clear()
      for (const diagnostic of earlyDiagnostics) {
        const outgoing = descriptor.outgoing ? descriptor.outgoing(diagnostic) : diagnostic
        if (!documents.has(outgoing.params.uri)) handleBackend(diagnostic, current)
      }
      return []
    }
    if (message.id != null && !message.method) {
      const request = pending.get(message.id)
      if (!request) return []
      pending.delete(message.id)
      if (request.version != null && documents.get(request.uri)?.version !== request.version) {
        send({ id: request.id, error: { code: -32801, message: 'Document changed while the request was running; retry it.' } })
        return []
      }
      try { descriptor.validate?.(message, request.method) } catch (error) {
        send({ id: request.id, error: { code: -32603, message: error.message } })
        return []
      }
      send({ ...(descriptor.outgoing ? descriptor.outgoing(message) : message), id: request.id })
    } else if (message.id != null) {
      if (message.method === 'client/registerCapability' || message.method === 'client/unregisterCapability') {
        writeBackend({ id: message.id, error: { code: -32601, message: 'Oro uses static capability registration' } })
        return []
      }
      if (serverRequests.size >= 1024) throw new Error('Too many pending backend requests')
      try { descriptor.validate?.(message) } catch (error) {
        writeBackend({ id: message.id, ...(message.method === 'workspace/applyEdit' ? { result: { applied: false, failureReason: error.message } } : { error: { code: -32603, message: error.message } }) })
        return []
      }
      const id = `oro:server:${current}:${++sequence}`
      serverRequests.set(id, message.id)
      send({ ...(descriptor.outgoing ? descriptor.outgoing(message) : message), id })
    } else {
      if (message.method === 'textDocument/publishDiagnostics') {
        if (descriptor.accepts && !descriptor.accepts(message.params.uri)) return []
        if (!ready) {
          backend.earlyDiagnostics.set(message.params.uri, message)
          if (backend.earlyDiagnostics.size > 1024 || [...backend.earlyDiagnostics.values()].reduce((size, item) => size + Buffer.byteLength(JSON.stringify(item)), 0) > maximumBuffer) throw new Error('Initial diagnostics exceed the editor buffer limit')
          return []
        }
        const outgoing = descriptor.outgoing ? descriptor.outgoing(message) : message
        const document = documents.get(outgoing.params.uri)
        if (document && outgoing.params.version != null && outgoing.params.version < document.version) return []
        diagnostics.add(outgoing.params.uri)
        send(outgoing)
        return []
      }
      send(descriptor.outgoing ? descriptor.outgoing(message) : message)
    }
    return []
  }
  const restart = async reason => {
    refreshing = true
    ready = false
    const previous = backend
    backend = null
    generation++
    const current = generation
    failPending()
    clearDiagnostics()
    controller?.abort(new Error('Editor model refresh'))
    controller = new AbortController()
    progress('begin', reason)
    startupTimer = setTimeout(() => failure(new Error(`Editor startup exceeded ${timeout / 1000}s. Check toolchain availability or increase --startup-timeout.`)), timeout)
    try {
      previous?.stop()
      if (previous) await previous.exited
      await descriptor?.release?.()
      descriptor = await operation(controller.signal, () => prepare(reason))
      controller.signal.throwIfAborted()
      signatures = descriptor.watch?.()
      backend = spawnManaged(descriptor.command, descriptor.args, { cwd: descriptor.cwd, env: descriptor.env, stdio: ['pipe', 'pipe', 'inherit'] })
      const managed = backend
      managed.exited = managed.finished
      managed.progressTokens = new Set()
      managed.indexTokens = new Set()
      managed.earlyDiagnostics = new Map()
      const parser = new Messages(message => handleBackend(message, current) || [])
      const backendFailure = error => { if (generation === current) failure(error) }
      parser.on('error', backendFailure)
      managed.child.stdin.on('error', backendFailure)
      managed.child.stdout.on('error', backendFailure)
      managed.child.on('error', backendFailure)
      managed.child.stdout.pipe(parser).resume()
      managed.child.once('exit', (code, signal) => {
        managed.stop()
        if (current !== generation || closed || shutdown || controller.signal.aborted) return
        if (!initialized) failure(new Error(`Backend exited during initialization (${signal || code}); inspect its stderr output.`))
        else { ready = false; failPending(); clearDiagnostics(); scheduleRefresh(`Backend exited (${signal || code}); restarting`, true) }
      })
      const request = structuredClone(initialize)
      request.id = `oro:initialize:${current}`
      request.params.capabilities = staticCapabilities(request.params.capabilities || {})
      request.params.capabilities.general = { ...request.params.capabilities.general, positionEncodings: ['utf-16'] }
      if (descriptor.initializationProgressTitle) request.params.capabilities.window = { ...request.params.capabilities.window, workDoneProgress: true }
      delete request.params.workDoneToken
      for (const message of descriptor.beforeInitialize || []) writeBackend(message)
      writeBackend(descriptor.initialize ? descriptor.initialize(request) : request)
    } finally {
      refreshing = false
    }
  }
  const input = new Messages(message => {
    try {
      if (message.method === 'initialize') {
        if (message.id !== initialize.id || initialized) send({ id: message.id, error: { code: -32600, message: 'Oro already received initialize' } })
        return []
      }
      if (message.method === 'initialized') return []
      if (message.method === 'exit') { finish(); return [] }
      if (message.method === 'shutdown') {
        shutdown = true
        controller?.abort(new Error('Editor shutdown'))
        backend?.stop()
        send({ id: message.id, result: null })
        disconnectTimer = setTimeout(finish, 3000)
        return []
      }
      if (message.method === '$/cancelRequest') {
        if (!initialized && message.params.id === initialize.id) { failure(new Error('Editor initialization cancelled')); return [] }
        for (const [id, request] of pending) if (request.id === message.params.id) writeBackend({ ...message, params: { id } })
        return []
      }
      if (message.method === 'window/workDoneProgress/cancel' && message.params.token === token) { failure(new Error('Editor preparation cancelled')); return [] }
      if (message.method === 'workspace/didChangeWorkspaceFolders') { report('Oro serves one application per process; start another client for an additional project.'); return [] }
      if (message.method === 'textDocument/didOpen') {
        const document = message.params.textDocument
        documents.set(document.uri, { ...document })
      } else if (message.method === 'textDocument/didChange') {
        const uri = message.params.textDocument.uri
        if (!documents.has(uri)) throw new Error('Document change arrived before didOpen')
        documents.set(uri, updateDocument(documents.get(uri), message.params))
        if (descriptor?.fullSync) message = { ...message, params: { ...message.params, contentChanges: [{ text: documents.get(uri).text }] } }
      } else if (message.method === 'textDocument/didClose') documents.delete(message.params.textDocument.uri)
      if ([...documents.values()].reduce((sum, document) => sum + Buffer.byteLength(document.text), 0) > 64 * 1024 * 1024) throw new Error('Open editor buffers exceed 64 MiB')
      if (message.method === 'workspace/didChangeConfiguration') configuration = structuredClone(message)
      if (message.id != null && !message.method) {
        if (serverRequests.has(message.id)) { const id = serverRequests.get(message.id); serverRequests.delete(message.id); writeBackend({ ...message, id }) }
      } else if (ready) forward(message)
      else if (message.id != null) send({ id: message.id, error: { code: -32802, message: 'Oro is preparing the editor model; retry the request.' } })
    } catch (error) {
      if (message.id != null) send({ id: message.id, error: { code: -32602, message: error.message } })
      else failure(error)
    }
    return []
  })
  const streamFailure = () => finish()
  input.on('error', streamFailure)
  client.input.on('error', streamFailure)
  client.input.once('end', finish)
  output.on('error', streamFailure)
  process.on('SIGINT', finish)
  process.on('SIGTERM', finish)
  client.input.pipe(input).resume()
  if (client.input.errored || client.input.readableEnded) finish()
  else preparation = restart('Preparing editor dependencies').catch(failure)
  watchTimer = setInterval(() => {
    if (!descriptor?.watch || refreshing || closed || shutdown) return
    try {
      const next = descriptor.watch()
      if (next !== signatures) { signatures = next; scheduleRefresh('Editor inputs changed; refreshing') }
    } catch (error) { failure(error) }
  }, 1500)
  try { await done } finally {
    clearInterval(watchTimer)
    await preparation
    backend?.stop()
    if (backend) await backend.exited
    await descriptor?.release?.()
    client.input.unpipe(input)
    client.input.removeListener('error', streamFailure)
    client.input.removeListener('end', finish)
    output.removeListener('error', streamFailure)
    process.removeListener('SIGINT', finish)
    process.removeListener('SIGTERM', finish)
  }
}

module.exports = { serve, updateDocument }
