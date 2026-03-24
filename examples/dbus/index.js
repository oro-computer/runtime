import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleStack,
  ExampleGrid,
  ExamplePanel,
  ExampleSection,
  ScrollArea,
  Button,
  StatusPill,
  InlineCode,
  Alert,
  AlertTitle,
  AlertDescription
} from '../ui/index.js'

import dbus, {
  BUS,
  NAME_FLAGS,
  WELL_KNOWN_INTERFACES,
  WELL_KNOWN_MEMBERS,
  WELL_KNOWN_NAMES,
  WELL_KNOWN_PATHS,
  WELL_KNOWN_ERRORS
} from 'oro:dbus'

const {
  createElement: h,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState
} = React

const BUS_NAME = 'com.oro.examples.DBusDemo'
const OBJECT_PATH = '/com/oro/examples/DBusDemo'
const INTERFACE = 'com.oro.examples.DBusDemo'

function alertVariant (tone) {
  switch (tone) {
    case 'success':
      return 'success'
    case 'warning':
      return 'warning'
    case 'error':
      return 'danger'
    default:
      return 'default'
  }
}

function DBusDemoApp () {
  const [status, setStatus] = useState({
    message: 'Check DBus availability to get started.',
    tone: 'info'
  })
  const [logs, setLogs] = useState([])
  const [connectionStatus, setConnectionStatus] = useState('disconnected')
  const [connectionId, setConnectionId] = useState(null)
  const [availability, setAvailability] = useState(null)
  const [busNameClaimed, setBusNameClaimed] = useState(false)
  const [matchCount, setMatchCount] = useState(0)
  const [isExported, setIsExported] = useState(false)

  const connectionRef = useRef(null)
  const matchIdsRef = useRef(new Map())
  const exportIdRef = useRef(null)
  const busNameClaimedRef = useRef(false)
  const logEndRef = useRef(null)

  const setStatusMessage = useCallback((message, tone = 'info') => {
    setStatus({ message, tone })
  }, [])

  const appendLog = useCallback((message, data) => {
    const ts = new Date().toISOString().slice(11, 23)
    let line = `[${ts}] ${message}`
    if (data !== undefined) {
      try {
        const payload =
          typeof data === 'string' ? data : JSON.stringify(data, null, 2)
        line += `\n${payload}`
      } catch (err) {
        line += `\n<failed to serialise: ${err?.message || err}>`
      }
    }
    setLogs((prev) => {
      const next = [...prev, line]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  useEffect(() => {
    if (logEndRef.current) {
      logEndRef.current.scrollIntoView({ behavior: 'smooth', block: 'end' })
    }
  }, [logs])

  const handleMethodCall = useCallback(
    async (call) => {
      const conn = connectionRef.current
      if (!conn) return

      if (call.path !== OBJECT_PATH || call.interface !== INTERFACE) {
        await conn.respondError(
          call.callId,
          WELL_KNOWN_ERRORS.UNKNOWN_OBJECT,
          'Unknown object path'
        )
        appendLog(`Ignored method call for ${call.path}.${call.member}`)
        return
      }

      const { member, sender, values } = call

      if (member === 'Echo') {
        const [text = ''] = Array.isArray(values) ? values : []
        await conn.respond(call.callId, {
          signature: 's',
          body: [`Echo from JS: ${text}`]
        })
        appendLog(`Handled Echo call from ${sender || 'local client'}`, text)
        return
      }

      if (member === 'Sum') {
        const numbers =
          Array.isArray(values) && Array.isArray(values[0]) ? values[0] : []
        const total = numbers.reduce(
          (acc, value) => acc + Number(value || 0),
          0
        )
        await conn.respond(call.callId, { signature: 'i', body: [total] })
        appendLog(`Handled Sum call from ${sender || 'local client'}`, {
          numbers,
          total
        })
        return
      }

      await conn.respondError(
        call.callId,
        WELL_KNOWN_ERRORS.UNKNOWN_METHOD,
        `Method ${member} not implemented`
      )
      appendLog(`Rejected unsupported method ${member}`)
    },
    [appendLog]
  )

  const requireConnection = useCallback(() => {
    const conn = connectionRef.current
    if (!conn || conn.closed) {
      appendLog('Connect to DBus first')
      setStatusMessage(
        'Connect to the session bus before using this action.',
        'warning'
      )
      return null
    }
    return conn
  }, [appendLog, setStatusMessage])

  const removeMatches = useCallback(
    async (skipLog = false) => {
      const conn = connectionRef.current
      const matchIds = matchIdsRef.current
      if (!conn || matchIds.size === 0) {
        if (!skipLog) appendLog('No match rules to remove')
        return
      }

      for (const [label, id] of matchIds.entries()) {
        try {
          await conn.removeMatch(id)
          if (!skipLog) appendLog(`Removed match ${label} (${id})`)
        } catch (err) {
          appendLog(`Failed to remove match ${label}: ${err?.message || err}`)
        }
      }
      matchIds.clear()
      setMatchCount(0)
    },
    [appendLog]
  )

  const disconnect = useCallback(
    async ({ silent = false } = {}) => {
      const conn = connectionRef.current
      if (!conn) {
        if (!silent) appendLog('No active connection')
        return
      }

      try {
        await removeMatches(true)

        if (exportIdRef.current) {
          try {
            await conn.unexportObject(exportIdRef.current)
            if (!silent) appendLog(`Unexported object ${exportIdRef.current}`)
          } catch (err) {
            appendLog(`Error while unexporting: ${err?.message || err}`)
          } finally {
            exportIdRef.current = null
            setIsExported(false)
          }
        }

        if (busNameClaimedRef.current) {
          try {
            await conn.releaseName(BUS_NAME)
            if (!silent) appendLog(`Released well-known name ${BUS_NAME}`)
          } catch (err) {
            appendLog(`Error while releasing name: ${err?.message || err}`)
          }
          busNameClaimedRef.current = false
          setBusNameClaimed(false)
        }

        await conn.close()
        if (!silent) appendLog('Disconnected')
        setStatusMessage('Disconnected from session bus.', 'warning')
      } catch (err) {
        appendLog(`Error while disconnecting: ${err?.message || err}`)
      } finally {
        connectionRef.current = null
        setConnectionStatus('disconnected')
        setConnectionId(null)
        matchIdsRef.current.clear()
        busNameClaimedRef.current = false
        exportIdRef.current = null
        setMatchCount(0)
        setIsExported(false)
      }
    },
    [appendLog, removeMatches, setStatusMessage]
  )

  useEffect(() => {
    if (typeof window === 'undefined') return undefined
    const handler = () => {
      disconnect({ silent: true }).catch(() => {})
    }
    window.addEventListener('beforeunload', handler)
    return () => window.removeEventListener('beforeunload', handler)
  }, [disconnect])

  const checkAvailability = useCallback(async () => {
    setStatusMessage('Checking DBus availability…')
    try {
      const info = await dbus.availability()
      setAvailability(info)
      if (info.available) {
        appendLog('DBus is available')
        setStatusMessage('Session bus is available on this system.', 'success')
      } else {
        appendLog('DBus unavailable', info)
        setStatusMessage(
          'Session bus is unavailable. Review the log for details.',
          'warning'
        )
      }
    } catch (err) {
      appendLog(`Failed to read availability: ${err?.message || err}`)
      setStatusMessage(
        `Availability check failed: ${err?.message || err}`,
        'error'
      )
    }
  }, [appendLog, setStatusMessage])

  const connect = useCallback(async () => {
    if (connectionRef.current && !connectionRef.current.closed) {
      appendLog(`Already connected (id=${connectionRef.current.id})`)
      return
    }

    try {
      const conn = await dbus.connect({ bus: BUS.SESSION })
      connectionRef.current = conn
      setConnectionStatus('connected')
      setConnectionId(conn.id)
      appendLog(`Connected to session bus (id=${conn.id})`)
      setStatusMessage('Connected to session bus.', 'success')

      conn.on('signal', (signal) => {
        const {
          interface: iface,
          member,
          path,
          sender,
          signature,
          values
        } = signal
        appendLog(
          `Signal ${iface}.${member} path=${path} sender=${sender} signature=${signature}`,
          values
        )
      })

      conn.on('methodCall', (call) => {
        handleMethodCall(call).catch((err) => {
          appendLog(`Error while handling method call: ${err?.message || err}`)
          if (call?.callId) {
            conn
              .respondError(
                call.callId,
                WELL_KNOWN_ERRORS.FAILED,
                err?.message || 'Unhandled error'
              )
              .catch((respondErr) => {
                appendLog(
                  `Failed to send DBus error response: ${respondErr?.message || respondErr}`
                )
              })
          }
        })
      })
    } catch (err) {
      appendLog(`Failed to connect: ${err?.message || err}`)
      connectionRef.current = null
      setConnectionStatus('disconnected')
      setConnectionId(null)
      setStatusMessage(
        `Failed to connect to session bus: ${err?.message || err}`,
        'error'
      )
    }
  }, [appendLog, handleMethodCall, setStatusMessage])

  const requestName = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return false
    if (busNameClaimedRef.current) {
      appendLog(`Name already requested (${BUS_NAME})`)
      return true
    }

    try {
      await conn.requestName(BUS_NAME, NAME_FLAGS.NONE)
      busNameClaimedRef.current = true
      setBusNameClaimed(true)
      appendLog(`Requested well-known name ${BUS_NAME}`)
      return true
    } catch (err) {
      appendLog(`Failed to request name: ${err?.message || err}`)
      return false
    }
  }, [appendLog, requireConnection])

  const releaseName = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return false
    if (!busNameClaimedRef.current) {
      appendLog(`Name not currently owned (${BUS_NAME})`)
      return true
    }

    try {
      await conn.releaseName(BUS_NAME)
      busNameClaimedRef.current = false
      setBusNameClaimed(false)
      appendLog(`Released well-known name ${BUS_NAME}`)
      return true
    } catch (err) {
      appendLog(`Failed to release name: ${err?.message || err}`)
      return false
    }
  }, [appendLog, requireConnection])

  const installMatches = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return
    if (matchIdsRef.current.size > 0) {
      appendLog('Match rules already installed')
      return
    }

    try {
      const selfRule = `type='signal',interface='${INTERFACE}'`
      const selfMatch = await conn.addMatch(selfRule, (signal) => {
        appendLog('[match:self] demo signal received', signal.values)
      })
      matchIdsRef.current.set('demo', selfMatch)

      const nameChangesRule = [
        "type='signal'",
        `sender='${WELL_KNOWN_NAMES.DBUS}'`,
        `interface='${WELL_KNOWN_INTERFACES.DBUS}'`,
        `member='${WELL_KNOWN_MEMBERS.NAME_OWNER_CHANGED}'`
      ].join(',')
      const nameMatch = await conn.addMatch(nameChangesRule, (signal) => {
        appendLog('[match:name-owner-changed]', signal.values)
      })
      matchIdsRef.current.set('nameOwnerChanged', nameMatch)

      setMatchCount(matchIdsRef.current.size)
      appendLog('Installed demo match rules')
    } catch (err) {
      appendLog(`Failed to add match rule: ${err?.message || err}`)
    }
  }, [appendLog, requireConnection])

  const exportObject = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return
    if (exportIdRef.current) {
      appendLog(`Object already exported (${exportIdRef.current})`)
      return
    }

    try {
      const id = await conn.exportObject({
        path: OBJECT_PATH,
        interface: INTERFACE,
        methods: ['Echo', 'Sum']
      })
      exportIdRef.current = id
      setIsExported(true)
      appendLog(`Exported object at ${OBJECT_PATH} (id=${id})`)
      if (!busNameClaimedRef.current) await requestName()
    } catch (err) {
      appendLog(`Failed to export object: ${err?.message || err}`)
    }
  }, [appendLog, requestName, requireConnection])

  const unexportObject = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return
    if (!exportIdRef.current) {
      appendLog('Object not exported')
      return
    }

    try {
      await conn.unexportObject(exportIdRef.current)
      appendLog(`Unexported object ${exportIdRef.current}`)
    } catch (err) {
      appendLog(`Failed to unexport object: ${err?.message || err}`)
    } finally {
      exportIdRef.current = null
      setIsExported(false)
    }
  }, [appendLog, requireConnection])

  const callEcho = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return
    if (!exportIdRef.current) {
      appendLog('Export the object first')
      return
    }

    if (!busNameClaimedRef.current) {
      const claimed = await requestName()
      if (!claimed) {
        appendLog('Request the demo name before invoking methods')
        return
      }
    }

    try {
      const message = `Hello at ${new Date().toISOString()}`
      const reply = await conn.call({
        destination: BUS_NAME,
        path: OBJECT_PATH,
        interface: INTERFACE,
        member: 'Echo',
        signature: 's',
        body: [message]
      })
      appendLog('Echo reply', reply)
    } catch (err) {
      appendLog(`Echo call failed: ${err?.message || err}`)
    }
  }, [appendLog, requireConnection, requestName])

  const callSum = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return
    if (!exportIdRef.current) {
      appendLog('Export the object first')
      return
    }

    if (!busNameClaimedRef.current) {
      const claimed = await requestName()
      if (!claimed) {
        appendLog('Request the demo name before invoking methods')
        return
      }
    }

    const values = Array.from({ length: 4 }, () =>
      Math.floor(Math.random() * 10)
    )

    try {
      const reply = await conn.call({
        destination: BUS_NAME,
        path: OBJECT_PATH,
        interface: INTERFACE,
        member: 'Sum',
        signature: 'ai',
        body: [values]
      })
      appendLog(`Sum reply for [${values.join(', ')}]`, reply)
    } catch (err) {
      appendLog(`Sum call failed: ${err?.message || err}`)
    }
  }, [appendLog, requireConnection, requestName])

  const callListNames = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return

    try {
      const reply = await conn.call({
        destination: WELL_KNOWN_NAMES.DBUS,
        path: WELL_KNOWN_PATHS.DBUS,
        interface: WELL_KNOWN_INTERFACES.DBUS,
        member: WELL_KNOWN_MEMBERS.LIST_NAMES
      })
      appendLog(
        `${WELL_KNOWN_NAMES.DBUS}.${WELL_KNOWN_MEMBERS.LIST_NAMES} reply`,
        reply
      )
    } catch (err) {
      appendLog(`ListNames call failed: ${err?.message || err}`)
    }
  }, [appendLog, requireConnection])

  const emitSignal = useCallback(async () => {
    const conn = requireConnection()
    if (!conn) return
    if (!exportIdRef.current) {
      appendLog('Export the object first')
      return
    }

    if (!busNameClaimedRef.current) {
      const claimed = await requestName()
      if (!claimed) {
        appendLog('Request the demo name before emitting signals')
        return
      }
    }

    try {
      await conn.emitSignal({
        path: OBJECT_PATH,
        interface: INTERFACE,
        name: 'DemoSignal',
        signature: 's',
        body: [`Emitting at ${new Date().toISOString()}`]
      })
      appendLog('Demo signal emitted')
    } catch (err) {
      appendLog(`Failed to emit signal: ${err?.message || err}`)
    }
  }, [appendLog, requireConnection, requestName])

  const availabilityLabel = useMemo(() => {
    if (!availability) return 'Unknown'
    return availability.available ? 'Available' : 'Unavailable'
  }, [availability])

  const connectionBadgeStatus =
    connectionStatus === 'connected' ? 'running' : 'idle'
  const connectionBadgeLabel =
    connectionStatus === 'connected'
      ? `Connected (#${connectionId})`
      : 'Disconnected'

  const aside = h(
    ExampleStack,
    { gap: 'md' },
    h(
      ExamplePanel,
      {
        title: 'Session status',
        description: 'Live view of the DBus demo state.'
      },
      h(
        'div',
        { className: 'dbus-status-list' },
        h(
          'div',
          { className: 'dbus-status-item' },
          h('span', { className: 'dbus-status-label' }, 'Connection'),
          h(
            'span',
            { className: 'dbus-status-value' },
            h(
              StatusPill,
              { status: connectionBadgeStatus },
              connectionBadgeLabel
            )
          )
        ),
        h(
          'div',
          { className: 'dbus-status-item' },
          h(
            'span',
            { className: 'dbus-status-label' },
            'Session bus availability'
          ),
          h('span', { className: 'dbus-status-value' }, availabilityLabel)
        ),
        h(
          'div',
          { className: 'dbus-status-item' },
          h('span', { className: 'dbus-status-label' }, 'Well-known name'),
          h(
            'span',
            { className: 'dbus-status-value' },
            busNameClaimed ? 'Claimed' : 'Not claimed'
          )
        ),
        h(
          'div',
          { className: 'dbus-status-item' },
          h('span', { className: 'dbus-status-label' }, 'Exported object'),
          h(
            'span',
            { className: 'dbus-status-value' },
            isExported ? 'Exported' : 'Not exported'
          )
        ),
        h(
          'div',
          { className: 'dbus-status-item' },
          h('span', { className: 'dbus-status-label' }, 'Match rules'),
          h(
            'span',
            { className: 'dbus-status-value' },
            matchCount > 0 ? `${matchCount} installed` : 'None'
          )
        )
      )
    ),
    h(
      ExamplePanel,
      {
        title: 'Identifiers',
        description: 'Constants referenced by the demo.'
      },
      h(
        'div',
        { className: 'dbus-identifiers' },
        h(
          'div',
          null,
          h('strong', null, 'Bus name: '),
          h(InlineCode, null, BUS_NAME)
        ),
        h(
          'div',
          null,
          h('strong', null, 'Object path: '),
          h(InlineCode, null, OBJECT_PATH)
        ),
        h(
          'div',
          null,
          h('strong', null, 'Interface: '),
          h(InlineCode, null, INTERFACE)
        )
      )
    )
  )

  return h(
    ExampleLayout,
    {
      title: 'DBus Demo',
      description:
        'Exercise the Oro Runtime DBus bridge by connecting to the session bus, exporting objects, and handling method calls.',
      badge: h(
        StatusPill,
        { status: connectionBadgeStatus },
        connectionBadgeLabel
      ),
      aside
    },
    h(
      ExampleStack,
      { gap: 'lg' },
      status?.message &&
        h(
          Alert,
          { variant: alertVariant(status.tone) },
          h(
            AlertTitle,
            null,
            status.tone === 'error' ? 'Action failed' : 'Status'
          ),
          h(AlertDescription, null, status.message)
        ),
      h(
        ExampleGrid,
        { columns: 2 },
        h(
          ExamplePanel,
          {
            title: 'Connection',
            description:
              'Check if DBus is accessible and connect to the session bus. Linux desktops typically ship with a session bus.'
          },
          h(
            ExampleSection,
            {
              title: 'Bus controls',
              description: 'Establish or tear down a session bus connection.'
            },
            h(
              'div',
              { className: 'dbus-actions' },
              h(Button, { onClick: checkAvailability }, 'Check availability'),
              h(Button, { onClick: connect }, 'Connect to session bus'),
              h(
                Button,
                { variant: 'secondary', onClick: () => disconnect() },
                'Disconnect'
              )
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Well-known name & matches',
            description:
              'Claim the demo name and install signal match rules to stream events into JavaScript.'
          },
          h(
            ExampleSection,
            null,
            h(
              'p',
              null,
              'Claiming the demo name ensures other clients can reach our exported object. Matches forward DBus signals into the renderer.'
            )
          ),
          h(
            'div',
            { className: 'dbus-actions' },
            h(
              Button,
              { onClick: requestName, disabled: busNameClaimed },
              'Request demo name'
            ),
            h(
              Button,
              { onClick: releaseName, disabled: !busNameClaimed },
              'Release demo name'
            ),
            h(
              Button,
              { onClick: installMatches, disabled: matchCount > 0 },
              'Install match rules'
            ),
            h(
              Button,
              { onClick: () => removeMatches(), disabled: matchCount === 0 },
              'Remove matches'
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Exports, calls, and signals',
            description:
              'Expose methods to DBus, invoke them locally, and emit demo signals.'
          },
          h(
            ExampleSection,
            null,
            h(
              'p',
              null,
              'The demo exports Echo and Sum methods implemented in JavaScript. Use these controls to try the round-trips.'
            )
          ),
          h(
            'div',
            { className: 'dbus-actions' },
            h(
              Button,
              { onClick: exportObject, disabled: isExported },
              'Export demo object'
            ),
            h(
              Button,
              { onClick: unexportObject, disabled: !isExported },
              'Unexport object'
            ),
            h(
              Button,
              { onClick: callEcho, disabled: !isExported },
              'Call Echo'
            ),
            h(Button, { onClick: callSum, disabled: !isExported }, 'Call Sum'),
            h(Button, { onClick: callListNames }, 'Call ListNames'),
            h(
              Button,
              { onClick: emitSignal, disabled: !isExported },
              'Emit demo signal'
            )
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Activity log',
          description:
            'Inspect connection events, method calls, and signal payloads.'
        },
        h(
          ScrollArea,
          { className: 'dbus-log' },
          logs.length
            ? logs.map((line, index) =>
              h(
                'div',
                { key: `${index}-${line}`, className: 'dbus-log-line' },
                line
              )
            )
            : h(
              'div',
              { className: 'dbus-log-line' },
              '[ready] Check availability or connect to begin logging.'
            ),
          h('div', { ref: logEndRef })
        )
      )
    )
  )
}

mountExample(DBusDemoApp)
