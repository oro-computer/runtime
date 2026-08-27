import test from 'oro:test'
import Buffer from 'oro:buffer'
import mcp from 'oro:mcp'
import { fetch } from './http-client.js'

const currentProtocolVersion = '2026-07-28'
const latestMcp2025ProtocolVersion = '2025-11-25'
const earlierMcp2025ProtocolVersion = '2025-06-18'
const mcp2025ProtocolVersions = [
  latestMcp2025ProtocolVersion,
  earlierMcp2025ProtocolVersion
]

function mirrorHeader (value) {
  return '=?base64?' + Buffer.from(value, 'utf8').toString('base64') + '?='
}

function modernParams (params = {}, capabilities = {}) {
  return {
    ...params,
    _meta: {
      ...params._meta,
      'io.modelcontextprotocol/protocolVersion': currentProtocolVersion,
      'io.modelcontextprotocol/clientCapabilities': capabilities
    }
  }
}

function modernHeaders (method, name = null, extra = {}) {
  const headers = {
    Accept: 'application/json, text/event-stream',
    'Content-Type': 'application/json',
    'MCP-Protocol-Version': currentProtocolVersion,
    'Mcp-Method': method,
    ...extra
  }
  if (name != null) headers['Mcp-Name'] = mirrorHeader(name)
  return headers
}

function createSSEReader (response) {
  const decoder = new TextDecoder()
  const reader = response.body.getReader()
  let buffer = ''

  return {
    async next () {
      while (true) {
        const marker = buffer.indexOf('\n\n')
        if (marker !== -1) {
          const block = buffer.slice(0, marker)
          buffer = buffer.slice(marker + 2)
          const data = block
            .split('\n')
            .filter((line) => line.startsWith('data:'))
            .map((line) => line.slice(5).trim())
            .join('\n')
          if (data) return JSON.parse(data)
        }

        const { done, value } = await reader.read()
        if (done) return null
        buffer += decoder
          .decode(value, { stream: true })
          .replace(/\r\n/g, '\n')
          .replace(/\r/g, '\n')
      }
    },

    async close () {
      try {
        await reader.cancel()
      } catch {}
    }
  }
}

async function readMCPResponse (response) {
  const contentType = response.headers.get('content-type') || ''
  if (contentType.startsWith('text/event-stream')) {
    const stream = createSSEReader(response)
    const payload = await stream.next()
    await stream.close()
    return payload
  }
  return response.json()
}

async function postModern (
  endpoint,
  id,
  method,
  params = {},
  { name = null, headers = {}, capabilities = {} } = {}
) {
  const response = await fetch(endpoint, {
    method: 'POST',
    headers: modernHeaders(method, name, headers),
    body: JSON.stringify({
      jsonrpc: '2.0',
      id,
      method,
      params: modernParams(params, capabilities)
    })
  })
  return { response, payload: await readMCPResponse(response) }
}

async function waitForCount (values, count, timeout = 2000) {
  const start = Date.now()
  while (values.length < count) {
    if (Date.now() - start > timeout) {
      throw new Error('Timed out waiting for MCP callback')
    }
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
}

test('mcp: current protocol discovery, descriptors, validation, calls, and resources', async (t) => {
  const toolName = 'oro.mcp.test.echo'
  const structuredToolName = 'oro.mcp.test.structured'
  const resourceUri = 'oro.mcp.test://resource'
  const toolMeta = { 'oro.test/tool': { version: 1 } }
  const resourceMeta = { 'oro.test/resource': { scope: 'test' } }
  const invocations = []
  let serverStarted = false

  try {
    await mcp.registerTool({
      name: toolName,
      title: 'Echo input',
      description: 'Echo a required message',
      inputSchema: {
        type: 'object',
        properties: {
          message: {
            type: 'string',
            minLength: 1,
            'x-mcp-header': 'Message'
          }
        },
        required: ['message'],
        additionalProperties: false
      },
      outputSchema: {
        type: 'object',
        properties: {
          echoed: { type: 'string' }
        },
        required: ['echoed']
      },
      annotations: {
        title: 'Echo input',
        readOnlyHint: true,
        idempotentHint: true
      },
      _meta: toolMeta,
      handler: async ({ arguments: args }) => {
        if (args.message === 'reject') throw new Error('tool rejected input')
        invocations.push(args)
        return {
          content: [{ type: 'text', text: args.message }],
          structuredContent: { echoed: args.message },
          _meta: { 'oro.test/result': true }
        }
      }
    })

    await mcp.registerTool({
      name: structuredToolName,
      title: 'Return any structured JSON value',
      description: 'Exercises non-object structured tool results',
      inputSchema: {
        type: 'object',
        properties: {
          kind: { type: 'string', enum: ['array', 'null'] }
        },
        required: ['kind'],
        additionalProperties: false
      },
      outputSchema: {
        oneOf: [
          {
            type: 'array',
            prefixItems: [
              { const: 'value' },
              { type: 'number' },
              { type: 'boolean' },
              { type: 'null' }
            ],
            minItems: 4,
            maxItems: 4
          },
          { type: 'null' }
        ]
      },
      handler: async ({ arguments: args }) =>
        args.kind === 'array' ? ['value', 1, true, null] : null
    })

    await mcp.registerResource({
      uri: resourceUri,
      name: 'test-resource',
      title: 'Test resource',
      description: 'Current protocol resource',
      mimeType: 'text/plain',
      size: 13,
      _meta: resourceMeta,
      handler: async () => 'resource-text'
    })

    const server = await mcp.startServer({ port: 0 })
    serverStarted = true
    const endpoint =
      'http://' + server.host + ':' + server.port + server.endpoint

    const discovery = await postModern(
      endpoint,
      'discover',
      'server/discover'
    )
    t.equal(discovery.response.status, 200, 'server discovery succeeds')
    t.ok(
      discovery.payload.result.supportedVersions.includes(
        currentProtocolVersion
      ),
      'current protocol is advertised'
    )
    t.ok(
      discovery.payload.result.supportedVersions.includes(
        latestMcp2025ProtocolVersion
      ),
      'latest handshake protocol is advertised'
    )
    t.ok(
      discovery.payload.result.supportedVersions.includes(
        earlierMcp2025ProtocolVersion
      ),
      'older supported handshake protocol is advertised'
    )
    t.equal(
      discovery.payload.result._meta[
        'io.modelcontextprotocol/serverInfo'
      ].name,
      'oro.runtime',
      'discovery identifies the server'
    )
    t.equal(
      discovery.response.headers.get('mcp-session-id'),
      null,
      'current protocol does not expose a session header'
    )
    t.equal(
      discovery.payload.result.capabilities.resources.subscribe,
      false,
      'discovery does not advertise unavailable resource subscriptions'
    )

    const tools = await postModern(endpoint, 'tools', 'tools/list')
    const tool = tools.payload.result.tools.find(
      (entry) => entry.name === toolName
    )
    t.equal(tool.title, 'Echo input', 'tool title is advertised')
    t.equal(
      tool.outputSchema.properties.echoed.type,
      'string',
      'tool output schema is advertised'
    )
    t.same(tool._meta, toolMeta, 'tool metadata uses the standard _meta field')
    t.equal(tool.metadata, undefined, 'non-standard metadata field is not emitted')
    t.equal(
      tools.payload.result.resultType,
      'complete',
      'list response identifies a complete result'
    )

    const resources = await postModern(
      endpoint,
      'resources',
      'resources/list'
    )
    const resource = resources.payload.result.resources.find(
      (entry) => entry.uri === resourceUri
    )
    t.equal(resource.title, 'Test resource', 'resource title is advertised')
    t.equal(resource.size, 13, 'resource byte size is advertised')
    t.same(
      resource._meta,
      resourceMeta,
      'resource metadata uses the standard _meta field'
    )
    t.equal(
      resource.subscribable,
      undefined,
      'internal subscribable state is not emitted'
    )

    const call = await postModern(
      endpoint,
      'call',
      'tools/call',
      {
        name: toolName,
        arguments: { message: 'hello-world' }
      },
      {
        name: toolName,
        headers: {
          'Mcp-Param-Message': mirrorHeader('hello-world')
        }
      }
    )
    t.equal(call.response.status, 200, 'tool call succeeds')
    t.equal(invocations.length, 1, 'tool handler executes exactly once')
    t.same(
      call.payload.result.structuredContent,
      { echoed: 'hello-world' },
      'structured tool result is returned'
    )
    t.equal(
      call.payload.result._meta['oro.test/result'],
      true,
      'handler result metadata is preserved'
    )
    t.equal(
      call.payload.result._meta[
        'io.modelcontextprotocol/serverInfo'
      ].name,
      'oro.runtime',
      'server result metadata is appended'
    )

    const sentinelLiteral = '=?base64?literal?='
    const unencodedSentinel = await postModern(
      endpoint,
      'unencoded-sentinel',
      'tools/call',
      {
        name: toolName,
        arguments: { message: sentinelLiteral }
      },
      {
        name: toolName,
        headers: {
          'Mcp-Param-Message': sentinelLiteral
        }
      }
    )
    t.equal(
      unencodedSentinel.response.status,
      400,
      'literal Base64 sentinel values must use MCP header encoding'
    )
    t.equal(
      unencodedSentinel.payload.error.code,
      -32020,
      'invalid mirrored value encoding returns HeaderMismatch'
    )
    t.equal(
      invocations.length,
      1,
      'invalid mirrored value encoding never reaches the tool handler'
    )

    const arrayResult = await postModern(
      endpoint,
      'structured-array',
      'tools/call',
      {
        name: structuredToolName,
        arguments: { kind: 'array' }
      },
      { name: structuredToolName }
    )
    t.same(
      arrayResult.payload.result.structuredContent,
      ['value', 1, true, null],
      'structured tool results may be JSON arrays'
    )
    t.ok(
      arrayResult.payload.result.content.some(
        (entry) => entry.type === 'text' &&
          entry.text === '["value",1,true,null]'
      ),
      'array results include serialized text for MCP 2025 model visibility'
    )

    const nullResult = await postModern(
      endpoint,
      'structured-null',
      'tools/call',
      {
        name: structuredToolName,
        arguments: { kind: 'null' }
      },
      { name: structuredToolName }
    )
    t.equal(
      nullResult.payload.result.structuredContent,
      null,
      'structured tool results may be JSON null'
    )
    t.ok(
      nullResult.payload.result.content.some(
        (entry) => entry.type === 'text' && entry.text === 'null'
      ),
      'null results include serialized text for MCP 2025 model visibility'
    )

    const rejected = await postModern(
      endpoint,
      'rejected',
      'tools/call',
      {
        name: toolName,
        arguments: { message: 'reject' }
      },
      {
        name: toolName,
        headers: {
          'Mcp-Param-Message': 'reject'
        }
      }
    )
    t.equal(
      rejected.payload.result.isError,
      true,
      'tool handler exceptions are returned as tool errors'
    )
    t.equal(
      rejected.payload.error,
      undefined,
      'tool handler exceptions do not become protocol errors'
    )

    const invalid = await postModern(
      endpoint,
      'invalid',
      'tools/call',
      { name: toolName, arguments: {} },
      { name: toolName }
    )
    t.equal(invalid.response.status, 200, 'validation failure is an MCP result')
    t.equal(
      invalid.payload.result.isError,
      true,
      'validation result is marked as an error'
    )
    t.equal(invocations.length, 1, 'invalid input never reaches the handler')

    const read = await postModern(
      endpoint,
      'read',
      'resources/read',
      { uri: resourceUri },
      { name: resourceUri }
    )
    t.equal(read.response.status, 200, 'resource read succeeds')
    t.equal(
      read.payload.result.contents[0].uri,
      resourceUri,
      'resource content includes its URI'
    )
    t.equal(
      read.payload.result.contents[0].text,
      'resource-text',
      'resource content includes handler text'
    )
    t.equal(
      read.payload.result.contents[0].type,
      undefined,
      'resource content does not use obsolete type discriminators'
    )

    const missingCapabilitiesResponse = await fetch(endpoint, {
      method: 'POST',
      headers: modernHeaders('tools/list'),
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 'missing-capabilities',
        method: 'tools/list',
        params: {
          _meta: {
            'io.modelcontextprotocol/protocolVersion':
              currentProtocolVersion
          }
        }
      })
    })
    const missingCapabilities = await missingCapabilitiesResponse.json()
    t.equal(
      missingCapabilitiesResponse.status,
      400,
      'missing capabilities return the required HTTP status'
    )
    t.equal(
      missingCapabilities.error.code,
      -32021,
      'missing capabilities use the current protocol error code'
    )
    t.same(
      missingCapabilities.error.data.requiredCapabilities,
      {},
      'missing capability errors identify the required capability set'
    )

    const invalidIdResponse = await fetch(endpoint, {
      method: 'POST',
      headers: modernHeaders('tools/list'),
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: null,
        method: 'tools/list',
        params: modernParams()
      })
    })
    const invalidId = await invalidIdResponse.json()
    t.equal(invalidIdResponse.status, 400, 'null request ids are rejected')
    t.equal(invalidId.error.code, -32600, 'invalid ids use Invalid Request')
    t.equal(invalidId.id, undefined, 'invalid ids are not reflected')

    const unknown = await postModern(endpoint, 'unknown', 'oro/unknown')
    t.equal(
      unknown.response.status,
      404,
      'unknown current method returns HTTP 404'
    )
  } finally {
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
    try {
      await mcp.unregisterResource(resourceUri)
    } catch {}
    try {
      await mcp.unregisterTool(toolName)
    } catch {}
    try {
      await mcp.unregisterTool(structuredToolName)
    } catch {}
  }
})

test('mcp: current subscriptions use a long-lived POST stream', async (t) => {
  const resourceUri = 'oro.mcp.test://subscription'
  const subscribeEvents = []
  const unsubscribeEvents = []
  let serverStarted = false
  let stream = null

  try {
    await mcp.registerResource({
      uri: resourceUri,
      name: 'subscription-resource',
      mimeType: 'text/plain',
      subscribable: true,
      handler: async () => 'subscription-resource',
      onSubscribe: (context) => {
        subscribeEvents.push(context)
      },
      onUnsubscribe: (context) => {
        unsubscribeEvents.push(context)
      }
    })

    const server = await mcp.startServer({ port: 0 })
    serverStarted = true
    const endpoint =
      'http://' + server.host + ':' + server.port + server.endpoint
    const discovery = await postModern(
      endpoint,
      'subscription-discovery',
      'server/discover'
    )
    t.equal(
      discovery.payload.result.capabilities.resources.subscribe,
      true,
      'discovery advertises registered resource subscriptions'
    )

    const invalidListenResponse = await fetch(endpoint, {
      method: 'POST',
      headers: modernHeaders('subscriptions/listen'),
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 'invalid-listen',
        method: 'subscriptions/listen',
        params: modernParams({
          notifications: { toolsListChanged: 'yes' }
        })
      })
    })
    t.equal(
      (invalidListenResponse.headers.get('content-type') || '').startsWith(
        'application/json'
      ),
      true,
      'rejected subscriptions return JSON instead of opening a stream'
    )
    const invalidListen = await invalidListenResponse.json()
    t.equal(
      invalidListen.error.code,
      -32602,
      'invalid subscription filters use Invalid Params'
    )

    const listenId = 'listen-1'
    const response = await fetch(endpoint, {
      method: 'POST',
      headers: modernHeaders('subscriptions/listen'),
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: listenId,
        method: 'subscriptions/listen',
        params: modernParams({
          notifications: {
            resourceSubscriptions: [resourceUri]
          }
        })
      })
    })

    t.equal(response.status, 200, 'subscription stream is accepted')
    t.ok(
      (response.headers.get('content-type') || '').startsWith(
        'text/event-stream'
      ),
      'subscription uses an SSE response to the POST request'
    )

    stream = createSSEReader(response)
    const acknowledgement = await stream.next()
    t.equal(
      acknowledgement.method,
      'notifications/subscriptions/acknowledged',
      'subscription is acknowledged before events'
    )
    t.equal(
      acknowledgement.params._meta[
        'io.modelcontextprotocol/subscriptionId'
      ],
      listenId,
      'acknowledgement identifies the subscription'
    )

    await waitForCount(subscribeEvents, 1)
    t.equal(
      subscribeEvents[0].id,
      listenId,
      'application callback receives the unquoted request identifier'
    )

    const delivered = await mcp.publishResource(resourceUri, 'updated', {
      subscriptionId: subscribeEvents[0].id
    })
    t.equal(delivered, true, 'subscription-scoped notification is delivered')

    const update = await stream.next()
    t.equal(
      update.method,
      'notifications/resources/updated',
      'resource update notification is streamed'
    )
    t.equal(update.params.uri, resourceUri, 'resource update identifies the URI')
    t.equal(
      update.params._meta[
        'io.modelcontextprotocol/subscriptionId'
      ],
      listenId,
      'resource update identifies the subscription'
    )

    await stream.close()
    stream = null
    await waitForCount(unsubscribeEvents, 1)
    t.equal(
      unsubscribeEvents[0].id,
      listenId,
      'closing the stream invokes the unsubscribe callback'
    )
  } finally {
    if (stream) await stream.close()
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
    try {
      await mcp.unregisterResource(resourceUri)
    } catch {}
  }
})

test('mcp: 2025 sessions negotiate supported versions and remain pinned', async (t) => {
  let serverStarted = false

  try {
    const server = await mcp.startServer({ port: 0 })
    serverStarted = true
    const endpoint =
      'http://' + server.host + ':' + server.port + server.endpoint
    for (const protocolVersion of mcp2025ProtocolVersions) {
      const initializeResponse = await fetch(endpoint, {
        method: 'POST',
        headers: {
          Accept: 'application/json, text/event-stream',
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 'initialize-' + protocolVersion,
          method: 'initialize',
          params: {
            protocolVersion,
            capabilities: {},
            clientInfo: {
              name: 'oro-runtime-tests',
              version: '0.1.0'
            }
          }
        })
      })
      const initialized = await initializeResponse.json()
      const sessionId = initializeResponse.headers.get('mcp-session-id')

      t.equal(
        initializeResponse.status,
        200,
        protocolVersion + ' initialize succeeds'
      )
      t.equal(
        initialized.result.protocolVersion,
        protocolVersion,
        protocolVersion + ' is negotiated exactly'
      )
      t.ok(sessionId, protocolVersion + ' initialize returns a session identifier')

      const listResponse = await fetch(endpoint, {
        method: 'POST',
        headers: {
          Accept: 'application/json, text/event-stream',
          'Content-Type': 'application/json',
          'MCP-Protocol-Version': protocolVersion,
          'Mcp-Session-Id': sessionId
        },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 'tools-' + protocolVersion,
          method: 'tools/list',
          params: {}
        })
      })
      const list = await listResponse.json()
      t.equal(listResponse.status, 200, protocolVersion + ' session request succeeds')
      t.ok(Array.isArray(list.result.tools), protocolVersion + ' tools/list returns tools')

      const mismatchedVersion = protocolVersion === latestMcp2025ProtocolVersion
        ? earlierMcp2025ProtocolVersion
        : latestMcp2025ProtocolVersion
      const mismatchResponse = await fetch(endpoint, {
        method: 'POST',
        headers: {
          Accept: 'application/json, text/event-stream',
          'Content-Type': 'application/json',
          'MCP-Protocol-Version': mismatchedVersion,
          'Mcp-Session-Id': sessionId
        },
        body: JSON.stringify({
          jsonrpc: '2.0',
          id: 'mismatch-' + protocolVersion,
          method: 'tools/list',
          params: {}
        })
      })
      const mismatch = await mismatchResponse.json()
      t.equal(mismatchResponse.status, 400, 'session protocol cannot change')
      t.equal(mismatch.error.code, -32020, 'version change is a header mismatch')

      const deleteResponse = await fetch(endpoint, {
        method: 'DELETE',
        headers: {
          'MCP-Protocol-Version': protocolVersion,
          'Mcp-Session-Id': sessionId
        }
      })
      t.equal(deleteResponse.status, 204, protocolVersion + ' session can be terminated')
    }

    const fallbackResponse = await fetch(endpoint, {
      method: 'POST',
      headers: {
        Accept: 'application/json, text/event-stream',
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 'initialize-fallback',
        method: 'initialize',
        params: {
          protocolVersion: '2099-01-01',
          capabilities: {},
          clientInfo: {
            name: 'oro-runtime-tests',
            version: '0.1.0'
          }
        }
      })
    })
    const fallback = await fallbackResponse.json()
    const fallbackSessionId = fallbackResponse.headers.get('mcp-session-id')
    t.equal(fallbackResponse.status, 200, 'unknown initialization offer is negotiated')
    t.equal(
      fallback.result.protocolVersion,
      latestMcp2025ProtocolVersion,
      'unknown initialization offer falls back to the latest handshake protocol'
    )

    const unknownSessionResponse = await fetch(endpoint, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Mcp-Session-Id': 'missing-session'
      },
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 'missing',
        method: 'tools/list',
        params: {}
      })
    })
    t.equal(
      unknownSessionResponse.status,
      404,
      'unknown MCP 2025 sessions are never recreated implicitly'
    )

    const deleteResponse = await fetch(endpoint, {
      method: 'DELETE',
      headers: {
        'MCP-Protocol-Version': latestMcp2025ProtocolVersion,
        'Mcp-Session-Id': fallbackSessionId
      }
    })
    t.equal(deleteResponse.status, 204, 'negotiated fallback session can be terminated')
  } finally {
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
  }
})
