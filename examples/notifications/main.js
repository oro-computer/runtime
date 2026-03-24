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

import { createNotificationController } from './basic.js'

const {
  createElement: h,
  useState,
  useMemo,
  useCallback,
  useEffect,
  useRef
} = React

function NotificationsApp () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Idle',
    message:
      'Request permission, then send desktop notifications from the runtime.'
  })
  const [logs, setLogs] = useState([])
  const [permission, setPermission] = useState('default')
  const [lastResponse, setLastResponse] = useState(null)
  const controllerRef = useRef(null)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  useEffect(() => {
    controllerRef.current = createNotificationController({
      log: (message) => appendLog(message),
      onResponse: (detail) => {
        setLastResponse(detail)
      }
    })
    return () => {
      controllerRef.current?.dispose()
      controllerRef.current = null
    }
  }, [appendLog])

  const requestPermission = useCallback(async () => {
    try {
      const ok = await controllerRef.current.ensurePermission()
      const state = ok ? 'granted' : 'denied'
      setPermission(state)
      setStatus({
        variant: ok ? 'success' : 'danger',
        title: ok ? 'Permission granted' : 'Permission denied',
        message: ok
          ? 'Notifications can be displayed while the app is running.'
          : 'Grant permission in system settings to enable notifications.'
      })
      appendLog(`permission -> ${state}`)
    } catch (error) {
      const message = error?.message || String(error)
      setPermission('denied')
      setStatus({
        variant: 'danger',
        title: 'Permission request failed',
        message
      })
      appendLog(`permission request failed: ${message}`)
    }
  }, [appendLog])

  const showNotification = useCallback(async () => {
    try {
      await controllerRef.current.showNotification('Oro Runtime Notification', {
        body: 'Tap to focus the application window',
        tag: 'demo',
        requireInteraction: false,
        silent: false
      })
      setStatus({
        variant: 'success',
        title: 'Notification dispatched',
        message: 'Await response events to confirm user interaction.'
      })
    } catch (error) {
      const message = error?.message || String(error)
      setStatus({
        variant: 'danger',
        title: 'Failed to show notification',
        message
      })
      appendLog(`notification error: ${message}`)
    }
  }, [appendLog])

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
      title: 'Notifications',
      description:
        'Exercise the notification API, track permission state, and observe user responses.'
    },
    h(
      ExamplePanel,
      {
        title: 'Notification controls',
        description: 'Request runtime permissions and dispatch notifications.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Permission state'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              StatusPill,
              {
                status:
                  permission === 'granted'
                    ? 'success'
                    : permission === 'denied'
                      ? 'error'
                      : 'idle'
              },
              permission
            ),
            h(
              ExampleProse,
              null,
              h(
                'p',
                null,
                'Permissions API used: ',
                h(InlineCode, null, 'navigator.permissions')
              ),
              h(
                'p',
                null,
                'Notifications module: ',
                h(InlineCode, null, 'oro:notification')
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Actions'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () => requestPermission().catch(() => {})
              },
              'Request permission'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => showNotification().catch(() => {}),
                disabled: permission === 'denied'
              },
              'Show notification'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Last response',
            description: 'Latest notificationresponse event payload.'
          },
          lastResponse
            ? h('pre', null, JSON.stringify(lastResponse, null, 2))
            : h(
              'p',
              { className: 'ui-prose' },
              'No notification responses yet.'
            )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Permission changes and notification events.',
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
            emptyState: 'Request permission to populate logs.'
          })
        )
      )
    )
  )
}

mountExample(NotificationsApp)
