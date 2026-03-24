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

function serialise (value) {
  try {
    return JSON.stringify(value)
  } catch {
    return String(value)
  }
}

const ACTIONS = [
  {
    key: 'open',
    label: 'Open file(s)',
    description: 'Allow picking one or more files from the host file system.',
    async run (win) {
      const result = await win.showOpenFilePicker({ allowMultiple: true })
      return {
        message: `Open picker returned ${Array.isArray(result) ? result.length : 0} path(s).`,
        details: result
      }
    }
  },
  {
    key: 'save',
    label: 'Save file',
    description: 'Prompt the user for a file location with a default name.',
    async run (win) {
      const result = await win.showSaveFilePicker({
        defaultName: 'example.txt'
      })
      return {
        message: 'Save picker returned a path.',
        details: result
      }
    }
  },
  {
    key: 'directory',
    label: 'Select directory',
    description:
      'Allow the user to pick a directory and enumerate its entries.',
    async run (win) {
      const result = await win.showDirectoryFilePicker({})
      return {
        message: `Directory picker returned ${Array.isArray(result) ? result.length : 0} path(s).`,
        details: result
      }
    }
  }
]

function WindowFilePickersDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Ready to launch file pickers',
    message: 'Invoke the native open/save dialogs from the current window.'
  })
  const [logs, setLogs] = useState([])
  const [pending, setPending] = useState(null)
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
        const outcome = await action.run(win)
        const message = outcome?.message || 'Picker completed.'
        const details = outcome?.details
        appendLog(`${message} -> ${serialise(details)}`)
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

  return h(
    ExampleLayout,
    {
      title: 'Window File Pickers',
      description:
        'Demonstrate the native file picker APIs exposed through oro:application.'
    },
    h(
      ExamplePanel,
      {
        title: 'Picker controls',
        description:
          'Trigger open, save, and directory selections from the active window.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Actions',
            description: 'Each button launches a native picker dialog.'
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
                  onClick: () => handleAction(action).catch(() => {}),
                  disabled: pending && pending !== action.key
                },
                pending === action.key ? 'Working…' : action.label
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Picker APIs'
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
                h(InlineCode, null, 'Window.showOpenFilePicker'),
                ' for multi-file selection.'
              ),
              h(
                'li',
                null,
                h(InlineCode, null, 'Window.showSaveFilePicker'),
                ' prompts for a save location.'
              ),
              h(
                'li',
                null,
                h(InlineCode, null, 'Window.showDirectoryFilePicker'),
                ' exposes directory contents.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Picker results and errors.',
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
            emptyState: 'Launch a picker to capture results.'
          })
        )
      )
    )
  )
}

mountExample(WindowFilePickersDemo)
