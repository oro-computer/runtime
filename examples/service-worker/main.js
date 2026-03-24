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

const { createElement: h, useState, useMemo, useCallback, useEffect } = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function ServiceWorkerDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Waiting to register the service worker.',
    message: 'No worker registered yet.'
  })
  const [logs, setLogs] = useState([])
  const [isReady, setIsReady] = useState(false)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 300 ? next.slice(next.length - 300) : next
    })
  }, [])

  const updateStatus = useCallback((variant, title, message) => {
    setStatus({ variant, title, message })
  }, [])

  const registerWorker = useCallback(async () => {
    if (!('serviceWorker' in navigator)) {
      appendLog('Service worker API is not available in this environment.')
      updateStatus(
        'danger',
        'Service worker unsupported',
        'navigator.serviceWorker is not available.'
      )
      return false
    }

    try {
      updateStatus(
        'warning',
        'Registering service worker…',
        'Awaiting ready state.'
      )
      const registration = await navigator.serviceWorker.register(
        './serviceworker.js',
        { scope: '/' }
      )
      await navigator.serviceWorker.ready
      const scope = registration.scope || '/'
      appendLog(`Service worker ready (scope: ${scope})`)
      updateStatus('success', 'Service worker ready', `Active scope: ${scope}`)
      setIsReady(true)
      return true
    } catch (error) {
      const message = formatError(error)
      appendLog(`Registration failed: ${message}`)
      updateStatus('danger', 'Registration failed', message)
      setIsReady(false)
      throw error
    }
  }, [appendLog, updateStatus])

  const fetchPing = useCallback(async () => {
    try {
      const response = await fetch('/ping')
      const text = await response.text()
      appendLog(`/ping -> ${response.status} ${JSON.stringify(text)}`)
      updateStatus('info', 'Fetched /ping', `Response: ${text}`)
      return { response, text }
    } catch (error) {
      const message = formatError(error)
      appendLog(`Failed to fetch /ping: ${message}`)
      updateStatus('danger', 'Fetch failed', message)
      throw error
    }
  }, [appendLog, updateStatus])

  const fetchOther = useCallback(async () => {
    try {
      const response = await fetch('./other')
      const text = await response.text()
      appendLog(`/other -> ${response.status} ${JSON.stringify(text)}`)
      updateStatus('info', 'Fetched ./other', `Response: ${text}`)
      return { response, text }
    } catch (error) {
      const message = formatError(error)
      appendLog(`Failed to fetch ./other: ${message}`)
      updateStatus('danger', 'Fetch failed', message)
      throw error
    }
  }, [appendLog, updateStatus])

  useEffect(() => {
    const params = new URLSearchParams(globalThis.location?.search || '')
    if (params.get('auto') !== '1') return

    const runAuto = async () => {
      try {
        await registerWorker()
        const { text } = await fetchPing()
        document.title = text
        appendLog(`auto fetch /ping -> ${text}`)
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
  }, [appendLog, fetchPing, registerWorker, updateStatus])

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
      title: 'Service Worker Basics',
      description:
        'Register a service worker, exercise fetch handlers, and observe runtime output.'
    },
    h(
      ExamplePanel,
      {
        title: 'Service Worker Control',
        description:
          'Use the actions below to manage registration and run fetch scenarios.'
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
              'Walk through registration and fetch flows intercepted by the service worker.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                variant: isReady ? 'secondary' : 'default',
                type: 'button',
                onClick: () => registerWorker().catch(() => {})
              },
              isReady ? 'Re-register worker' : 'Register worker'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => fetchPing().catch(() => {}),
                disabled: !isReady
              },
              'Fetch /ping'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => fetchOther().catch(() => {}),
                disabled: !isReady
              },
              'Fetch ./other'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'What this demonstrates',
            description: 'Key behaviours exposed through this example.'
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
                'Registering workers at application startup with custom scopes.'
              ),
              h(
                'li',
                null,
                'Intercepting navigation requests such as ',
                h(InlineCode, null, '/ping'),
                '.'
              ),
              h(
                'li',
                null,
                'Progressing through test harness automation via ',
                h(InlineCode, null, '?auto=1'),
                '.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Latest events streamed from this page.',
            actions: h(
              Button,
              {
                variant: 'ghost',
                size: 'sm',
                type: 'button',
                onClick: () => setLogs([])
              },
              'Clear log'
            )
          },
          h(LogViewer, {
            entries: logs,
            emptyState: 'Waiting for service worker events…'
          })
        )
      )
    )
  )
}

mountExample(ServiceWorkerDemo)
