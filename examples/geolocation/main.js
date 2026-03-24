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
  LogViewer,
  StatusPill
} from '../ui/index.js'

import { createGeolocationWatcher } from './watch.js'

const {
  createElement: h,
  useState,
  useMemo,
  useCallback,
  useEffect,
  useRef
} = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function formatCoords (coords) {
  if (!coords) return 'No fix yet'
  const { latitude, longitude, accuracy, altitude } = coords
  const parts = [
    latitude != null ? `lat ${latitude.toFixed(6)}` : null,
    longitude != null ? `lon ${longitude.toFixed(6)}` : null,
    accuracy != null ? `±${accuracy}m` : null,
    altitude != null ? `alt ${altitude}m` : null
  ].filter(Boolean)
  return parts.join(' · ') || 'No fix yet'
}

function GeolocationApp () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Idle',
    message: 'Start the watcher to stream location updates.'
  })
  const [logs, setLogs] = useState([])
  const [coords, setCoords] = useState(null)
  const [errorMessage, setErrorMessage] = useState(null)
  const [isWatching, setIsWatching] = useState(false)
  const watcherRef = useRef(null)
  const shouldWatchRef = useRef(false)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const ensureWatcher = useCallback(() => {
    if (!watcherRef.current) {
      watcherRef.current = createGeolocationWatcher({
        onPosition: (position) => {
          const { coords: nextCoords } = position || {}
          setCoords(nextCoords || null)
          if (nextCoords) {
            appendLog(
              `position -> ${nextCoords.latitude},${nextCoords.longitude}`
            )
            setErrorMessage(null)
          }
        },
        onError: (message) => {
          appendLog(`error -> ${message}`)
          setErrorMessage(message)
          setStatus({
            variant: 'danger',
            title: 'Watch error',
            message
          })
          setIsWatching(false)
        },
        log: (message) => appendLog(message)
      })
    }
    return watcherRef.current
  }, [appendLog])

  const startWatch = useCallback(async () => {
    const watcher = ensureWatcher()
    try {
      const ok = await watcher.ensurePermission()
      if (!ok) {
        appendLog('permission denied')
        setStatus({
          variant: 'danger',
          title: 'Permission denied',
          message: 'Grant geolocation permission to receive updates.'
        })
        setErrorMessage('Permission denied')
        shouldWatchRef.current = false
        setIsWatching(false)
        return
      }
      watcher.start()
      shouldWatchRef.current = true
      setIsWatching(true)
      setStatus({
        variant: 'success',
        title: 'Watching position',
        message: 'Streaming geolocation updates with high accuracy.'
      })
      appendLog('watch started')
    } catch (error) {
      const message = formatError(error)
      appendLog(`failed to start: ${message}`)
      setStatus({
        variant: 'danger',
        title: 'Failed to start watch',
        message
      })
      setErrorMessage(message)
      shouldWatchRef.current = false
      setIsWatching(false)
    }
  }, [appendLog, ensureWatcher])

  const stopWatch = useCallback(() => {
    const watcher = watcherRef.current
    if (watcher) watcher.stop()
    shouldWatchRef.current = false
    setIsWatching(false)
    setStatus({
      variant: 'info',
      title: 'Watch stopped',
      message: 'Start again to resume streaming positions.'
    })
    appendLog('watch stopped')
  }, [appendLog])

  useEffect(() => {
    const handlePause = () => {
      const watcher = watcherRef.current
      if (watcher && watcher.isWatching()) {
        watcher.stop()
        appendLog('paused by runtime; watch stopped')
      }
    }
    const handleResume = () => {
      if (shouldWatchRef.current) {
        setTimeout(() => {
          const watcher = ensureWatcher()
          watcher.start()
          appendLog('runtime resumed; watch restarted')
        }, 100)
      }
    }
    const handleStop = () => {
      const watcher = watcherRef.current
      if (watcher) watcher.stop()
      appendLog('runtime stopped; watch cleared')
    }

    globalThis.addEventListener('applicationpause', handlePause)
    globalThis.addEventListener('applicationresume', handleResume)
    globalThis.addEventListener('applicationstop', handleStop)
    return () => {
      globalThis.removeEventListener('applicationpause', handlePause)
      globalThis.removeEventListener('applicationresume', handleResume)
      globalThis.removeEventListener('applicationstop', handleStop)
    }
  }, [appendLog, ensureWatcher])

  useEffect(
    () => () => {
      const watcher = watcherRef.current
      if (watcher) watcher.stop()
    },
    []
  )

  const statusBanner = useMemo(
    () =>
      h(StatusBanner, {
        variant: status.variant,
        title: status.title,
        message: status.message
      }),
    [status]
  )

  return h(
    ExampleLayout,
    {
      title: 'Geolocation Watch',
      description:
        'Request permission, stream positions, and observe lifecycle-aware behaviour.'
    },
    h(
      ExamplePanel,
      {
        title: 'Geolocation monitor',
        description: 'Start the watcher to follow device location updates.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        errorMessage &&
          h(StatusBanner, {
            variant: 'danger',
            title: 'Latest error',
            message: errorMessage,
            assertive: true
          }),
        h(
          ExampleSection,
          {
            title: 'Controls',
            description:
              'Watch can be paused automatically during application lifecycle transitions.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () => startWatch().catch(() => {}),
                disabled: isWatching
              },
              isWatching ? 'Watching…' : 'Start watch'
            ),
            h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                onClick: () => stopWatch(),
                disabled: !isWatching && !shouldWatchRef.current
              },
              'Stop watch'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Latest fix',
            description: 'Most recent coordinates reported by the runtime.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              StatusPill,
              { status: isWatching ? 'running' : 'idle' },
              isWatching ? 'watching' : 'stopped'
            ),
            h(
              'div',
              { className: 'ui-prose' },
              h('p', null, h(InlineCode, null, formatCoords(coords)))
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
                'Requests geolocation permission using the ',
                h(InlineCode, null, 'navigator.permissions'),
                ' API.'
              ),
              h(
                'li',
                null,
                'Streams updates via ',
                h(InlineCode, null, 'navigator.geolocation.watchPosition'),
                ' with high accuracy enabled.'
              ),
              h(
                'li',
                null,
                'Automatically stops on ',
                h(InlineCode, null, 'applicationpause'),
                ' and restarts on resume.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Raw events emitted while the watcher is running.',
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
            emptyState: 'Start the watcher to populate logs.'
          })
        )
      )
    )
  )
}

mountExample(GeolocationApp)
