import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleStack,
  ExampleGrid,
  ScrollArea,
  Button,
  StatusPill
} from '../ui/index.js'

const {
  createElement: h,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState
} = React

function KitchenSinkApp () {
  const [logLines, setLogLines] = useState([])
  const [aotStatus, setAotStatus] = useState('unknown')
  const [isMonitorRunning, setIsMonitorRunning] = useState(false)
  const [secondaryStatus, setSecondaryStatus] = useState(
    'Secondary window closed'
  )
  const [secondaryOpen, setSecondaryOpen] = useState(false)

  const logRef = useRef(null)
  const aotTimerRef = useRef(null)
  const secondaryIndexRef = useRef(2)
  const secondaryOpenRef = useRef(false)

  const appendLog = useCallback((message) => {
    const ts = new Date().toISOString().slice(11, 23)
    const line = `[${ts}] ${message}`
    setLogLines((prev) => {
      const next = [...prev, line]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  useEffect(() => {
    if (!logRef.current) return
    logRef.current.scrollTop = logRef.current.scrollHeight
  }, [logLines])

  const updateAOTStatusOnce = useCallback(async () => {
    try {
      const application = (await import('oro:application')).default
      const win = await application.getCurrentWindow()
      const value = await win.isAlwaysOnTop()
      setAotStatus(value ? 'on' : 'off')
    } catch (err) {
      setAotStatus('error')
      appendLog(`AOT status error: ${err?.message || err}`)
    }
  }, [appendLog])

  const toggleAOTMonitor = useCallback(async () => {
    if (isMonitorRunning) {
      if (aotTimerRef.current) clearInterval(aotTimerRef.current)
      aotTimerRef.current = null
      setIsMonitorRunning(false)
      appendLog('AOT monitor stopped')
      return
    }

    await updateAOTStatusOnce()
    aotTimerRef.current = setInterval(updateAOTStatusOnce, 1000)
    setIsMonitorRunning(true)
    appendLog('AOT monitor started (1s)')
  }, [isMonitorRunning, updateAOTStatusOnce, appendLog])

  const enableLifecycleLogger = useCallback(async () => {
    await import('../lifecycle/log-events.js')
    appendLog('Lifecycle logger enabled')
  }, [appendLog])

  const enableFSWatchDemo = useCallback(async () => {
    await import('../lifecycle/fs-watch-recreate.js')
    appendLog('FS watch demo enabled (recreates watchers on resume)')
  }, [appendLog])

  const showNotification = useCallback(async () => {
    const mod = await import('../notifications/basic.js')
    await mod.demo()
    appendLog('Notification requested')
  }, [appendLog])

  const startGeolocation = useCallback(async () => {
    const mod = await import('../geolocation/watch.js')
    await mod.demo()
    appendLog('Geolocation watch started')
  }, [appendLog])

  const startMedia = useCallback(async () => {
    const mod = await import('../media/user-media.js')
    await mod.demo()
    appendLog('Media requested')
  }, [appendLog])

  const enableDeepLink = useCallback(async () => {
    await import('../application/deeplink.js')
    appendLog('Deep link handler enabled')
  }, [appendLog])

  const simulateDeepLink = useCallback(() => {
    try {
      const Ctor = globalThis.ApplicationURLEvent
      if (typeof Ctor !== 'function') {
        throw new Error('ApplicationURLEvent not available')
      }
      const url = 'oro://__BUNDLE__/open?file=/tmp/demo.txt'
      const ev = new Ctor('applicationurl', { url })
      globalThis.dispatchEvent(ev)
      appendLog(`Simulated deep link: ${url}`)
    } catch (err) {
      appendLog(`Deep link simulate error: ${err?.message || err}`)
    }
  }, [appendLog])

  const withApplicationWindow = useCallback(async (fn, options = {}) => {
    const application = (await import('oro:application')).default
    const win =
      options.index === undefined
        ? await application.getCurrentWindow()
        : await application.getWindow(options.index, { max: false })
    if (!win) throw new Error('Window not available')
    return fn(win, application)
  }, [])

  const focusWindow = useCallback(async () => {
    try {
      await withApplicationWindow(async (win) => {
        await win.focus()
        appendLog('Window focused')
      })
    } catch (err) {
      appendLog(`Window focus error: ${err?.message || err}`)
    }
  }, [withApplicationWindow, appendLog])

  const blurWindow = useCallback(async () => {
    try {
      await withApplicationWindow(async (win) => {
        await win.blur()
        appendLog('Window blurred')
      })
    } catch (err) {
      appendLog(`Window blur error: ${err?.message || err}`)
    }
  }, [withApplicationWindow, appendLog])

  const setAlwaysOnTop = useCallback(
    async (enabled) => {
      try {
        await withApplicationWindow(async (win) => {
          await win.setAlwaysOnTop(enabled)
          const value = await win.isAlwaysOnTop()
          setAotStatus(value ? 'on' : 'off')
          appendLog(`Always On Top: ${value}`)
        })
      } catch (err) {
        appendLog(`Always On Top error: ${err?.message || err}`)
      }
    },
    [withApplicationWindow, appendLog]
  )

  const showContextMenu = useCallback(async () => {
    try {
      await withApplicationWindow(async (win) => {
        await win.setContextMenu({
          value: 'Menu:\n  ---\n  Ping: p;\n  Pong: o;\n  ---\n  Quit: q;'
        })
        appendLog('Context menu shown')
      })
    } catch (err) {
      appendLog(`Context menu error: ${err?.message || err}`)
    }
  }, [withApplicationWindow, appendLog])

  const lifecycleShowMode = useCallback(async () => {
    const application = (await import('oro:application')).default
    const process = (await import('oro:process')).default
    const value = String(
      application.config?.lifecycle_desktop_always_running ?? 'true'
    )
    const platform = process.platform
    const realPauseEnabled = value === 'false'
    appendLog(`platform=${platform}`)
    appendLog(`lifecycle_desktop_always_running = ${value}`)
    appendLog(`native pause/resume enabled = ${realPauseEnabled}`)
  }, [appendLog])

  const lifecycleHowTo = useCallback(() => {
    appendLog('Desktop lifecycle mode toggles:')
    appendLog(
      ' - Default is always-running (emit-only); no native pause/resume.'
    )
    appendLog(' - To enable native pause/resume on desktop, set in oro.toml:')
    appendLog('     lifecycle_desktop_always_running = false')
    appendLog(' - See examples:')
    appendLog('     examples/lifecycle/oro.ini.always-running')
    appendLog('     examples/lifecycle/oro.ini.real-pause')
  }, [appendLog])

  const dispatchLifecycle = useCallback(
    (type) => {
      globalThis.dispatchEvent(new Event(type))
      appendLog(`Dispatched ${type}`)
    },
    [appendLog]
  )

  const pauseLifecycle = useCallback(
    () => dispatchLifecycle('applicationpause'),
    [dispatchLifecycle]
  )
  const resumeLifecycle = useCallback(
    () => dispatchLifecycle('applicationresume'),
    [dispatchLifecycle]
  )
  const stopLifecycle = useCallback(
    () => dispatchLifecycle('applicationstop'),
    [dispatchLifecycle]
  )

  const openSecondaryWindow = useCallback(async () => {
    const application = (await import('oro:application')).default
    try {
      const win = await application.createWindow({
        index: secondaryIndexRef.current,
        path: 'examples/window/secondary.html',
        title: 'Secondary'
      })
      secondaryOpenRef.current = true
      setSecondaryOpen(true)
      setSecondaryStatus(`Secondary window open (index ${win.index})`)
      appendLog('Secondary window opened')
    } catch (err) {
      secondaryOpenRef.current = true
      setSecondaryOpen(true)
      setSecondaryStatus(
        `Secondary window open (index ${secondaryIndexRef.current})`
      )
      appendLog(
        `Secondary open error (may already exist): ${err?.message || err}`
      )
    }
  }, [appendLog])

  const closeSecondaryWindow = useCallback(
    async ({ silent = false } = {}) => {
      const application = (await import('oro:application')).default
      const win = await application.getWindow(secondaryIndexRef.current, {
        max: false
      })
      if (win) {
        await win.close()
        secondaryOpenRef.current = false
        setSecondaryOpen(false)
        setSecondaryStatus('Secondary window closed')
        if (!silent) appendLog('Secondary window closed')
      } else if (!silent) {
        appendLog('Secondary window not found')
      }
    },
    [appendLog]
  )

  useEffect(() => {
    return () => {
      if (aotTimerRef.current) clearInterval(aotTimerRef.current)
      if (secondaryOpenRef.current) {
        closeSecondaryWindow({ silent: true }).catch(() => {})
      }
    }
  }, [closeSecondaryWindow])

  const focusSecondary = useCallback(async () => {
    try {
      await withApplicationWindow(
        async (win) => {
          await win.focus()
          appendLog('Secondary window focused')
        },
        { index: secondaryIndexRef.current }
      )
    } catch (err) {
      appendLog(`Secondary focus error: ${err?.message || err}`)
    }
  }, [withApplicationWindow, appendLog])

  const blurSecondary = useCallback(async () => {
    try {
      await withApplicationWindow(
        async (win) => {
          await win.blur()
          appendLog('Secondary window blurred')
        },
        { index: secondaryIndexRef.current }
      )
    } catch (err) {
      appendLog(`Secondary blur error: ${err?.message || err}`)
    }
  }, [withApplicationWindow, appendLog])

  const secondaryAOT = useCallback(
    async (enabled) => {
      try {
        await withApplicationWindow(
          async (win) => {
            try {
              await win.setAlwaysOnTop(enabled)
              const value = await win.isAlwaysOnTop()
              appendLog(`Secondary AOT: ${value}`)
            } catch (err) {
              appendLog(`Secondary AOT error: ${err?.message || err}`)
            }
          },
          { index: secondaryIndexRef.current }
        )
      } catch (err) {
        appendLog(`Secondary window not available: ${err?.message || err}`)
      }
    },
    [withApplicationWindow, appendLog]
  )

  const secondaryContextMenu = useCallback(async () => {
    try {
      await withApplicationWindow(
        async (win) => {
          try {
            await win.setContextMenu({
              value: 'Menu:\n  Hello: h;\n  World: w;'
            })
            appendLog('Secondary context menu shown')
          } catch (err) {
            appendLog(`Secondary context menu error: ${err?.message || err}`)
          }
        },
        { index: secondaryIndexRef.current }
      )
    } catch (err) {
      appendLog(`Secondary window not available: ${err?.message || err}`)
    }
  }, [withApplicationWindow, appendLog])

  const controlGroups = useMemo(
    () => [
      {
        key: 'lifecycle',
        title: 'Lifecycle',
        description: 'Emit lifecycle events and enable logging.',
        actions: [
          { label: 'Enable Logger', handler: enableLifecycleLogger },
          { label: 'Pause', handler: pauseLifecycle },
          { label: 'Resume', handler: resumeLifecycle },
          { label: 'Stop', handler: stopLifecycle }
        ]
      },
      {
        key: 'desktop-mode',
        title: 'Desktop Lifecycle Mode',
        description: 'Inspect the current desktop lifecycle configuration.',
        actions: [
          { label: 'Show Mode', handler: lifecycleShowMode },
          { label: 'How to Toggle', handler: lifecycleHowTo }
        ]
      },
      {
        key: 'fs',
        title: 'Filesystem Watch',
        description: 'Demonstrate recreating FS watchers after resume.',
        actions: [{ label: 'Enable Demo', handler: enableFSWatchDemo }]
      },
      {
        key: 'notifications',
        title: 'Notifications',
        description: 'Trigger a native notification.',
        actions: [{ label: 'Show Notification', handler: showNotification }]
      },
      {
        key: 'geo',
        title: 'Geolocation',
        description: 'Watch continuous location updates.',
        actions: [
          { label: 'Start Watch', handler: startGeolocation },
          { label: 'Pause (simulate)', handler: pauseLifecycle },
          { label: 'Resume (simulate)', handler: resumeLifecycle }
        ]
      },
      {
        key: 'media',
        title: 'Media Devices',
        description: 'Request microphone and camera access.',
        actions: [
          { label: 'Start', handler: startMedia },
          { label: 'Pause (simulate)', handler: pauseLifecycle },
          { label: 'Resume (simulate)', handler: resumeLifecycle }
        ]
      },
      {
        key: 'deeplink',
        title: 'Application URL',
        description: 'Handle deep links routed into the app.',
        actions: [
          { label: 'Enable Handler', handler: enableDeepLink },
          { label: 'Simulate Deep Link', handler: simulateDeepLink }
        ]
      },
      {
        key: 'window',
        title: 'Window Management',
        description: 'Control the primary window.',
        actions: [
          { label: 'Focus', handler: focusWindow },
          { label: 'Blur', handler: blurWindow },
          { label: 'AOT On', handler: () => setAlwaysOnTop(true) },
          { label: 'AOT Off', handler: () => setAlwaysOnTop(false) },
          { label: 'Context Menu', handler: showContextMenu },
          {
            label: isMonitorRunning ? 'Stop AOT Monitor' : 'Start AOT Monitor',
            handler: toggleAOTMonitor
          }
        ]
      },
      {
        key: 'secondary',
        title: 'Secondary Window',
        description: 'Create and control a secondary window.',
        actions: [
          { label: 'Open', handler: openSecondaryWindow },
          { label: 'Close', handler: () => closeSecondaryWindow() },
          { label: 'Focus', handler: focusSecondary },
          { label: 'Blur', handler: blurSecondary },
          { label: 'AOT On', handler: () => secondaryAOT(true) },
          { label: 'AOT Off', handler: () => secondaryAOT(false) },
          { label: 'Context Menu', handler: secondaryContextMenu }
        ]
      }
    ],
    [
      enableLifecycleLogger,
      pauseLifecycle,
      resumeLifecycle,
      stopLifecycle,
      lifecycleShowMode,
      lifecycleHowTo,
      enableFSWatchDemo,
      showNotification,
      startGeolocation,
      startMedia,
      enableDeepLink,
      simulateDeepLink,
      focusWindow,
      blurWindow,
      setAlwaysOnTop,
      showContextMenu,
      isMonitorRunning,
      toggleAOTMonitor,
      openSecondaryWindow,
      closeSecondaryWindow,
      focusSecondary,
      blurSecondary,
      secondaryAOT,
      secondaryContextMenu
    ]
  )

  const statusBadgeVariant =
    aotStatus === 'on' ? 'success' : aotStatus === 'off' ? 'neutral' : 'warning'

  const aside = h(
    ExampleStack,
    { gap: 'md' },
    h(
      ExamplePanel,
      { title: 'Runtime Status' },
      h(
        'div',
        { className: 'kitchen-sink__status-list' },
        h(
          'div',
          { className: 'kitchen-sink__status-item' },
          h(
            'span',
            { className: 'kitchen-sink__status-label' },
            'Always On Top'
          ),
          h(
            StatusPill,
            {
              status:
                aotStatus === 'on'
                  ? 'running'
                  : aotStatus === 'off'
                    ? 'idle'
                    : 'error'
            },
            aotStatus
          )
        ),
        h(
          'div',
          { className: 'kitchen-sink__status-item' },
          h(
            'span',
            { className: 'kitchen-sink__status-label' },
            'Secondary Window'
          ),
          h(
            StatusPill,
            { status: secondaryOpen ? 'running' : 'idle' },
            secondaryOpen ? 'Open' : 'Closed'
          )
        ),
        h(
          'div',
          { className: 'kitchen-sink__status-item' },
          h('span', { className: 'kitchen-sink__status-label' }, 'Details'),
          h(
            'span',
            { className: 'kitchen-sink__status-value' },
            secondaryStatus
          )
        )
      ),
      h(
        'div',
        { className: 'kitchen-sink__aside-actions' },
        h(
          Button,
          {
            variant: isMonitorRunning ? 'destructive' : 'secondary',
            onClick: toggleAOTMonitor
          },
          isMonitorRunning ? 'Stop AOT Monitor' : 'Start AOT Monitor'
        ),
        h(
          Button,
          {
            variant: 'outline',
            onClick: updateAOTStatusOnce,
            disabled: isMonitorRunning
          },
          'Refresh AOT status'
        ),
        h(
          Button,
          {
            variant: 'secondary',
            onClick: () => closeSecondaryWindow()
          },
          'Close Secondary'
        )
      )
    )
  )

  return h(
    ExampleLayout,
    {
      title: 'Kitchen Sink',
      description:
        'A grab bag of runtime capabilities that you can toggle on demand. Each control imports the relevant example lazily.',
      badge: h(StatusPill, { status: statusBadgeVariant }, `AOT: ${aotStatus}`),
      aside
    },
    h(
      ExampleStack,
      { gap: 'lg' },
      h(
        ExampleGrid,
        { columns: 2 },
        controlGroups.map((group) =>
          h(
            ExamplePanel,
            {
              key: group.key,
              title: group.title,
              description: group.description
            },
            h(
              'div',
              { className: 'kitchen-sink__actions' },
              group.actions.map((action, index) =>
                h(
                  Button,
                  {
                    key: `${group.key}-${index}`,
                    onClick: action.handler,
                    size: 'sm'
                  },
                  action.label
                )
              )
            )
          )
        )
      ),
      h(
        ExamplePanel,
        { title: 'Log', description: 'Recent activity across the demos.' },
        h(
          ScrollArea,
          { className: 'kitchen-sink__log', ref: logRef },
          logLines.length
            ? logLines.map((line, index) =>
              h(
                'div',
                {
                  key: `${index}-${line}`,
                  className: 'kitchen-sink__log-line'
                },
                line
              )
            )
            : h(
              'div',
              { className: 'kitchen-sink__log-line' },
              '[ready] Toggle a demo to begin logging.'
            )
        )
      )
    )
  )
}

mountExample(KitchenSinkApp)
