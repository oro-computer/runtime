import application from 'oro:application'

import React from '../ui/react.js'
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
  LogViewer
} from '../ui/index.js'

const {
  createElement: h,
  useState,
  useMemo,
  useCallback,
  useEffect,
  useRef
} = React

const PEER_INDEX = 5

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function WindowMessagingDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Ready to test window messaging',
    message: 'Open a peer window to exchange messages.'
  })
  const [logs, setLogs] = useState([])
  const [pending, setPending] = useState(null)
  const [peerOpened, setPeerOpened] = useState(false)
  const windowRef = useRef(null)

  const appendLog = useCallback((message) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${message}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const updateStatus = useCallback((variant, title, message) => {
    setStatus({ variant, title, message })
  }, [])

  const getCurrentWindow = useCallback(async () => {
    if (windowRef.current) return windowRef.current
    const win = await application.getCurrentWindow()
    windowRef.current = win
    return win
  }, [])

  useEffect(() => {
    const listener = (event) => {
      try {
        const payload =
          typeof event.detail !== 'undefined' ? event.detail : event.data
        appendLog(`Message received: ${JSON.stringify(payload)}`)
      } catch (error) {
        appendLog(`Message parse error: ${formatError(error)}`)
      }
    }
    globalThis.addEventListener('message', listener)
    return () => globalThis.removeEventListener('message', listener)
  }, [appendLog])

  const openPeer = useCallback(async () => {
    setPending('open')
    try {
      await application.createWindow({
        index: PEER_INDEX,
        path: 'examples/window/messaging.html',
        title: 'Peer window'
      })
      setPeerOpened(true)
      appendLog('Peer window opened.')
      updateStatus(
        'success',
        'Peer window ready',
        'The peer can now receive messages.'
      )
    } catch (error) {
      const message = formatError(error)
      appendLog(`Failed to open peer: ${message}`)
      updateStatus('danger', 'Failed to open peer window', message)
    } finally {
      setPending(null)
    }
  }, [appendLog, updateStatus])

  const getPeerWindow = useCallback(async () => {
    const peer = await application.getWindow(PEER_INDEX, { max: false })
    if (!peer) {
      appendLog('Peer window not found.')
      updateStatus(
        'warning',
        'Peer window unavailable',
        'Open the peer window before sending messages.'
      )
      return null
    }
    return peer
  }, [appendLog, updateStatus])

  const sendPing = useCallback(async () => {
    setPending('ping')
    try {
      const peer = await getPeerWindow()
      if (!peer) return
      await peer.postMessage('ping from main window')
      appendLog('Ping sent to peer via postMessage.')
      updateStatus(
        'success',
        'Ping sent',
        'peer.postMessage("ping from main window")'
      )
    } catch (error) {
      const message = formatError(error)
      appendLog(`Ping failed: ${message}`)
      updateStatus('danger', 'Ping failed', message)
    } finally {
      setPending(null)
    }
  }, [appendLog, getPeerWindow, updateStatus])

  const sendJson = useCallback(async () => {
    setPending('json')
    try {
      const [current, peer] = await Promise.all([
        getCurrentWindow(),
        getPeerWindow()
      ])
      if (!peer) return
      const payload = {
        now: Date.now(),
        msg: 'hello peer',
        via: 'window.send'
      }
      await current.send({
        window: PEER_INDEX,
        event: 'message',
        value: payload
      })
      appendLog(`JSON sent via window.send: ${JSON.stringify(payload)}`)
      updateStatus(
        'success',
        'JSON payload sent',
        'Used ApplicationWindow.send to deliver structured data.'
      )
    } catch (error) {
      const message = formatError(error)
      appendLog(`Send failed: ${message}`)
      updateStatus('danger', 'JSON send failed', message)
    } finally {
      setPending(null)
    }
  }, [appendLog, getCurrentWindow, getPeerWindow, updateStatus])

  const statusBanner = useMemo(() => {
    return h(StatusBanner, {
      variant: status.variant,
      title: status.title,
      message: status.message
    })
  }, [status])

  return h(
    ExampleLayout,
    {
      title: 'Window Messaging',
      description:
        'Create secondary windows and exchange messages using postMessage and ApplicationWindow.send.'
    },
    h(
      ExamplePanel,
      {
        title: 'Messaging controls',
        description:
          'Use these helpers to test window-to-window communication.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Actions',
            description:
              'Open a peer window, then send string or JSON payloads.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () => openPeer().catch(() => {}),
                disabled: pending && pending !== 'open'
              },
              pending === 'open'
                ? 'Opening…'
                : peerOpened
                  ? 'Re-open peer'
                  : 'Open peer window'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => sendPing().catch(() => {}),
                disabled: pending && pending !== 'ping'
              },
              pending === 'ping' ? 'Sending…' : 'Send ping'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => sendJson().catch(() => {}),
                disabled: pending && pending !== 'json'
              },
              pending === 'json' ? 'Sending…' : 'Send JSON payload'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Messaging APIs'
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
                h(InlineCode, null, 'Application.createWindow'),
                ' creates the peer window at index ',
                h(InlineCode, null, String(PEER_INDEX)),
                '.'
              ),
              h(
                'li',
                null,
                h(InlineCode, null, 'peer.postMessage'),
                ' is used for classic postMessage-style communication.'
              ),
              h(
                'li',
                null,
                h(InlineCode, null, 'current.send'),
                ' demonstrates routing structured payloads via the runtime messaging API.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Incoming messages and outgoing actions.',
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
            emptyState: 'Open the peer window to start exchanging messages.'
          })
        )
      )
    )
  )
}

mountExample(WindowMessagingDemo)
