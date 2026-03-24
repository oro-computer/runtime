import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleSection,
  ExampleStack,
  ExampleProse,
  StatusBanner,
  Button,
  Input,
  InlineCode,
  LogViewer,
  FormField,
  Switch
} from '../ui/index.js'

import { runIrohDemo, DEFAULT_ALPN } from './workflow.js'

const { createElement: h, useState, useCallback, useMemo } = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function formatDuration (details) {
  if (!details?.startedAt || !details?.finishedAt) return null
  const ms = Math.max(0, details.finishedAt - details.startedAt)
  if (ms < 1000) return `${ms} ms`
  return `${(ms / 1000).toFixed(2)} s`
}

function useStatusBanner (status) {
  return useMemo(
    () =>
      h(StatusBanner, {
        variant: status.variant,
        title: status.title,
        message: status.message
      }),
    [status]
  )
}

function IrohExampleApp () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Ready',
    message: 'Configure the parameters and launch the connectivity flow.'
  })
  const [logs, setLogs] = useState([])
  const [alpn, setAlpn] = useState(DEFAULT_ALPN)
  const [datagramOutbound, setDatagramOutbound] = useState('ping from client')
  const [datagramInbound, setDatagramInbound] = useState('pong from server')
  const [includeWatcher, setIncludeWatcher] = useState(true)
  const [details, setDetails] = useState(null)
  const [connectionType, setConnectionType] = useState(null)
  const [running, setRunning] = useState(false)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const statusBanner = useStatusBanner(status)

  const handleWatcherEvent = useCallback(
    (event) => {
      if (event?.type === 'connectionType' && event.name) {
        setConnectionType(event.name)
      }
      if (event?.type === 'error' && event.message) {
        appendLog(`watcher error → ${event.message}`)
      }
    },
    [appendLog]
  )

  const runDemo = useCallback(async () => {
    if (running) return
    setRunning(true)
    setLogs([])
    setDetails(null)
    setConnectionType(null)
    setStatus({
      variant: 'warning',
      title: 'Running flow…',
      message: 'Initialising iroh, creating endpoints, and exchanging traffic.'
    })
    appendLog('Starting iroh connectivity flow')

    try {
      const result = await runIrohDemo({
        alpn,
        datagramOutbound,
        datagramInbound,
        includeWatcher,
        log: (line) => appendLog(line),
        onWatcherEvent: handleWatcherEvent
      })
      setDetails(result)
      if (result?.connectionEvents?.length) {
        const last = result.connectionEvents[result.connectionEvents.length - 1]
        if (last?.type === 'connectionType' && last.name) {
          setConnectionType(last.name)
        }
      }
      const duration = formatDuration(result)
      setStatus({
        variant: 'success',
        title: 'Flow completed',
        message: duration
          ? `Datagrams and streams exchanged successfully in ${duration}.`
          : 'Datagrams and streams exchanged successfully.'
      })
      appendLog('Connectivity flow completed successfully')
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Flow failed',
        message
      })
      appendLog(`error → ${message}`)
    } finally {
      setRunning(false)
    }
  }, [
    running,
    alpn,
    datagramOutbound,
    datagramInbound,
    includeWatcher,
    appendLog,
    handleWatcherEvent
  ])

  const summaryList = useMemo(() => {
    if (!details) {
      return ['Run the flow to populate the connection summary.']
    }

    const duration = formatDuration(details)
    const stats = details.stats || {}
    const items = [
      `Server node address: ${details.serverAddr || '—'}`,
      `Client node address: ${details.clientAddr || '—'}`
    ]

    if (connectionType) {
      items.push(`Last reported connection type: ${connectionType}`)
    } else if (includeWatcher) {
      items.push('Connection type watcher did not report any updates.')
    }

    items.push(
      `Datagram round trip: ${details.datagramRoundtrip ? 'complete' : 'incomplete'}`
    )
    items.push(
      `Unidirectional payload: ${details.unidirectionalPayload || '—'}`
    )
    if (details.bidirectionalPayload) {
      items.push(
        `Bidirectional payload (server→client): ${details.bidirectionalPayload.serverToClient || '—'}`
      )
      items.push(
        `Bidirectional payload (client→server): ${details.bidirectionalPayload.clientToServer || '—'}`
      )
    }
    if (typeof stats.maxDatagramSize === 'number') {
      items.push(`Max datagram size: ${stats.maxDatagramSize}`)
    }
    if (typeof stats.rtt === 'number') {
      items.push(`Round trip time (rtt): ${stats.rtt}`)
    }
    if (typeof stats.packetLoss === 'number') {
      items.push(`Packet loss: ${stats.packetLoss}`)
    }
    if (duration) {
      items.push(`Flow duration: ${duration}`)
    }
    if (typeof details.shutdown === 'boolean') {
      items.push(
        `Iroh shutdown result: ${details.shutdown ? 'service stopped' : 'service already stopped'}`
      )
    }
    return items
  }, [details, connectionType, includeWatcher])

  return h(
    ExampleLayout,
    {
      title: 'Iroh Connectivity Demo',
      description:
        'Establish paired endpoints, exchange datagrams and streams, and inspect oro:iroh telemetry.'
    },
    h(
      ExamplePanel,
      {
        title: 'Connectivity workflow',
        description:
          'Drive the native iroh service from the Oro Runtime and observe the full round trip.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Configuration',
            description:
              'Adjust the transport identifier and datagram payloads sent during the demo.'
          },
          h(
            ExampleStack,
            { gap: 'md' },
            h(
              FormField,
              {
                label: 'ALPN',
                description:
                  'The protocol identifier shared by both endpoints.',
                htmlFor: 'iroh-alpn'
              },
              h(Input, {
                id: 'iroh-alpn',
                value: alpn,
                onChange: (event) => setAlpn(event.target.value),
                placeholder: DEFAULT_ALPN,
                disabled: running
              })
            ),
            h(
              FormField,
              {
                label: 'Client → Server datagram',
                htmlFor: 'iroh-datagram-out'
              },
              h(Input, {
                id: 'iroh-datagram-out',
                value: datagramOutbound,
                onChange: (event) => setDatagramOutbound(event.target.value),
                placeholder: 'ping from client',
                disabled: running
              })
            ),
            h(
              FormField,
              {
                label: 'Server → Client datagram',
                htmlFor: 'iroh-datagram-in'
              },
              h(Input, {
                id: 'iroh-datagram-in',
                value: datagramInbound,
                onChange: (event) => setDatagramInbound(event.target.value),
                placeholder: 'pong from server',
                disabled: running
              })
            ),
            h(
              FormField,
              {
                label: 'Connection type watcher',
                description:
                  'Report socket-provided connection type updates during the run.'
              },
              h(Switch, {
                checked: includeWatcher,
                onCheckedChange: (value) => setIncludeWatcher(Boolean(value)),
                disabled: running,
                label: 'Toggle connection watcher'
              })
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Actions',
            description:
              'Launch the round trip to see datagrams, streams, and connection telemetry.'
          },
          h(
            Button,
            {
              type: 'button',
              onClick: () => runDemo().catch(() => {}),
              disabled: running
            },
            running ? 'Running…' : 'Run connectivity flow'
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Connection summary'
          },
          h(
            ExampleProse,
            null,
            h(
              'ul',
              null,
              summaryList.map((item, index) => h('li', { key: index }, item))
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'How this works'
          },
          h(
            ExampleProse,
            null,
            h(
              'ul',
              null,
              h(
                'li',
                null,
                'Initialises the native service via ',
                h(InlineCode, null, 'iroh.ensureInitialized()'),
                ' and sets a log level.'
              ),
              h(
                'li',
                null,
                'Creates paired endpoints, binds them, and exchanges node addresses.'
              ),
              h(
                'li',
                null,
                'Establishes a connection using the shared ',
                h(InlineCode, null, 'ALPN'),
                ' identifier.'
              ),
              h(
                'li',
                null,
                'Sends datagram pings and pongs, then streams payloads over uni- and bidirectional channels.'
              ),
              h(
                'li',
                null,
                'Collects connection stats and shuts the service down so the flow can be re-run.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Transport operations emitted by the workflow.',
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
            emptyState: 'Run the connectivity flow to see iroh output.'
          })
        )
      )
    )
  )
}

mountExample(IrohExampleApp)
