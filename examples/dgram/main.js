import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleSection,
  ExampleStack,
  ExampleProse,
  Button,
  Input,
  InlineCode,
  StatusBanner,
  LogViewer
} from '../ui/index.js'

import { runMulticastEcho } from './multicast-echo.js'

const { createElement: h, useState, useMemo, useCallback } = React

function DgramApp () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Ready',
    message: 'Send a multicast packet and wait for the loopback echo.'
  })
  const [logs, setLogs] = useState([])
  const [message, setMessage] = useState('hello multicast')
  const [running, setRunning] = useState(false)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const runExample = useCallback(async () => {
    setRunning(true)
    setStatus({
      variant: 'info',
      title: 'Sending packet…',
      message: `Dispatching multicast message: ${message}`
    })
    try {
      await runMulticastEcho({
        message,
        log: (line) => appendLog(line)
      })
      setStatus({
        variant: 'success',
        title: 'Echo received',
        message: 'Multicast loopback succeeded.'
      })
    } catch (error) {
      const errored = error?.message || String(error)
      appendLog(`error -> ${errored}`)
      setStatus({
        variant: 'danger',
        title: 'Multicast failed',
        message: errored
      })
    } finally {
      setRunning(false)
    }
  }, [appendLog, message])

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
      title: 'UDP Multicast Echo',
      description:
        'Send a UDP multicast datagram and verify local loopback using oro:dgram.'
    },
    h(
      ExamplePanel,
      {
        title: 'Multicast controller',
        description: 'Configure the payload and launch the echo round trip.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Payload'
          },
          h(Input, {
            value: message,
            onChange: (event) => setMessage(event.target.value),
            placeholder: 'Message to send',
            disabled: running
          })
        ),
        h(
          ExampleSection,
          {
            title: 'Actions'
          },
          h(
            Button,
            {
              type: 'button',
              onClick: () => runExample().catch(() => {}),
              disabled: running
            },
            running ? 'Running…' : 'Send multicast'
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
                'Checks multicast support via ',
                h(InlineCode, null, 'dgram.getCapabilities()'),
                '.'
              ),
              h(
                'li',
                null,
                'Binds a UDP socket with loopback enabled, then joins ',
                h(InlineCode, null, '239.255.0.123:45001'),
                '.'
              ),
              h(
                'li',
                null,
                'Sends a datagram and waits for the echoed payload on the same socket.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Socket operations and received payloads.',
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
            emptyState: 'Trigger the multicast flow to see logs.'
          })
        )
      )
    )
  )
}

mountExample(DgramApp)
