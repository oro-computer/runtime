import application from 'oro:application'
import hooks from 'oro:hooks'
import mcp from 'oro:mcp'
import os from 'oro:os'

import React from '../ui/react.js'
import clipboard from 'oro:clipboard'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleSection,
  ExampleStack,
  ExampleProse,
  Button,
  InlineCode,
  StatusBanner,
  StatusPill,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  Code,
  LogViewer,
  EmptyState,
  Badge
} from '../ui/index.js'

const {
  createElement: h,
  useState,
  useMemo,
  useCallback,
  useEffect,
  useRef
} = React

const STATUS_RESOURCE_URI = 'oro.examples.mcp://status'
const ACTIVITY_RESOURCE_URI = 'oro.examples.mcp://activity'
const MAX_ACTIVITY_ENTRIES = 50
const STATUS_REFRESH_INTERVAL_MS = 20000
const DEFAULT_OAUTH_CLIENT_ID = 'oro-mcp-example-client'
const DEFAULT_OAUTH_SCOPE = 'activity:read tools:invoke'
const OAUTH_AUTHORIZE_PATH = '/mcp/oauth/authorize'
const OAUTH_TOKEN_PATH = '/mcp/oauth/token'
const OAUTH_METADATA_PATH = '/mcp/.well-known/oauth-authorization-server'
const OAUTH_DEMO_REDIRECT_URI = 'http://127.0.0.1:43110/callback'
const OAUTH_DEMO_CODE_CHALLENGE = 'demo-challenge'
const DEFAULT_OAUTH_SCREEN_HTML = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Oro MCP Authorization</title>
<style>
  body{font-family:system-ui,-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:32px;background:#0f172a;color:#f8fafc;}
  main{max-width:480px;margin:0 auto;background:#111827;padding:32px;border-radius:18px;box-shadow:0 18px 48px rgba(15,23,42,0.45);}
  h1{font-size:1.75rem;margin:0 0 16px;}
  p{margin:0 0 12px;line-height:1.5;color:#cbd5f5;}
  .info{background:#1e293b;border-radius:12px;padding:16px;margin:20px 0;}
  .info dt{color:#94a3b8;font-weight:600;}
  .info dd{margin:0 0 12px;}
  button{display:inline-flex;align-items:center;justify-content:center;padding:12px 20px;border-radius:999px;border:none;font-size:1rem;font-weight:600;margin-right:12px;cursor:pointer;}
  button[name="decision"][value="approve"]{background:#22d3ee;color:#0f172a;}
  button[name="decision"][value="deny"]{background:transparent;color:#f8fafc;border:1px solid #334155;}
</style>
</head>
<body>
<main>
  <h1>Authorize MCP Client</h1>
  <p><strong>{{CLIENT_ID}}</strong> is requesting access to the local MCP server.</p>
  <div class="info">
    <dl>
      <dt>Requested scope</dt>
      <dd><code>{{SCOPE}}</code></dd>
      <dt>Redirect URI</dt>
      <dd>{{REDIRECT_URI}}</dd>
    </dl>
  </div>
  <p>Select an option below to continue.</p>
  <button type="submit" name="decision" value="approve">Approve</button>
  <button type="submit" name="decision" value="deny">Deny</button>
</main>
</body>
</html>`

const appConfig = application?.config || {}
const configuredHost =
  typeof appConfig.mcp_host === 'string' && appConfig.mcp_host.length > 0
    ? appConfig.mcp_host
    : null
const configuredEndpoint =
  typeof appConfig.mcp_endpoint === 'string' &&
  appConfig.mcp_endpoint.length > 0
    ? appConfig.mcp_endpoint
    : null
const configuredPortValue = Number.parseInt(appConfig.mcp_port, 10)
const configuredPort = Number.isFinite(configuredPortValue)
  ? configuredPortValue
  : null
const configuredRetryValue = Number.parseInt(appConfig.mcp_retry, 10)
const configuredRetry = Number.isFinite(configuredRetryValue)
  ? configuredRetryValue
  : null
const configuredToken =
  typeof appConfig.mcp_token === 'string' && appConfig.mcp_token.length > 0
    ? appConfig.mcp_token
    : null

function generateToken () {
  const bytes = new Uint8Array(16)
  crypto.getRandomValues(bytes)
  return Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join(
    ''
  )
}

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function resolvePlatform () {
  try {
    if (typeof os?.platform === 'function') {
      return os.platform()
    }
  } catch {}
  try {
    if (typeof os?.type === 'function') {
      return os.type()
    }
  } catch {}
  return 'unknown'
}

function buildEndpointUrl (info) {
  if (!info) return 'Server not ready'
  const base = `http://${info.host}:${info.port}`
  return `${base}${info.endpoint || '/mcp'}`
}

function useMcpControl () {
  const [statusMessage, setStatusMessage] = useState({
    variant: 'info',
    title: 'Waiting for runtime readiness',
    message: 'The MCP server will start automatically when the app is ready.'
  })
  const [serverInfo, setServerInfo] = useState(null)
  const [endpoint, setEndpoint] = useState('Waiting for session…')
  const [registeredTools, setRegisteredTools] = useState([])
  const [activity, setActivity] = useState([])
  const [latestSnapshot, setLatestSnapshot] = useState(null)
  const [errorMessage, setErrorMessage] = useState(null)
  const [isRunning, setIsRunning] = useState(false)
  const [isReady, setIsReady] = useState(false)
  const [windowVisible, setWindowVisible] = useState(true)
  const [oauthDetails, setOauthDetails] = useState(null)

  const windowRef = useRef(null)
  const statusTimerRef = useRef(null)
  const authTokenRef = useRef(configuredToken || generateToken())
  const activityRef = useRef([])
  const serverInfoRef = useRef(null)
  const registeredToolsRef = useRef([])
  const serverEndpointRef = useRef('Waiting for session…')
  const startTimeRef = useRef(Date.now())
  const windowVisibleRef = useRef(true)
  const startingRef = useRef(false)

  const updateStatus = useCallback((variant, title, message) => {
    setStatusMessage({ variant, title, message })
  }, [])

  const appendActivity = useCallback(async (kind, message, details) => {
    const entry = {
      id: `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`,
      timestamp: new Date().toISOString(),
      kind,
      message,
      details
    }
    const next = [...activityRef.current, entry]
    activityRef.current =
      next.length > MAX_ACTIVITY_ENTRIES
        ? next.slice(next.length - MAX_ACTIVITY_ENTRIES)
        : next
    setActivity([...activityRef.current])
    try {
      await mcp.publishResource(ACTIVITY_RESOURCE_URI, JSON.stringify(entry))
    } catch (error) {
      console.warn(
        '[examples/mcp-control] Failed to publish activity update',
        error
      )
    }
    return entry
  }, [])

  const getWindow = useCallback(async () => {
    if (windowRef.current) return windowRef.current
    const win = await application.getCurrentWindow()
    windowRef.current = win
    return win
  }, [])

  const safeInvoke = useCallback(async (fn, fallback = null) => {
    try {
      return await fn()
    } catch (error) {
      console.warn('[examples/mcp-control] Ignored window query failure', error)
      return fallback
    }
  }, [])

  const collectStatus = useCallback(async () => {
    const win = await getWindow()
    const [size, position, title] = await Promise.all([
      safeInvoke(() => win.getSize()),
      safeInvoke(() => win.getPosition()),
      safeInvoke(() => win.getTitle(), '')
    ])

    return {
      title,
      size,
      position,
      visible: windowVisibleRef.current,
      platform: resolvePlatform(),
      registeredTools: [...registeredToolsRef.current],
      resources: [STATUS_RESOURCE_URI, ACTIVITY_RESOURCE_URI],
      server: serverInfoRef.current,
      uptimeSeconds: Number(
        ((Date.now() - startTimeRef.current) / 1000).toFixed(1)
      ),
      updatedAt: new Date().toISOString()
    }
  }, [getWindow, safeInvoke])

  const refreshStatus = useCallback(async () => {
    try {
      const snapshot = await collectStatus()
      setLatestSnapshot(snapshot)
      try {
        await mcp.publishResource(
          STATUS_RESOURCE_URI,
          JSON.stringify(snapshot, null, 2)
        )
      } catch (error) {
        console.warn(
          '[examples/mcp-control] Failed to publish status resource',
          error
        )
      }
      return snapshot
    } catch (error) {
      const message = formatError(error)
      console.warn('[examples/mcp-control] Failed to collect status', message)
      return null
    }
  }, [collectStatus])

  const registerResources = useCallback(() => {
    return Promise.allSettled([
      mcp.registerResource({
        uri: STATUS_RESOURCE_URI,
        name: 'Application Status',
        description:
          'Live snapshot of the window and server state (JSON encoded).',
        mimeType: 'application/json',
        metadata: {
          example: 'JSON.stringify({ title, size, position, platform })',
          format: 'JSON text'
        },
        handler: async () => JSON.stringify(await collectStatus(), null, 2)
      }),
      mcp.registerResource({
        uri: ACTIVITY_RESOURCE_URI,
        name: 'Activity Stream',
        description: 'Recent control center activity rendered as JSON lines.',
        mimeType: 'application/json',
        subscribable: true,
        metadata: {
          format: 'JSON Lines',
          entry: '{ timestamp, kind, message, details }'
        },
        handler: async () => {
          if (activityRef.current.length === 0) return '[]'
          return activityRef.current
            .map((entry) => JSON.stringify(entry))
            .join('\n')
        },
        onSubscribe: async (context) => {
          if (activityRef.current.length > 0) {
            const backlog = activityRef.current
              .slice(-10)
              .map((entry) => JSON.stringify(entry))
              .join('\n')
            try {
              await mcp.publishResource(ACTIVITY_RESOURCE_URI, backlog, {
                subscriptionId: context.id
              })
            } catch (error) {
              console.warn(
                '[examples/mcp-control] Failed to publish backlog',
                error
              )
            }
          }
          if (serverEndpointRef.current) {
            setEndpoint(serverEndpointRef.current)
          }
          await appendActivity(
            'session',
            'Activity stream subscription opened',
            {
              sessionId: context.sessionId
            }
          )
        },
        onUnsubscribe: async (context) => {
          await appendActivity(
            'session',
            'Activity stream subscription closed',
            {
              sessionId: context.sessionId
            }
          )
        }
      })
    ])
  }, [appendActivity, collectStatus])

  const registerTool = useCallback(
    (tool) => {
      return mcp
        .registerTool({
          ...tool,
          handler: async (context) => {
            try {
              const result = await tool.handler(context)
              await appendActivity('tool', `${tool.name} completed`, {
                sessionId: context.sessionId,
                arguments: context.arguments,
                result
              })
              return result ?? { ok: true }
            } catch (error) {
              await appendActivity('error', `${tool.name} failed`, {
                sessionId: context.sessionId,
                message: formatError(error)
              })
              throw error
            } finally {
              refreshStatus().catch((err) => {
                console.warn(
                  '[examples/mcp-control] Failed to refresh status after tool call',
                  err
                )
              })
            }
          }
        })
        .then(() => {
          registeredToolsRef.current.push(tool.name)
          setRegisteredTools([...registeredToolsRef.current])
        })
    },
    [appendActivity, refreshStatus]
  )

  const registerTools = useCallback(() => {
    return Promise.allSettled([
      registerTool({
        name: 'set_application_title',
        description: 'Update the application window title.',
        inputSchema: {
          type: 'object',
          required: ['title'],
          properties: {
            title: {
              type: 'string',
              minLength: 1,
              description: 'New title to display in the window frame.'
            }
          }
        },
        metadata: {
          category: 'window',
          example: '{"title": "Socket MCP Control"}'
        },
        handler: async ({ arguments: args }) => {
          if (
            !args ||
            typeof args.title !== 'string' ||
            args.title.trim().length === 0
          ) {
            throw new TypeError('arguments.title must be a non-empty string')
          }
          const win = await getWindow()
          await win.setTitle(args.title)
          return { title: args.title }
        }
      }),
      registerTool({
        name: 'resize_application_window',
        description:
          'Resize the application window to an explicit width and height.',
        inputSchema: {
          type: 'object',
          required: ['width', 'height'],
          properties: {
            width: { type: 'number', minimum: 320 },
            height: { type: 'number', minimum: 240 }
          }
        },
        metadata: {
          category: 'window',
          example: '{"width": 1280, "height": 720}'
        },
        handler: async ({ arguments: args }) => {
          const width = Number(args?.width)
          const height = Number(args?.height)
          if (!Number.isFinite(width) || width <= 0) {
            throw new TypeError('arguments.width must be a positive number')
          }
          if (!Number.isFinite(height) || height <= 0) {
            throw new TypeError('arguments.height must be a positive number')
          }
          const win = await getWindow()
          const { width: appliedWidth, height: appliedHeight } =
            await win.setSize({ width, height })
          return { width: appliedWidth, height: appliedHeight }
        }
      }),
      registerTool({
        name: 'move_application_window',
        description: 'Move the window to an absolute screen position.',
        inputSchema: {
          type: 'object',
          required: ['x', 'y'],
          properties: {
            x: {
              type: 'number',
              description: 'Left coordinate in physical pixels.'
            },
            y: {
              type: 'number',
              description: 'Top coordinate in physical pixels.'
            }
          }
        },
        metadata: {
          category: 'window',
          example: '{"x": 64, "y": 64}'
        },
        handler: async ({ arguments: args }) => {
          const x = Number(args?.x)
          const y = Number(args?.y)
          if (!Number.isFinite(x) || !Number.isFinite(y)) {
            throw new TypeError(
              'arguments.x and arguments.y must be finite numbers'
            )
          }
          const win = await getWindow()
          await win.setPosition({ x, y })
          return { x, y }
        }
      }),
      registerTool({
        name: 'set_application_window_visibility',
        description: 'Show or hide the main window.',
        inputSchema: {
          type: 'object',
          required: ['visible'],
          properties: {
            visible: {
              type: 'boolean',
              description: 'True to show, false to hide.'
            }
          }
        },
        metadata: {
          category: 'window',
          example: '{"visible": true}'
        },
        handler: async ({ arguments: args }) => {
          if (typeof args?.visible !== 'boolean') {
            throw new TypeError('arguments.visible must be a boolean')
          }
          const win = await getWindow()
          if (args.visible) {
            await win.show()
            await win.focus()
            windowVisibleRef.current = true
            setWindowVisible(true)
          } else {
            await win.hide()
            windowVisibleRef.current = false
            setWindowVisible(false)
          }
          return { visible: windowVisibleRef.current }
        }
      }),
      registerTool({
        name: 'log_application_note',
        description: 'Append a custom entry to the activity stream.',
        inputSchema: {
          type: 'object',
          required: ['message'],
          properties: {
            message: {
              type: 'string',
              minLength: 1,
              description: 'Message recorded in the activity feed.'
            }
          }
        },
        metadata: {
          category: 'utility',
          example: '{"message": "Triggering workflow from operator console."}'
        },
        handler: async ({ arguments: args, sessionId }) => {
          if (
            !args ||
            typeof args.message !== 'string' ||
            args.message.trim().length === 0
          ) {
            throw new TypeError('arguments.message must be a non-empty string')
          }
          await appendActivity('note', 'Operator note', {
            sessionId,
            message: args.message
          })
          return { message: args.message }
        }
      })
    ])
  }, [appendActivity, getWindow, registerTool])

  const startServer = useCallback(
    async (reason = null) => {
      if (startingRef.current) return
      startingRef.current = true
      setErrorMessage(null)
      setOauthDetails(null)
      const restarting = isRunning
      updateStatus(
        'info',
        restarting ? 'Restarting MCP server…' : 'Starting MCP server…',
        'Registering resources and tools.'
      )

      try {
        if (restarting) {
          await appendActivity('system', 'Stopping MCP server before restart', {
            reason: reason || 'manual restart'
          }).catch(() => {})
          try {
            await mcp.stopServer()
          } catch (stopError) {
            console.warn(
              '[examples/mcp-control] Failed to stop existing MCP server',
              stopError
            )
          }
          setIsRunning(false)
        }

        await Promise.allSettled([registerResources(), registerTools()])

        await appendActivity('system', 'Registering MCP surface', {
          tools: registeredToolsRef.current,
          resources: [STATUS_RESOURCE_URI, ACTIVITY_RESOURCE_URI]
        })

        const startOptions = {}
        if (configuredHost) startOptions.host = configuredHost
        if (configuredEndpoint) startOptions.endpoint = configuredEndpoint
        if (Number.isFinite(configuredPort)) {
          startOptions.port = configuredPort
        } else {
          startOptions.port = 0
        }
        if (Number.isFinite(configuredRetry)) {
          startOptions.retry = configuredRetry
        }
        if (authTokenRef.current) startOptions.token = authTokenRef.current
        const oauthOptions = {
          enabled: true,
          authorizePath: OAUTH_AUTHORIZE_PATH,
          tokenPath: OAUTH_TOKEN_PATH,
          metadataPath: OAUTH_METADATA_PATH,
          defaultClientId: DEFAULT_OAUTH_CLIENT_ID,
          defaultScope: DEFAULT_OAUTH_SCOPE,
          screen: { html: DEFAULT_OAUTH_SCREEN_HTML }
        }
        startOptions.oauth = oauthOptions

        const info = await mcp.startServer(startOptions)
        if (!info?.running) {
          throw new Error('MCP server reported as not running')
        }

        serverInfoRef.current = info
        setServerInfo(info)
        serverEndpointRef.current = buildEndpointUrl(info)
        setEndpoint(serverEndpointRef.current)
        startTimeRef.current = Date.now()
        updateStatus(
          'success',
          'MCP server ready',
          'Server is accepting MCP connections.'
        )
        setIsRunning(true)

        let oauthSummary = null
        const hostForUrl =
          info.host && info.host.includes(':') ? `[${info.host}]` : info.host
        const baseUrl = `http://${hostForUrl}:${info.port}`
        const coercePath = (value, fallback) => {
          const candidate =
            typeof value === 'string' && value.length > 0 ? value : fallback
          if (typeof candidate !== 'string' || candidate.length === 0) {
            return '/'
          }
          return candidate.startsWith('/') ? candidate : `/${candidate}`
        }
        const resolvedAuthorizePath = coercePath(
          info?.oauth?.authorizePath,
          oauthOptions.authorizePath || '/oauth/authorize'
        )
        const resolvedTokenPath = coercePath(
          info?.oauth?.tokenPath,
          oauthOptions.tokenPath || '/oauth/token'
        )
        const resolvedMetadataPath = coercePath(
          info?.oauth?.metadataPath,
          oauthOptions.metadataPath || '/.well-known/oauth-authorization-server'
        )
        const authorizeUrl = `${baseUrl}${resolvedAuthorizePath}`
        const tokenUrl = `${baseUrl}${resolvedTokenPath}`
        const metadataUrl = `${baseUrl}${resolvedMetadataPath}`
        const params = new URLSearchParams({
          response_type: 'code',
          client_id: oauthOptions.defaultClientId || DEFAULT_OAUTH_CLIENT_ID,
          redirect_uri: OAUTH_DEMO_REDIRECT_URI,
          scope: oauthOptions.defaultScope || DEFAULT_OAUTH_SCOPE,
          code_challenge: OAUTH_DEMO_CODE_CHALLENGE,
          code_challenge_method: 'plain',
          state: 'demo-state'
        })
        oauthSummary = {
          authorizeUrl,
          tokenUrl,
          metadataUrl,
          clientId: oauthOptions.defaultClientId || DEFAULT_OAUTH_CLIENT_ID,
          scope: oauthOptions.defaultScope || DEFAULT_OAUTH_SCOPE,
          customScreen: Boolean(
            oauthOptions.screen && oauthOptions.screen.html
          ),
          demoAuthorizeUrl: `${authorizeUrl}?${params.toString()}`,
          codeVerifier: OAUTH_DEMO_CODE_CHALLENGE,
          redirectUri: OAUTH_DEMO_REDIRECT_URI
        }
        setOauthDetails(oauthSummary)
        await appendActivity('system', 'OAuth authorization flow ready', {
          authorize: oauthSummary.authorizeUrl,
          token: oauthSummary.tokenUrl,
          metadata: oauthSummary.metadataUrl,
          clientId: oauthSummary.clientId,
          scope: oauthSummary.scope,
          customScreen: oauthSummary.customScreen
        })

        await appendActivity('system', 'MCP server ready', {
          host: info.host,
          port: info.port,
          endpoint: serverEndpointRef.current,
          authorization: authTokenRef.current
            ? `Authorization: Bearer ${authTokenRef.current}`
            : undefined,
          sessionHeader: 'Mcp-Session-Id: <value from ready event>',
          oauth: {
            authorize: authorizeUrl,
            token: tokenUrl,
            metadata: metadataUrl
          },
          restartReason: reason || (restarting ? 'restart' : 'initial start')
        })

        await refreshStatus()
        if (statusTimerRef.current) clearInterval(statusTimerRef.current)
        statusTimerRef.current = setInterval(() => {
          refreshStatus().catch((error) => {
            console.warn(
              '[examples/mcp-control] Periodic status refresh failed',
              error
            )
          })
        }, STATUS_REFRESH_INTERVAL_MS)
      } catch (error) {
        const message = formatError(error)
        console.error('[examples/mcp-control] Failed to start server', error)
        setErrorMessage(message)
        updateStatus('danger', 'Failed to start MCP server', message)
        setIsRunning(false)
      } finally {
        startingRef.current = false
        setIsReady(true)
      }
    },
    [
      appendActivity,
      isRunning,
      registerResources,
      registerTools,
      refreshStatus,
      updateStatus
    ]
  )

  const clearActivity = useCallback(() => {
    activityRef.current = []
    setActivity([])
  }, [])

  useEffect(() => {
    const offReady = hooks.onReady(async () => {
      try {
        await getWindow()
        await startServer()
      } catch (error) {
        const message = formatError(error)
        setErrorMessage(message)
        updateStatus('danger', 'Failed to start MCP server', message)
      }
    })

    const offPause = hooks.onApplicationPause(() => {
      appendActivity(
        'system',
        'Application paused by the host environment'
      ).catch(() => {})
    })

    const offResume = hooks.onApplicationResume(() => {
      appendActivity(
        'system',
        'Application resumed by the host environment'
      ).catch(() => {})
      refreshStatus().catch(() => {})
    })

    const offUrl = hooks.onApplicationURL((event) => {
      appendActivity('url', 'Received deep link', {
        url: event?.url
      }).catch(() => {})
    })

    return () => {
      offReady?.()
      offPause?.()
      offResume?.()
      offUrl?.()
      if (statusTimerRef.current) {
        clearInterval(statusTimerRef.current)
        statusTimerRef.current = null
      }
    }
  }, [appendActivity, getWindow, refreshStatus, startServer, updateStatus])

  const restartServer = useCallback(
    () => startServer('manual restart'),
    [startServer]
  )

  return {
    statusMessage,
    serverInfo,
    endpoint,
    token: authTokenRef.current,
    registeredTools,
    activity,
    latestSnapshot,
    errorMessage,
    isRunning,
    isReady,
    windowVisible,
    refreshStatus,
    recordActivity: appendActivity,
    restart: restartServer,
    clearActivity,
    oauthDetails
  }
}

function formatStatusSnapshot (snapshot) {
  if (!snapshot) return 'No status available yet.'
  try {
    return JSON.stringify(snapshot, null, 2)
  } catch {
    return String(snapshot)
  }
}

function renderActivityEntry (entry) {
  const time = entry.timestamp ? entry.timestamp.slice(11, 19) : '--:--:--'
  return h(
    'div',
    { className: 'ui-prose' },
    h('strong', null, `[${time}] ${entry.kind ?? 'event'}`),
    h('div', null, entry.message || ''),
    entry.details &&
      h(
        'pre',
        null,
        (() => {
          try {
            return JSON.stringify(entry.details, null, 2)
          } catch {
            return String(entry.details)
          }
        })()
      )
  )
}

function ServerSummary ({
  serverInfo,
  endpoint,
  token,
  oauthDetails,
  isRunning,
  windowVisible
}) {
  if (!serverInfo) {
    return h(EmptyState, {
      title: 'Server not started yet',
      description:
        'Waiting for the runtime ready event to start the MCP server.'
    })
  }

  const rows = [
    h(
      TableRow,
      { key: 'host' },
      h(TableCell, null, 'Host'),
      h(TableCell, null, h(InlineCode, null, serverInfo.host))
    ),
    h(
      TableRow,
      { key: 'port' },
      h(TableCell, null, 'Port'),
      h(TableCell, null, h(InlineCode, null, String(serverInfo.port)))
    ),
    h(
      TableRow,
      { key: 'endpoint' },
      h(TableCell, null, 'Endpoint'),
      h(TableCell, null, h(InlineCode, null, endpoint))
    ),
    h(
      TableRow,
      { key: 'token' },
      h(TableCell, null, 'Token'),
      h(TableCell, null, token ? h(InlineCode, null, token) : '—')
    ),
    h(
      TableRow,
      { key: 'server-status' },
      h(TableCell, null, 'Server status'),
      h(
        TableCell,
        null,
        h(
          StatusPill,
          { status: isRunning ? 'running' : 'idle' },
          isRunning ? 'running' : 'stopped'
        )
      )
    ),
    h(
      TableRow,
      { key: 'window-visible' },
      h(TableCell, null, 'Window visible'),
      h(
        TableCell,
        null,
        h(
          StatusPill,
          { status: windowVisible ? 'success' : 'idle' },
          windowVisible ? 'yes' : 'no'
        )
      )
    ),
    h(
      TableRow,
      { key: 'oauth-flow' },
      h(TableCell, null, 'OAuth flow'),
      h(
        TableCell,
        null,
        h(
          StatusPill,
          { status: oauthDetails ? 'success' : 'idle' },
          oauthDetails ? 'enabled' : 'disabled'
        )
      )
    )
  ]

  if (oauthDetails) {
    rows.push(
      h(
        TableRow,
        { key: 'oauth-authorize' },
        h(TableCell, null, 'Authorize endpoint'),
        h(TableCell, null, h(InlineCode, null, oauthDetails.authorizeUrl))
      ),
      h(
        TableRow,
        { key: 'oauth-token' },
        h(TableCell, null, 'Token endpoint'),
        h(TableCell, null, h(InlineCode, null, oauthDetails.tokenUrl))
      ),
      h(
        TableRow,
        { key: 'oauth-metadata' },
        h(TableCell, null, 'Metadata endpoint'),
        h(TableCell, null, h(InlineCode, null, oauthDetails.metadataUrl))
      ),
      h(
        TableRow,
        { key: 'oauth-client' },
        h(TableCell, null, 'Default client id'),
        h(TableCell, null, h(InlineCode, null, oauthDetails.clientId))
      ),
      h(
        TableRow,
        { key: 'oauth-scope' },
        h(TableCell, null, 'Requested scope'),
        h(
          TableCell,
          null,
          oauthDetails.scope ? h(InlineCode, null, oauthDetails.scope) : '—'
        )
      ),
      h(
        TableRow,
        { key: 'oauth-screen' },
        h(TableCell, null, 'Custom consent screen'),
        h(
          TableCell,
          null,
          h(
            StatusPill,
            { status: oauthDetails.customScreen ? 'info' : 'idle' },
            oauthDetails.customScreen ? 'enabled' : 'default'
          )
        )
      )
    )
  }

  return h(
    Table,
    null,
    h(
      TableHead,
      null,
      h(
        TableRow,
        null,
        h(TableHeader, null, 'Field'),
        h(TableHeader, null, 'Value')
      )
    ),
    h(TableBody, null, ...rows)
  )
}

function ToolsList ({ tools }) {
  if (!tools.length) {
    return h(EmptyState, {
      title: 'Tools not registered yet',
      description:
        'The server registers tools during startup. They will appear here once ready.'
    })
  }

  return h(
    'div',
    { className: 'ui-prose' },
    h(
      'ul',
      null,
      tools.map((tool) =>
        h('li', { key: tool }, h(Badge, { variant: 'neutral' }, tool))
      )
    )
  )
}

function McpControlApp () {
  const {
    statusMessage,
    serverInfo,
    endpoint,
    token,
    registeredTools,
    activity,
    latestSnapshot,
    errorMessage,
    isRunning,
    isReady,
    windowVisible,
    refreshStatus,
    recordActivity,
    restart,
    clearActivity,
    oauthDetails
  } = useMcpControl()

  const handlePreviewAuthorize = useCallback(() => {
    if (!oauthDetails?.demoAuthorizeUrl) return
    try {
      window.open(
        oauthDetails.demoAuthorizeUrl,
        '_blank',
        'noopener,noreferrer'
      )
    } catch (error) {
      console.warn(
        '[examples/mcp-control] Failed to open OAuth authorize preview',
        error
      )
    }
  }, [oauthDetails])

  const oauthCurlSnippet = useMemo(() => {
    if (!oauthDetails) return null
    const lines = [
      '# Visit the authorization URL in a browser to approve the demo request:',
      oauthDetails.demoAuthorizeUrl,
      '',
      '# Exchange the authorization code for an access token:',
      'CODE="<authorization_code_from_redirect>"',
      `curl -sS -X POST "${oauthDetails.tokenUrl}" \\`,
      '  -H "Content-Type: application/x-www-form-urlencoded" \\',
      '  -d "grant_type=authorization_code" \\',
      `  -d "client_id=${oauthDetails.clientId}" \\`,
      `  -d "redirect_uri=${oauthDetails.redirectUri}" \\`,
      `  -d "code_verifier=${oauthDetails.codeVerifier}" \\`,
      '  -d "code=$CODE"'
    ]
    return lines.join('\n')
  }, [oauthDetails])

  const statusBanner = useMemo(
    () =>
      h(StatusBanner, {
        variant: statusMessage.variant,
        title: statusMessage.title,
        message: statusMessage.message
      }),
    [statusMessage]
  )

  return h(
    ExampleLayout,
    {
      title: 'MCP Control Center',
      description:
        'Start a local MCP server, inspect its status, and review activity published to remote clients.'
    },
    h(
      ExamplePanel,
      {
        title: 'Server overview',
        description:
          'The control center surfaces server connectivity, tool registration, and runtime events.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        errorMessage &&
          h(StatusBanner, {
            variant: 'danger',
            title: 'Server error',
            message: errorMessage,
            assertive: true
          }),
        h(
          ExampleSection,
          {
            title: 'Controls',
            description: 'Refresh status or emit demo activity entries.',
            actions: h(
              Button,
              {
                type: 'button',
                size: 'sm',
                variant: 'secondary',
                onClick: () => refreshStatus().catch(() => {}),
                disabled: !isRunning
              },
              'Refresh snapshot'
            )
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () => restart().catch(() => {}),
                disabled: !isReady
              },
              'Restart server'
            ),
            h(
              Button,
              {
                type: 'button',
                variant: 'secondary',
                onClick: () => {
                  recordActivity('demo', 'Manual activity entry', {
                    note: 'Recorded from the MCP control UI',
                    at: new Date().toISOString()
                  }).catch(() => {})
                },
                disabled: !isRunning
              },
              'Record sample activity'
            ),
            oauthDetails &&
              h(
                Button,
                {
                  type: 'button',
                  variant: 'secondary',
                  onClick: handlePreviewAuthorize,
                  disabled: !isRunning
                },
                'Open OAuth consent screen'
              )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Server configuration'
          },
          h(ServerSummary, {
            serverInfo,
            endpoint,
            token,
            oauthDetails,
            isRunning,
            windowVisible
          })
        ),
        h(
          ExampleSection,
          {
            title: 'Registered tools',
            description: 'Tools exposed via the MCP server.'
          },
          h(ToolsList, { tools: registeredTools })
        ),
        h(
          ExampleSection,
          {
            title: 'Status snapshot',
            description: 'Latest payload served over the status resource.'
          },
          h(Code, null, formatStatusSnapshot(latestSnapshot))
        ),
        h(
          ExampleSection,
          {
            title: 'Activity stream',
            description:
              'Activity entries published over the MCP activity resource.',
            actions: h(
              ExampleStack,
              { gap: 'xs' },
              h(
                Button,
                {
                  type: 'button',
                  variant: 'ghost',
                  size: 'sm',
                  onClick: () => {
                    clearActivity()
                    if (isRunning) {
                      recordActivity('demo', 'Activity log cleared from UI', {
                        requestedAt: new Date().toISOString()
                      })
                        .finally(() => {
                          clearActivity()
                        })
                        .catch(() => {})
                    }
                  }
                },
                'Clear log'
              ),
              h(
                Button,
                {
                  type: 'button',
                  variant: 'ghost',
                  size: 'sm',
                  onClick: async () => {
                    try {
                      const text = activity
                        .map((entry) => {
                          try {
                            return JSON.stringify(entry)
                          } catch {
                            return String(entry)
                          }
                        })
                        .join('\n')
                      await clipboard.writeText(text)
                      recordActivity(
                        'demo',
                        'Copied activity log to clipboard',
                        {
                          entries: activity.length
                        }
                      ).catch(() => {})
                    } catch (error) {
                      console.warn(
                        '[examples/mcp-control] Failed to copy activity log',
                        error
                      )
                      recordActivity('error', 'Failed to copy activity log', {
                        message: formatError(error)
                      }).catch(() => {})
                    }
                  },
                  disabled: activity.length === 0
                },
                'Copy activity log'
              )
            )
          },
          h(LogViewer, {
            entries: activity,
            renderEntry: renderActivityEntry,
            emptyState: 'No activity recorded yet.'
          })
        ),
        h(
          ExampleSection,
          {
            title: 'Integration notes'
          },
          h(
            ExampleProse,
            null,
            h(
              'p',
              null,
              'The control center starts a local MCP server with demo resources and tools. Remote clients can connect using ',
              h(
                InlineCode,
                null,
                `http://${configuredHost || '<host>'}:${configuredPort || '<port>'}`
              ),
              ' and authenticate with the token above.'
            ),
            h(
              'p',
              null,
              'Use these tools as reference implementations when wiring MCP servers into your automation or Copilot flows.'
            ),
            oauthDetails &&
              h(
                React.Fragment,
                null,
                h(
                  'p',
                  null,
                  'OAuth is enabled. Launch the consent screen with the buttons above or by visiting ',
                  h(InlineCode, null, oauthDetails.demoAuthorizeUrl),
                  '. The demo flow uses a plain PKCE challenge named ',
                  h(InlineCode, null, oauthDetails.codeVerifier),
                  ' and redirects to ',
                  h(InlineCode, null, oauthDetails.redirectUri),
                  ' (you can copy the authorization code from the URL even if no server is running there).'
                ),
                h(
                  'p',
                  null,
                  'After approving, exchange the authorization code using the snippet below. The resulting access token can be supplied as ',
                  h(InlineCode, null, 'Authorization: Bearer <token>'),
                  ' for both SSE and JSON-RPC calls.'
                ),
                oauthCurlSnippet && h(Code, null, oauthCurlSnippet)
              )
          )
        )
      )
    )
  )
}

mountExample(McpControlApp)
