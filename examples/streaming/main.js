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
  useMemo,
  useEffect,
  useRef
} = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function StreamingDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Idle',
    message: 'Register the service worker to stream responses through it.'
  })
  const [logs, setLogs] = useState([])
  const [isRegistered, setIsRegistered] = useState(false)
  const [isStreaming, setIsStreaming] = useState(false)
  const abortRef = useRef(null)
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
        scope: './'
      })
      await navigator.serviceWorker.ready
      setIsRegistered(true)
      appendLog('Service worker registered with ./ scope.')
      updateStatus(
        'success',
        'Service worker ready',
        'Ready to fetch streamed responses.'
      )
      return true
    } catch (error) {
      const message = formatError(error)
      appendLog(`Registration failed: ${message}`)
      updateStatus('danger', 'Registration failed', message)
      setIsRegistered(false)
      throw error
    }
  }, [appendLog, updateStatus])

  const stopStream = useCallback(() => {
    if (abortRef.current) {
      abortRef.current.abort()
      abortRef.current = null
    }
    if (isStreaming) {
      appendLog('Streaming cancelled.')
    }
    setIsStreaming(false)
  }, [appendLog, isStreaming])

  const startStream = useCallback(async () => {
    if (!isRegistered) {
      const ok = await registerWorker()
      if (!ok) return
    }

    stopStream()

    try {
      const controller = new AbortController()
      abortRef.current = controller
      setIsStreaming(true)
      updateStatus('info', 'Fetching stream…', 'Reading chunks from ./stream.')
      appendLog('Fetching ./stream via service worker…')

      const response = await fetch('./stream', { signal: controller.signal })
      if (!response.body) {
        appendLog(`No readable body; status ${response.status}`)
        updateStatus(
          'warning',
          'No body to stream',
          `Status ${response.status}`
        )
        setIsStreaming(false)
        return
      }

      const reader = response.body.getReader()
      const decoder = new TextDecoder()
      while (true) {
        const { done, value } = await reader.read()
        if (done) break
        const chunk = decoder.decode(value, { stream: true })
        appendLog(chunk.trim() || '[empty chunk]')
      }

      appendLog('Stream completed.')
      updateStatus('success', 'Stream complete', 'All chunks received.')
      setIsStreaming(false)
      abortRef.current = null
      if (isAutoRef.current) {
        document.title = 'stream-ok'
      }
    } catch (error) {
      if (error?.name === 'AbortError') {
        appendLog('Stream aborted by user.')
        updateStatus('warning', 'Stream aborted', 'Aborted via Stop button.')
      } else {
        const message = formatError(error)
        appendLog(`Streaming failed: ${message}`)
        updateStatus('danger', 'Streaming failed', message)
        if (isAutoRef.current) {
          document.title = 'error'
        }
      }
      setIsStreaming(false)
      abortRef.current = null
    }
  }, [
    appendLog,
    isAutoRef,
    isRegistered,
    registerWorker,
    stopStream,
    updateStatus
  ])

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
      title: 'Streaming via Service Worker',
      description:
        'Stream chunked responses intercepted by a service worker and inspect delivery timing.'
    },
    h(
      ExamplePanel,
      {
        title: 'Streaming Monitor',
        description:
          'Register the worker, kick off the streaming fetch, and review emitted chunks.'
      },
      h(
        ExampleStack,
        { gap: 'md' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Actions',
            description: 'Registration required before fetching the stream.'
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
              isStreaming ? 'Streaming…' : 'Start stream'
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
            description: 'Key capabilities surfaced through this example.'
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
                'Streaming fetch responses through a service worker-controlled scope.'
              ),
              h(
                'li',
                null,
                'Incrementally rendering chunks in the UI as they arrive.'
              ),
              h(
                'li',
                null,
                'Automated verification using ',
                h(InlineCode, null, '?auto=1'),
                ' to set ',
                h(InlineCode, null, 'document.title'),
                '.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Stream output',
            description: 'Each chunk appended in the order received.',
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
            emptyState: 'Start the stream to view chunked output.'
          })
        )
      )
    )
  )
}

mountExample(StreamingDemo)
