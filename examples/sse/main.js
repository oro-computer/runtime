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
  useCallback,
  useEffect,
  useMemo,
  useRef
} = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function SseDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Idle',
    message: 'Register the service worker to enable SSE proxying.'
  })
  const [logs, setLogs] = useState([])
  const [isRegistered, setIsRegistered] = useState(false)
  const [isStreaming, setIsStreaming] = useState(false)
  const sourceRef = useRef(null)
  const isAutoRef = useRef(false)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const updateStatus = useCallback((variant, title, message) => {
    setStatus({ variant, title, message })
  }, [])

  const registerWorker = useCallback(async () => {
    if (!('serviceWorker' in navigator)) {
      appendLog('Service worker API unavailable.')
      updateStatus(
        'danger',
        'Service worker unsupported',
        'navigator.serviceWorker is not present.'
      )
      return false
    }

    try {
      updateStatus(
        'warning',
        'Registering service worker…',
        'Awaiting ready event.'
      )
      await navigator.serviceWorker.register('./serviceworker.js', {
        scope: '/'
      })
      await navigator.serviceWorker.ready
      setIsRegistered(true)
      appendLog('Service worker ready for SSE routing.')
      updateStatus(
        'success',
        'Service worker ready',
        'You can start the SSE stream.'
      )
      return true
    } catch (error) {
      const message = formatError(error)
      setIsRegistered(false)
      appendLog(`Registration failed: ${message}`)
      updateStatus('danger', 'Registration failed', message)
      throw error
    }
  }, [appendLog, updateStatus])

  const stopStream = useCallback(() => {
    if (!sourceRef.current) return
    try {
      sourceRef.current.close()
    } catch {}
    sourceRef.current = null
    setIsStreaming(false)
    appendLog('SSE stream closed.')
  }, [appendLog])

  const startStream = useCallback(async () => {
    if (!isRegistered) {
      const ok = await registerWorker()
      if (!ok) return
    }

    stopStream()

    try {
      const EventSourceCtor = globalThis.EventSource

      if (typeof EventSourceCtor !== 'function') {
        throw new Error('EventSource is not available in this context')
      }

      const source = new EventSourceCtor('/events')
      sourceRef.current = source
      setIsStreaming(true)
      updateStatus(
        'info',
        'Listening for events…',
        'Receiving /events via the service worker.'
      )
      appendLog('Opened EventSource /events')

      let count = 0
      source.addEventListener('message', (event) => {
        appendLog(`event: ${event.data}`)
        count += 1
        if (count >= 10) {
          appendLog('Received 10 messages, closing stream.')
          stopStream()
          if (isAutoRef.current) {
            document.title = 'sse-ok'
          }
        }
      })

      source.addEventListener('error', (event) => {
        const message = formatError(event)
        appendLog(`EventSource error: ${message}`)
        if (count > 0) {
          stopStream()
        } else {
          updateStatus(
            'warning',
            'Event stream closed',
            'No events received before closure.'
          )
          stopStream()
        }
      })
    } catch (error) {
      const message = formatError(error)
      appendLog(`Failed to start SSE: ${message}`)
      updateStatus('danger', 'Failed to start SSE', message)
      stopStream()
      throw error
    }
  }, [appendLog, isRegistered, registerWorker, stopStream])

  useEffect(() => {
    return () => {
      stopStream()
    }
  }, [stopStream])

  useEffect(() => {
    const params = new URLSearchParams(globalThis.location?.search || '')
    if (params.get('auto') !== '1') return
    isAutoRef.current = true

    const runAuto = async () => {
      try {
        await registerWorker()
        await startStream()
      } catch (error) {
        const message = formatError(error)
        document.title = 'error'
        appendLog(`auto flow failed: ${message}`)
        updateStatus('danger', 'Auto mode failed', message)
      }
    }

    runAuto().catch((error) => {
      const message = formatError(error)
      document.title = 'error'
      appendLog(`auto flow failed: ${message}`)
      updateStatus('danger', 'Auto mode failed', message)
    })
  }, [appendLog, registerWorker, startStream, updateStatus])

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
      title: 'Service Worker SSE',
      description:
        'Proxy server-sent events through a service worker and observe delivery in real time.'
    },
    h(
      ExamplePanel,
      {
        title: 'Event Stream Monitor',
        description:
          'Register the worker, start the EventSource stream, and watch the incoming events.'
      },
      h(
        ExampleStack,
        { gap: 'md' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Actions',
            description:
              'Service workers must be registered before streaming events.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                variant: isRegistered ? 'secondary' : 'default',
                onClick: () => registerWorker().catch(() => {})
              },
              isRegistered ? 'Re-register worker' : 'Register worker'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => startStream().catch(() => {}),
                disabled: isStreaming
              },
              isStreaming ? 'Streaming…' : 'Start SSE'
            ),
            h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                onClick: () => stopStream(),
                disabled: !isStreaming
              },
              'Stop stream'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'What this demonstrates',
            description:
              'Key takeaways for integrating SSE with Socket service workers.'
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
                'Using service workers to intercept and proxy ',
                h(InlineCode, null, '/events'),
                '.'
              ),
              h(
                'li',
                null,
                'Managing long-lived EventSource connections inside the runtime UI.'
              ),
              h(
                'li',
                null,
                'Automating regression tests with ',
                h(InlineCode, null, '?auto=1'),
                ' to mark success in the document title.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Event log',
            description: 'Live feed of messages and lifecycle changes.',
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
            emptyState: 'Waiting for events…'
          })
        )
      )
    )
  )
}

mountExample(SseDemo)
