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

const { createElement: h, useState, useMemo, useCallback, useRef } = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function formatColor (value) {
  if (!value) return 'not set'
  if (typeof value === 'string') return value
  if (typeof value === 'object') {
    try {
      return JSON.stringify(value)
    } catch {}
  }
  return String(value)
}

const ACTIONS = [
  {
    key: 'light',
    label: 'Set light background',
    description: 'Apply an opaque light background colour.',
    async run (win, { updateColor }) {
      const color = { red: 255, green: 255, blue: 255, alpha: 1 }
      await win.setBackgroundColor(color)
      updateColor(color)
      return 'Background set to light'
    }
  },
  {
    key: 'dark',
    label: 'Set dark background',
    description: 'Switch to the dark acrylic colour used in Oro examples.',
    async run (win, { updateColor }) {
      const color = { red: 15, green: 18, blue: 28, alpha: 1 }
      await win.setBackgroundColor(color)
      updateColor(color)
      return 'Background set to dark'
    }
  },
  {
    key: 'clear',
    label: 'Clear background',
    description:
      'Request a transparent background (platform support required).',
    async run (win, { updateColor }) {
      const color = { red: 0, green: 0, blue: 0, alpha: 0 }
      await win.setBackgroundColor(color)
      updateColor(color)
      return 'Background cleared (transparent when supported)'
    }
  },
  {
    key: 'read',
    label: 'Read background',
    description: 'Query the current window background colour.',
    async run (win, { updateColor }) {
      const value = await win.getBackgroundColor()
      updateColor(value)
      return `Current background: ${formatColor(value)}`
    }
  }
]

function WindowAppearanceDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Ready to control window appearance',
    message: 'Use the actions below to change the background colour.'
  })
  const [logs, setLogs] = useState([])
  const [activeColor, setActiveColor] = useState(null)
  const [pending, setPending] = useState(null)
  const windowRef = useRef(null)

  const appendLog = useCallback((message) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${message}`]
      return next.length > 300 ? next.slice(next.length - 300) : next
    })
  }, [])

  const updateStatus = useCallback((variant, title, message) => {
    setStatus({ variant, title, message })
  }, [])

  const getWindow = useCallback(async () => {
    if (windowRef.current) return windowRef.current
    const win = await application.getCurrentWindow()
    windowRef.current = win
    return win
  }, [])

  const handleAction = useCallback(
    async (action) => {
      if (!action) return
      setPending(action.key)
      try {
        updateStatus('info', `Running "${action.label}"`, action.description)
        const win = await getWindow()
        const helpers = {
          updateColor: (value) => setActiveColor(value)
        }
        const result = await action.run(win, helpers)
        const message = typeof result === 'string' ? result : 'Action complete.'
        appendLog(message)
        updateStatus('success', action.label, message)
      } catch (error) {
        const message = formatError(error)
        appendLog(`Error: ${message}`)
        updateStatus('danger', `${action.label} failed`, message)
      } finally {
        setPending(null)
      }
    },
    [appendLog, getWindow, updateStatus]
  )

  const statusBanner = useMemo(() => {
    return h(StatusBanner, {
      variant: status.variant,
      title: status.title,
      message: status.message
    })
  }, [status])

  const currentColor = useMemo(() => formatColor(activeColor), [activeColor])

  return h(
    ExampleLayout,
    {
      title: 'Window Appearance',
      description:
        'Control the background colour of the current window and inspect the applied values.'
    },
    h(
      ExamplePanel,
      {
        title: 'Appearance controls',
        description:
          'These actions call window APIs exposed through oro:application.'
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
              'Each button triggers a background colour update via the native window.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            ACTIONS.map((action) =>
              h(
                Button,
                {
                  key: action.key,
                  type: 'button',
                  variant: action.key === 'clear' ? 'secondary' : 'default',
                  disabled: pending && pending !== action.key,
                  onClick: () => handleAction(action)
                },
                pending === action.key ? 'Working…' : action.label
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'About these APIs'
          },
          h(
            ExampleProse,
            null,
            h(
              'p',
              null,
              'The underlying calls use ',
              h(InlineCode, null, 'window.setBackgroundColor'),
              ' and ',
              h(InlineCode, null, 'window.getBackgroundColor'),
              ' to bridge into native window chrome.'
            ),
            h(
              'p',
              null,
              'On some platforms transparent backgrounds require enabling composition in configuration. When unsupported, the runtime falls back to an opaque colour.'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Current background',
            description: 'Latest colour value recorded from the runtime.'
          },
          h(
            'div',
            { className: 'ui-prose' },
            h('p', null, h(InlineCode, null, currentColor))
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Recent appearance operations.',
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
            emptyState: 'No appearance updates yet.'
          })
        )
      )
    )
  )
}

mountExample(WindowAppearanceDemo)
