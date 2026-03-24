import test from 'oro:test'
import { fetch } from 'oro:fetch'
import mcp from 'oro:mcp'

const protocolVersion = '2024-11-05'

function createSSEClient (t, url) {
  const decoder = new TextDecoder()
  let buffer = ''
  let reader = null

  async function init () {
    const response = await fetch(url, {
      headers: {
        Accept: 'text/event-stream'
      }
    })

    t.equal(response.status, 200, 'SSE connection accepted')

    reader = response.body.getReader()
  }

  const client = {
    async connect () {
      await init()
      return client
    },

    async nextEvent () {
      while (true) {
        const marker = buffer.indexOf('\n\n')
        if (marker !== -1) {
          const block = buffer.slice(0, marker)
          buffer = buffer.slice(marker + 2)
          return parseEvent(block)
        }

        const { done, value } = await reader.read()
        if (done) return null
        const chunk = decoder
          .decode(value, { stream: true })
          .replace(/\r\n/g, '\n')
          .replace(/\r/g, '\n')
        buffer += chunk
      }
    },

    async close () {
      if (!reader) return
      try {
        await reader.cancel()
      } catch {}
    }
  }

  return client
}

function parseEvent (raw) {
  const normalized = raw.replace(/\r\n/g, '\n').replace(/\r/g, '\n')
  const lines = normalized.split('\n')
  let event = 'message'
  const dataLines = []

  for (const line of lines) {
    if (!line) continue
    if (line.startsWith('event:')) {
      event = line.slice(6).trim()
    } else if (line.startsWith('data:')) {
      dataLines.push(line.slice(5).trim())
    }
  }

  return {
    event,
    data: dataLines.join('\n')
  }
}

function extractSessionId (endpointData) {
  if (typeof endpointData !== 'string') return ''
  const marker = 'session_id='
  const index = endpointData.indexOf(marker)
  if (index === -1) return ''
  const start = index + marker.length
  const end = endpointData.indexOf('&', start)
  return end === -1 ? endpointData.slice(start) : endpointData.slice(start, end)
}

async function postJSON (t, url, payload) {
  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(payload)
  })

  t.equal(response.status, 202, 'Message accepted')
  await response.text()
}

async function waitForUnsubscribeEvent (events, expectedCount, timeout = 2000) {
  const start = Date.now()
  while (true) {
    if (events.length >= expectedCount) {
      return events[expectedCount - 1]
    }
    if (Date.now() - start > timeout) {
      throw new Error('Timed out waiting for unsubscribe event')
    }
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
}

test('mcp: tool invocation and resource read over HTTP bridge', async (t) => {
  const toolInvocations = []
  const subscribeEvents = []
  const unsubscribeEvents = []

  const toolName = 'oro.mcp.test.echo'
  const resourceUri = 'oro.mcp.test://resource'
  const toolMetadata = { version: 1, tags: ['echo'] }
  const resourceMetadata = { scope: 'test', binary: false }
  const toolInputSchema = {
    type: 'object',
    properties: {
      message: {
        type: 'string',
        description: 'Text to echo back'
      }
    },
    required: ['message']
  }

  const cleanup = []
  let serverStopped = false

  try {
    const toolId = await mcp.registerTool({
      name: toolName,
      description: 'Echo test tool',
      metadata: toolMetadata,
      inputSchema: toolInputSchema,
      handler: async ({ arguments: args }) => {
        toolInvocations.push(args)
        return {
          echoed: args.message
        }
      }
    })
    t.ok(toolId, 'tool registered')
    cleanup.push(() => mcp.unregisterTool(toolName))

    const resourceId = await mcp.registerResource({
      uri: resourceUri,
      name: 'Test Resource',
      description: 'Returns text for tests',
      metadata: resourceMetadata,
      handler: async () => 'resource-text',
      onSubscribe: async (context) => {
        subscribeEvents.push(context)
        await mcp.publishResource(
          resourceUri,
          {
            contents: [{ type: 'text', text: 'subscription-update' }]
          },
          {
            subscriptionId: context.id
          }
        )
      },
      onUnsubscribe: (context) => {
        unsubscribeEvents.push(context)
      }
    })
    t.ok(resourceId, 'resource registered')
    cleanup.push(() => mcp.unregisterResource(resourceUri))

    const server = await mcp.startServer({ port: 0 })
    t.ok(server.running, 'server started')
    cleanup.push(async () => {
      if (!serverStopped) {
        await mcp.stopServer()
      }
    })

    const baseUrl = `http://${server.host}:${server.port}`

    const sse = createSSEClient(t, `${baseUrl}/sse`)
    await sse.connect()
    cleanup.push(() => sse.close())

    const messageQueue = []

    async function takeMessage () {
      if (messageQueue.length > 0) {
        return messageQueue.shift()
      }

      while (true) {
        const event = await sse.nextEvent()
        if (!event) return null
        if (!event.data) {
          continue
        }
        try {
          return JSON.parse(event.data)
        } catch {}
      }
    }

    function stashMessage (json) {
      messageQueue.push(json)
    }

    async function waitForResponse (expectedId) {
      while (true) {
        const json = await takeMessage()
        if (!json) return null
        if (json.id === expectedId) {
          return json
        }
        stashMessage(json)
      }
    }

    async function waitForUpdateText (expected) {
      while (true) {
        const json = await takeMessage()
        if (!json) return null
        if (json.method === 'resources/update') {
          const contents = json?.params?.contents ?? []
          const first = Array.isArray(contents) ? contents[0] : null
          if (first && first.text === expected) {
            return json
          }
        }
        stashMessage(json)
      }
    }

    const endpointEvent = await sse.nextEvent()
    t.equal(endpointEvent?.event, 'endpoint', 'received endpoint event')
    t.ok(
      endpointEvent?.data.includes('?session_id='),
      'endpoint event includes session id'
    )

    const messageUrl = `${baseUrl}${endpointEvent.data}`
    const sessionId = extractSessionId(endpointEvent.data)

    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: '1',
      method: 'initialize',
      params: {
        protocolVersion
      }
    })

    const initPayload = await takeMessage()
    t.ok(initPayload, 'initialize response received')
    t.equal(initPayload.jsonrpc, '2.0', 'jsonrpc version returned')
    t.equal(initPayload.id, '1', 'initialize response id matches')
    t.ok(initPayload.result, 'initialize response includes result payload')
    t.equal(
      initPayload.result.protocolVersion,
      protocolVersion,
      'protocol version negotiated'
    )
    const capabilities = initPayload.result.capabilities
    t.ok(
      capabilities?.resources?.subscribe?.enabled,
      'capabilities advertise resource subscription support'
    )
    t.ok(
      capabilities?.tools?.call?.enabled,
      'capabilities advertise tool invocation support'
    )

    const advertisedToolEntry = initPayload.result.tools.find(
      (value) => value.name === toolName
    )
    t.same(
      advertisedToolEntry?.metadata,
      toolMetadata,
      'tool metadata preserved in handshake'
    )
    t.equal(
      advertisedToolEntry?.inputSchema?.properties?.message?.type,
      'string',
      'tool input schema advertised'
    )

    const advertisedResourceEntry = initPayload.result.resources.find(
      (value) => value.uri === resourceUri
    )
    t.same(
      advertisedResourceEntry?.metadata,
      resourceMetadata,
      'resource metadata preserved in handshake'
    )

    const advertisedTools = initPayload.result.tools.map((value) => value.name)
    t.ok(advertisedTools.includes(toolName), 'tool advertised to client')

    const advertisedResources = initPayload.result.resources.map(
      (value) => value.uri
    )
    t.ok(
      advertisedResources.includes(resourceUri),
      'resource advertised to client'
    )

    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: '2',
      method: 'tools/call',
      params: {
        name: toolName,
        arguments: {
          message: 'hello-world'
        }
      }
    })

    const toolPayload = await takeMessage()
    t.ok(toolPayload, 'tool invocation response delivered')
    t.equal(toolPayload.id, '2', 'tool invocation response id matches')
    t.ok(toolPayload.result, 'tool invocation response includes result payload')
    t.ok(
      Array.isArray(toolInvocations) && toolInvocations.length === 1,
      'tool handler executed exactly once'
    )
    t.equal(
      toolInvocations[0].message,
      'hello-world',
      'tool handler received arguments'
    )

    const toolResultJson = toolPayload.result.result
    t.ok(
      typeof toolResultJson === 'string',
      'tool invocation result encoded as JSON string'
    )
    const toolResult = JSON.parse(toolResultJson)
    t.same(
      toolResult,
      { echoed: 'hello-world' },
      'tool response payload delivered to client'
    )

    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: '3',
      method: 'resources/read',
      params: {
        uri: resourceUri
      }
    })

    const resourcePayload = await takeMessage()
    t.ok(resourcePayload, 'resource read response delivered')
    t.equal(resourcePayload.id, '3', 'resource read response id matches')
    t.ok(
      resourcePayload.result,
      'resource read response includes result payload'
    )
    const [content] = resourcePayload.result.contents
    t.equal(content.type, 'text', 'resource response emitted text content')
    t.equal(
      content.text,
      'resource-text',
      'resource response text matches handler output'
    )

    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: '4',
      method: 'resources/subscribe',
      params: {
        uri: resourceUri
      }
    })

    let subscribeAck = null
    let subscriptionUpdate = null

    while (!subscribeAck || !subscriptionUpdate) {
      const json = await takeMessage()
      if (!json) break

      if (json.id === '4') {
        subscribeAck = json
        continue
      }

      if (json.method === 'resources/update' && !subscriptionUpdate) {
        subscriptionUpdate = json
        continue
      }

      stashMessage(json)
    }

    t.ok(subscribeAck, 'subscription acknowledgement delivered')
    const subscriptionId = subscribeAck?.result?.subscription?.id ?? ''
    t.ok(subscriptionId, 'subscription id returned to client')
    t.equal(subscribeEvents.length, 1, 'subscribe handler invoked once')
    t.equal(
      subscribeEvents[0]?.id,
      subscriptionId,
      'subscribe handler context matches subscription id'
    )
    t.equal(
      subscribeEvents[0]?.sessionId,
      sessionId,
      'subscribe handler context session id matches'
    )
    t.same(
      subscribeEvents[0]?.descriptor?.metadata,
      resourceMetadata,
      'subscribe handler descriptor includes metadata'
    )

    const subscriptionContents = subscriptionUpdate?.params?.contents ?? []
    t.ok(
      Array.isArray(subscriptionContents),
      'subscription update includes contents array'
    )
    const subscriptionText = subscriptionContents[0]?.text
    t.equal(
      subscriptionText,
      'subscription-update',
      'subscribe handler pushed initial update'
    )

    const manualDelivered = await mcp.publishResource(
      resourceUri,
      'manual-update',
      {
        subscriptionId
      }
    )
    t.ok(
      manualDelivered,
      'publishResource (subscription-scoped) reported delivery'
    )

    const manualUpdate = await waitForUpdateText('manual-update')
    t.ok(manualUpdate, 'manual publish delivered update to client')

    const broadcastDelivered = await mcp.publishResource(
      resourceUri,
      {
        contents: [{ type: 'text', text: 'session-update' }]
      },
      {
        sessionId
      }
    )
    t.ok(broadcastDelivered, 'session-scoped publish reported delivery')

    const broadcastUpdate = await waitForUpdateText('session-update')
    t.ok(broadcastUpdate, 'session-scoped publish produced update event')

    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: '5',
      method: 'resources/unsubscribe',
      params: {
        subscription: {
          id: subscriptionId
        }
      }
    })

    const unsubscribeAck = await waitForResponse('5')
    t.ok(unsubscribeAck, 'unsubscribe acknowledgement delivered')
    t.equal(
      unsubscribeAck?.result?.subscription?.id,
      subscriptionId,
      'unsubscribe response references subscription id'
    )
    t.equal(unsubscribeEvents.length, 1, 'unsubscribe handler invoked once')
    t.equal(
      unsubscribeEvents[0]?.id,
      subscriptionId,
      'unsubscribe handler context matches subscription id'
    )
    t.equal(
      unsubscribeEvents[0]?.sessionId,
      sessionId,
      'unsubscribe handler context session id matches'
    )

    const afterUnsubscribeDelivered = await mcp.publishResource(
      resourceUri,
      'post-unsubscribe',
      {
        subscriptionId
      }
    )
    t.equal(
      afterUnsubscribeDelivered,
      false,
      'publishResource returns false when no subscribers remain'
    )

    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: '6',
      method: 'resources/subscribe',
      params: {
        uri: resourceUri
      }
    })

    const lingeringAck = await waitForResponse('6')
    t.ok(lingeringAck, 'second subscription acknowledgement delivered')
    const lingeringSubscriptionId = lingeringAck?.result?.subscription?.id ?? ''
    t.ok(lingeringSubscriptionId, 'second subscription id returned to client')
    t.equal(
      subscribeEvents.length,
      2,
      'subscribe handler invoked for lingering subscription'
    )
    t.same(
      subscribeEvents[1]?.descriptor?.metadata,
      resourceMetadata,
      'lingering subscribe descriptor preserves metadata'
    )

    const lingeringUpdate = await waitForUpdateText('subscription-update')
    t.ok(lingeringUpdate, 'lingering subscription received initial update')

    const stopResult = await mcp.stopServer()
    t.ok(stopResult, 'server stopped via API')
    serverStopped = true

    const stopUnsubscribe = await waitForUnsubscribeEvent(unsubscribeEvents, 2)
    t.equal(
      stopUnsubscribe?.id,
      lingeringSubscriptionId,
      'server stop unsubscribe references lingering subscription'
    )
    t.equal(
      stopUnsubscribe?.params?.reason,
      'server-stopped',
      'server stop unsubscribe includes reason'
    )
  } finally {
    for (const action of cleanup.reverse()) {
      try {
        await action()
      } catch {}
    }
  }
})

test('mcp: oauth endpoints are reachable on normalized paths', async (t) => {
  const oauthScreenHtml = '<!doctype html><html><body>Authorize</body></html>'
  let serverStarted = false

  try {
    const server = await mcp.startServer({
      port: 0,
      endpoint: 'http://localhost/mcp-oauth/',
      oauth: {
        authorizePath: 'http://localhost/mcp/oauth/authorize?ignored=1',
        tokenPath: '/mcp/oauth/token/',
        metadataPath: '/mcp/.well-known/oauth-authorization-server',
        defaultClientId: 'test-client',
        defaultScope: 'scope:read',
        screen: { html: oauthScreenHtml }
      }
    })
    serverStarted = true

    t.ok(server.running, 'server started with oauth configuration')
    const baseUrl = `http://${server.host}:${server.port}`
    const authorizePath =
      server.oauth?.authorizePath && server.oauth.authorizePath.length > 0
        ? server.oauth.authorizePath
        : '/oauth/authorize'
    const tokenPath =
      server.oauth?.tokenPath && server.oauth.tokenPath.length > 0
        ? server.oauth.tokenPath
        : '/oauth/token'
    const metadataPath =
      server.oauth?.metadataPath && server.oauth.metadataPath.length > 0
        ? server.oauth.metadataPath
        : '/.well-known/oauth-authorization-server'

    const params = new URLSearchParams({
      response_type: 'code',
      client_id: 'test-client',
      redirect_uri: 'http://127.0.0.1/callback',
      scope: 'scope:read',
      code_challenge: 'demo-challenge',
      code_challenge_method: 'plain',
      state: 'demo-state'
    })

    const authorizeResponse = await fetch(
      `${baseUrl}${authorizePath}?${params.toString()}`
    )
    t.equal(
      authorizeResponse.status,
      200,
      'authorize endpoint responds with HTML'
    )
    await authorizeResponse.text()

    const metadataResponse = await fetch(`${baseUrl}${metadataPath}`)
    t.equal(metadataResponse.status, 200, 'metadata endpoint is reachable')
    const metadataJson = await metadataResponse.json()
    t.equal(
      metadataJson.authorization_endpoint,
      `${baseUrl}${authorizePath}`,
      'metadata advertises normalized authorize endpoint'
    )

    const tokenResponse = await fetch(`${baseUrl}${tokenPath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      body: new URLSearchParams({
        grant_type: 'authorization_code',
        code: 'invalid',
        code_verifier: 'demo-challenge',
        redirect_uri: 'http://127.0.0.1/callback'
      }).toString()
    })
    t.notEqual(tokenResponse.status, 404, 'token endpoint does not return 404')
    await tokenResponse.text()
  } finally {
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
  }
})

test('mcp: oauth authorization requires explicit approval and issues tokens', async (t) => {
  const oauthScreenHtml = '<!doctype html><html><body>Authorize</body></html>'
  const redirectUri = 'http://127.0.0.1/callback'
  const clientId = 'test-client'
  const scope = 'scope:read'
  const codeChallenge = 'demo-challenge'
  const state = 'demo-state'
  let serverStarted = false

  const buildAuthorizeForm = (overrides = {}) => {
    const params = new URLSearchParams({
      response_type: 'code',
      client_id: clientId,
      redirect_uri: redirectUri,
      scope,
      state,
      code_challenge: codeChallenge,
      code_challenge_method: 'plain'
    })

    for (const [key, value] of Object.entries(overrides)) {
      params.set(key, value)
    }

    return params
  }

  try {
    const server = await mcp.startServer({
      port: 0,
      oauth: {
        defaultClientId: clientId,
        defaultScope: scope,
        screen: { html: oauthScreenHtml }
      }
    })
    serverStarted = true

    const baseUrl = `http://${server.host}:${server.port}`
    const authorizePath =
      server.oauth?.authorizePath && server.oauth.authorizePath.length > 0
        ? server.oauth.authorizePath
        : '/oauth/authorize'
    const tokenPath =
      server.oauth?.tokenPath && server.oauth.tokenPath.length > 0
        ? server.oauth.tokenPath
        : '/oauth/token'

    const authorizeResponse = await fetch(
      `${baseUrl}${authorizePath}?${buildAuthorizeForm().toString()}`
    )
    t.equal(
      authorizeResponse.status,
      200,
      'authorize GET returns approval screen'
    )
    await authorizeResponse.text()

    const missingDecisionResponse = await fetch(`${baseUrl}${authorizePath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      body: buildAuthorizeForm().toString()
    })
    t.equal(
      missingDecisionResponse.status,
      400,
      'missing decision request rejected'
    )
    await missingDecisionResponse.text()

    const denyResponse = await fetch(`${baseUrl}${authorizePath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      redirect: 'manual',
      body: buildAuthorizeForm({ decision: 'deny' }).toString()
    })
    t.equal(denyResponse.status, 302, 'deny request redirects')
    const denyLocation = denyResponse.headers.get('location') ?? ''
    t.ok(
      denyLocation.includes('error=access_denied'),
      'deny redirect includes error marker'
    )
    await denyResponse.text()

    const approveResponse = await fetch(`${baseUrl}${authorizePath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      redirect: 'manual',
      body: buildAuthorizeForm({ decision: 'approve' }).toString()
    })
    t.equal(approveResponse.status, 302, 'approve request redirects')
    const approveLocation = approveResponse.headers.get('location') ?? ''
    t.ok(
      approveLocation.includes('code='),
      'approve redirect includes authorization code'
    )
    const callbackUrl = new URL(approveLocation)
    const authorizationCode = callbackUrl.searchParams.get('code')
    t.ok(authorizationCode, 'authorization code provided')
    t.equal(
      callbackUrl.searchParams.get('state'),
      state,
      'state value preserved in redirect'
    )
    await approveResponse.text()

    const tokenResponse = await fetch(`${baseUrl}${tokenPath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      body: new URLSearchParams({
        grant_type: 'authorization_code',
        code: authorizationCode,
        redirect_uri: redirectUri,
        client_id: clientId,
        code_verifier: codeChallenge
      }).toString()
    })

    t.equal(tokenResponse.status, 200, 'token endpoint returns success')
    const tokenPayload = await tokenResponse.json()
    t.type(tokenPayload.access_token, 'string', 'access token issued')
    t.equal(tokenPayload.token_type, 'bearer', 'token type is bearer')
    t.equal(tokenPayload.scope, scope, 'scope propagated to token response')
  } finally {
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
  }
})

test('mcp: oauth rejects redirect URIs containing newlines', async (t) => {
  const oauthScreenHtml = '<!doctype html><html><body>Authorize</body></html>'
  let serverStarted = false

  try {
    const server = await mcp.startServer({
      port: 0,
      oauth: {
        defaultClientId: 'test-client',
        defaultScope: 'scope:read',
        screen: { html: oauthScreenHtml }
      }
    })
    serverStarted = true

    const baseUrl = `http://${server.host}:${server.port}`
    const authorizePath =
      server.oauth?.authorizePath && server.oauth.authorizePath.length > 0
        ? server.oauth.authorizePath
        : '/oauth/authorize'

    const response = await fetch(
      `${baseUrl}${authorizePath}?${new URLSearchParams({
        response_type: 'code',
        client_id: 'test-client',
        redirect_uri: 'http://127.0.0.1/callback\r\nnext',
        scope: 'scope:read',
        code_challenge: 'demo-challenge',
        code_challenge_method: 'plain'
      }).toString()}`
    )

    t.equal(
      response.status,
      400,
      'authorize request with newline redirect is rejected'
    )
    await response.text()
  } finally {
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
  }
})

test('mcp: startServer normalizes full endpoint URLs', async (t) => {
  const fullEndpoint = 'http://example.com/mcp-full-url/?ignored=true'
  const expectedPath = '/mcp-full-url'
  let sseClient = null
  let serverStarted = false

  try {
    const server = await mcp.startServer({
      port: 0,
      endpoint: fullEndpoint
    })
    serverStarted = true

    t.ok(server.running, 'server started when full URL endpoint provided')
    t.equal(
      server.endpoint,
      expectedPath,
      'endpoint normalized to path component'
    )

    const baseUrl = `http://${server.host}:${server.port}`
    sseClient = createSSEClient(t, `${baseUrl}${expectedPath}`)
    await sseClient.connect()

    const readyEvent = await sseClient.nextEvent()
    t.equal(
      readyEvent?.event,
      'ready',
      'ready event delivered for normalized endpoint'
    )
    let readyPayload = null
    if (readyEvent?.data) {
      try {
        readyPayload = JSON.parse(readyEvent.data)
      } catch {}
    }
    t.equal(
      readyPayload?.endpoint,
      expectedPath,
      'ready payload advertises normalized endpoint'
    )

    const endpointEvent = await sseClient.nextEvent()
    t.equal(endpointEvent?.event, 'endpoint', 'legacy endpoint event delivered')
    t.ok(
      endpointEvent?.data.startsWith(`${expectedPath}?session_id=`),
      'legacy endpoint uses normalized path'
    )

    const messageUrl = `${baseUrl}${endpointEvent.data}`
    await postJSON(t, messageUrl, {
      jsonrpc: '2.0',
      id: 'normalize-init',
      method: 'initialize',
      params: {
        protocolVersion
      }
    })

    let initResponse = null
    while (!initResponse) {
      const event = await sseClient.nextEvent()
      if (!event) break
      if (event.event !== 'message' || !event.data) {
        continue
      }
      try {
        const json = JSON.parse(event.data)
        if (json.id === 'normalize-init') {
          initResponse = json
          break
        }
      } catch {}
    }

    t.ok(initResponse, 'initialize response received over SSE')
    t.equal(
      initResponse?.result?.protocolVersion,
      protocolVersion,
      'protocol version echoed back to client'
    )
  } finally {
    if (sseClient) {
      try {
        await sseClient.close()
      } catch {}
    }
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
  }
})
