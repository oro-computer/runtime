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

import hooks from 'oro:hooks'

const { createElement: h, useState, useMemo, useEffect, useCallback } = React

function LifecycleApp () {
  const [logs, setLogs] = useState([])
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Monitoring runtime lifecycle',
    message: 'Interact with the window to trigger events.'
  })

  const appendLog = useCallback((event, payload) => {
    const ts = new Date().toISOString().slice(11, 23)
    const message = payload ? `${event} -> ${payload}` : event
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${message}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  useEffect(() => {
    const pauseDispose = hooks.onApplicationPause(() => {
      appendLog('applicationpause')
      setStatus({
        variant: 'warning',
        title: 'Application paused',
        message: 'Runtime emitted applicationpause.'
      })
    })
    const resumeDispose = hooks.onApplicationResume(() => {
      appendLog('applicationresume')
      setStatus({
        variant: 'success',
        title: 'Application resumed',
        message: 'Runtime emitted applicationresume.'
      })
    })
    const stopHandler = () => {
      appendLog('applicationstop')
      setStatus({
        variant: 'danger',
        title: 'Application stopped',
        message: 'Runtime emitted applicationstop.'
      })
    }

    const focusHandler = () => appendLog('window focus')
    const blurHandler = () => appendLog('window blur')
    globalThis.addEventListener('focus', focusHandler)
    globalThis.addEventListener('blur', blurHandler)
    globalThis.addEventListener('applicationstop', stopHandler)

    return () => {
      pauseDispose?.()
      resumeDispose?.()
      globalThis.removeEventListener('applicationstop', stopHandler)
      globalThis.removeEventListener('focus', focusHandler)
      globalThis.removeEventListener('blur', blurHandler)
    }
  }, [appendLog])

  const dispatchEvent = useCallback(
    (type) => {
      try {
        globalThis.dispatchEvent(new Event(type))
        appendLog(`dispatched ${type}`)
      } catch (error) {
        appendLog(`dispatch failed ${type} -> ${error?.message || error}`)
      }
    },
    [appendLog]
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
      title: 'Application Lifecycle',
      description:
        'Listen for runtime lifecycle events and dispatch simulated transitions.'
    },
    h(
      ExamplePanel,
      {
        title: 'Lifecycle monitor',
        description:
          'Pause/resume/stop events are emitted by the runtime and mirrored here.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Dispatch helpers',
            description: 'Trigger synthetic lifecycle events to test handlers.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () => dispatchEvent('applicationpause')
              },
              'Dispatch applicationpause'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => dispatchEvent('applicationresume')
              },
              'Dispatch applicationresume'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: () => dispatchEvent('applicationstop')
              },
              'Dispatch applicationstop'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Desktop modes'
          },
          h(
            ExampleProse,
            null,
            h(
              'p',
              null,
              'Configure lifecycle behaviour with ',
              h(InlineCode, null, 'lifecycle_desktop_always_running'),
              ' in ',
              h(InlineCode, null, 'oro.toml'),
              '.'
            ),
            h(
              'p',
              null,
              'Set the value to ',
              h(InlineCode, null, 'false'),
              ' to receive real pause/resume events on desktop.'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Event log',
            description: 'Window focus/blur and lifecycle emissions.',
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
            emptyState: 'Waiting for lifecycle events.'
          })
        )
      )
    )
  )
}

mountExample(LifecycleApp)
