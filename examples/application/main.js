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

import { createApplicationUrlListener } from './deeplink.js'

const { createElement: h, useState, useMemo, useEffect, useCallback } = React

function ApplicationUrlApp () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Listening for deep links',
    message:
      'Use platform-specific mechanisms to route URLs into the application.'
  })
  const [logs, setLogs] = useState([])
  const [lastLink, setLastLink] = useState(null)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  useEffect(() => {
    const listener = createApplicationUrlListener({
      log: (message) => appendLog(message),
      onLink: ({ url, parsed }) => {
        setLastLink({ url, parsed })
        setStatus({
          variant: 'success',
          title: 'Deep link received',
          message: url || 'Unknown URL'
        })
      }
    })
    return () => listener.dispose?.()
  }, [appendLog])

  const simulateLink = useCallback(() => {
    try {
      const Ctor = globalThis.ApplicationURLEvent
      if (typeof Ctor !== 'function') {
        throw new Error('ApplicationURLEvent not supported in this context')
      }
      const url = 'oro://example/open?file=/tmp/demo.txt'
      const event = new Ctor('applicationurl', { url })
      globalThis.dispatchEvent(event)
      appendLog(`simulated deep link -> ${url}`)
    } catch (error) {
      const message = error?.message || String(error)
      setStatus({
        variant: 'danger',
        title: 'Simulation failed',
        message
      })
      appendLog(`simulation failed: ${message}`)
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
      title: 'Application Deep Links',
      description:
        'Observe URL routing events delivered via oro:hooks.onApplicationURL.'
    },
    h(
      ExamplePanel,
      {
        title: 'Deep link monitor',
        description: 'Incoming URLs are parsed and shown below.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
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
                onClick: () => simulateLink()
              },
              'Simulate deep link'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Latest link'
          },
          lastLink
            ? h(
              ExampleProse,
              null,
              h('p', null, 'URL: ', h(InlineCode, null, lastLink.url || '—')),
              lastLink.parsed &&
                  h(
                    'p',
                    null,
                    'Path: ',
                    h(InlineCode, null, lastLink.parsed.pathname)
                  ),
              lastLink.parsed &&
                  h(
                    'p',
                    null,
                    'Query: ',
                    h(InlineCode, null, lastLink.parsed.search)
                  )
            )
            : h('p', { className: 'ui-prose' }, 'No deep links received yet.')
        ),
        h(
          ExampleSection,
          {
            title: 'Integration notes'
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
                'Register protocol handlers in ',
                h(InlineCode, null, 'oro.toml'),
                ' or platform manifests.'
              ),
              h(
                'li',
                null,
                'Use ',
                h(InlineCode, null, 'onApplicationURL'),
                ' to handle incoming deep link events.'
              ),
              h(
                'li',
                null,
                'Dispatch ',
                h(InlineCode, null, 'ApplicationURLEvent'),
                ' for local simulation and testing.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Event log',
            description: 'Detailed deep link event history.',
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
            emptyState: 'Waiting for deep link activity.'
          })
        )
      )
    )
  )
}

mountExample(ApplicationUrlApp)
