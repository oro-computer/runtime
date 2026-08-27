import test from 'oro:test'
import mcp from 'oro:mcp'
import { fetch } from './http-client.js'

const pkceVerifier = 'dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk'
const pkceChallenge = 'E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM'

test('mcp: public API rejects malformed server and descriptor options', async (t) => {
  await t.rejects(
    mcp.startServer({ port: 1.5 }),
    /port must be a safe integer/,
    'fractional ports are rejected before IPC'
  )
  await t.rejects(
    mcp.startServer({ retry: -1 }),
    /retry must be a positive/,
    'negative retry intervals are rejected before IPC'
  )
  await t.rejects(
    mcp.startServer({ oauth: { redirectUris: [null] } }),
    /redirectUris must be an array of non-empty strings/,
    'invalid OAuth redirect registrations are rejected'
  )
  await t.rejects(
    mcp.registerTool({
      name: 'invalid-icon',
      icons: [{ src: '', theme: 'automatic' }]
    }),
    /icons\[0\]\.src must be a non-empty string/,
    'malformed tool icons are rejected before IPC'
  )
  await t.rejects(
    mcp.registerTool({
      name: 'invalid-output-schema',
      outputSchema: { type: 7 }
    }),
    /not a supported, valid JSON Schema/,
    'invalid JSON Schemas are rejected during tool registration'
  )
  await t.rejects(
    mcp.registerResource({
      uri: 'oro.mcp.test://invalid-annotations',
      annotations: { priority: 2 }
    }),
    /priority must be a number from 0 through 1/,
    'out-of-range resource priorities are rejected before IPC'
  )
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
        redirectUris: ['http://127.0.0.1/callback'],
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
      code_challenge: pkceChallenge,
      code_challenge_method: 'S256',
      resource: `${baseUrl}/mcp-oauth`,
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
    t.same(
      metadataJson.code_challenge_methods_supported,
      ['S256'],
      'metadata advertises only secure PKCE'
    )

    const protectedMetadataPath =
      server.oauth?.protectedResourceMetadataPath ??
      '/.well-known/oauth-protected-resource/mcp-oauth'
    const protectedMetadataResponse = await fetch(
      `${baseUrl}${protectedMetadataPath}`
    )
    t.equal(
      protectedMetadataResponse.status,
      200,
      'protected resource metadata is reachable'
    )
    const protectedMetadata = await protectedMetadataResponse.json()
    t.equal(
      protectedMetadata.resource,
      `${baseUrl}/mcp-oauth`,
      'protected metadata identifies the exact MCP resource'
    )

    const tokenResponse = await fetch(`${baseUrl}${tokenPath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      body: new URLSearchParams({
        grant_type: 'authorization_code',
        code: 'invalid',
        code_verifier: pkceVerifier,
        redirect_uri: 'http://127.0.0.1/callback',
        client_id: 'test-client',
        resource: `${baseUrl}/mcp-oauth`
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
  const redirectUri = 'http://127.0.0.1/callback'
  const clientId = 'test-client'
  const scope = 'scope:read'
  const codeChallenge = pkceChallenge
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
      code_challenge_method: 'S256'
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
        redirectUris: [redirectUri]
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

    const resource = `${baseUrl}/mcp`
    const beginAuthorization = async () => {
      const authorizeResponse = await fetch(
        `${baseUrl}${authorizePath}?${buildAuthorizeForm({ resource }).toString()}`
      )
      t.equal(
        authorizeResponse.status,
        200,
        'authorize GET returns approval screen'
      )
      const html = await authorizeResponse.text()
      const match = html.match(
        /name="authorization_request" value="([A-Za-z0-9]+)"/
      )
      t.ok(match, 'approval screen contains a one-time authorization request')
      return match?.[1] ?? ''
    }

    const authorizationRequest = await beginAuthorization()

    const missingDecisionResponse = await fetch(`${baseUrl}${authorizePath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      body: new URLSearchParams({
        authorization_request: authorizationRequest
      }).toString()
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
      body: new URLSearchParams({
        authorization_request: authorizationRequest,
        decision: 'deny'
      }).toString()
    })
    t.equal(denyResponse.status, 302, 'deny request redirects')
    const denyLocation = denyResponse.headers.get('location') ?? ''
    t.ok(
      denyLocation.includes('error=access_denied'),
      'deny redirect includes error marker'
    )
    await denyResponse.text()

    const approvalRequest = await beginAuthorization()
    const approveResponse = await fetch(`${baseUrl}${authorizePath}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      },
      redirect: 'manual',
      body: new URLSearchParams({
        authorization_request: approvalRequest,
        decision: 'approve'
      }).toString()
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
    t.equal(
      callbackUrl.searchParams.get('iss'),
      baseUrl,
      'authorization response identifies its issuer'
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
        code_verifier: pkceVerifier,
        resource
      }).toString()
    })

    t.equal(tokenResponse.status, 200, 'token endpoint returns success')
    const tokenPayload = await tokenResponse.json()
    t.equal(typeof tokenPayload.access_token, 'string', 'access token issued')
    t.equal(tokenPayload.token_type, 'Bearer', 'token type is Bearer')
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
        redirectUris: ['http://127.0.0.1/callback'],
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
        code_challenge: pkceChallenge,
        code_challenge_method: 'S256',
        resource: `${baseUrl}/mcp`
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
    const response = await fetch(`${baseUrl}${expectedPath}`, {
      method: 'POST',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/json',
        'MCP-Protocol-Version': '2026-07-28',
        'Mcp-Method': 'server/discover'
      },
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 'normalize-discover',
        method: 'server/discover',
        params: {
          _meta: {
            'io.modelcontextprotocol/protocolVersion': '2026-07-28',
            'io.modelcontextprotocol/clientCapabilities': {}
          }
        }
      })
    })
    const discovered = await response.json()
    t.equal(response.status, 200, 'normalized endpoint accepts MCP requests')
    t.equal(
      discovered.id,
      'normalize-discover',
      'normalized endpoint returns the matching response'
    )
  } finally {
    if (serverStarted) {
      try {
        await mcp.stopServer()
      } catch {}
    }
  }
})
