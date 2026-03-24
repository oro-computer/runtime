import * as ipfs from 'oro:ipfs'
import fs from 'oro:fs/promises'
import path from 'oro:path'
import { homedir } from 'oro:os'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleGrid,
  ExamplePanel,
  ExampleStack,
  StatusBanner,
  FormField,
  Input,
  Button,
  Switch,
  Badge,
  InlineCode,
  LogViewer
} from '../ui/index.js'

const { createElement: h, useCallback, useEffect, useMemo, useState } = React

const DEFAULT_PORT = 45005
const FALLBACK_CWD = (() => {
  try {
    const proc = globalThis.process
    if (proc && typeof proc.cwd === 'function') {
      return proc.cwd()
    }
  } catch {}
  return '.'
})()

const DEFAULT_BASE = (() => {
  const home = homedir()
  if (home && typeof home === 'string' && home.length > 0) {
    return path.join(home, '.socket-ipfs-example')
  }
  return path.join(FALLBACK_CWD, '.socket-ipfs-example')
})()

const DEFAULT_REPO_PATH = path.join(DEFAULT_BASE, 'repo')

function deriveWorkspaceRoot (repoPath) {
  const parent = repoPath ? path.dirname(repoPath) : DEFAULT_BASE
  return path.join(parent, 'ipfs-workspace')
}

function toIpfsPath (input) {
  if (!input) return ''
  return input.startsWith('/ipfs/') ? input : `/ipfs/${input}`
}

function stripIpfsPrefix (input) {
  if (!input) return ''
  return input.startsWith('/ipfs/') ? input.slice('/ipfs/'.length) : input
}

function formatError (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error instanceof Error) {
    if (error.code) return `${error.message} (code: ${error.code})`
    return error.message
  }
  if (typeof error.message === 'string') return error.message
  try {
    return JSON.stringify(error)
  } catch {
    return String(error)
  }
}

function createLogEntry (message, details = null, level = 'info') {
  const now = new Date()
  return {
    id: `${now.getTime().toString(36)}-${Math.random().toString(16).slice(2)}`,
    time: now.toISOString(),
    level,
    message,
    details
  }
}

function IpfsWorkbench () {
  const [status, setStatus] = useState({
    available: false,
    started: false,
    repoPath: DEFAULT_REPO_PATH,
    port: DEFAULT_PORT,
    peerId: ''
  })

  const [repoPath, setRepoPath] = useState(DEFAULT_REPO_PATH)
  const [repoPathDirty, setRepoPathDirty] = useState(false)

  const [portInput, setPortInput] = useState(String(DEFAULT_PORT))
  const [portDirty, setPortDirty] = useState(false)

  const [workspaceRoot, setWorkspaceRoot] = useState(() =>
    deriveWorkspaceRoot(DEFAULT_REPO_PATH)
  )

  const [cidInput, setCidInput] = useState('')
  const [cidHistory, setCidHistory] = useState([])

  const [downloadPathDirty, setDownloadPathDirty] = useState(false)
  const [downloadPath, setDownloadPath] = useState(() =>
    path.join(
      deriveWorkspaceRoot(DEFAULT_REPO_PATH),
      'downloads',
      'payload.txt'
    )
  )

  const [pinOnFetch, setPinOnFetch] = useState(true)

  const [logs, setLogs] = useState([])
  const [busyAction, setBusyAction] = useState(null)

  const isBusy = busyAction !== null

  useEffect(() => {
    setWorkspaceRoot(deriveWorkspaceRoot(repoPath))
  }, [repoPath])

  useEffect(() => {
    if (!downloadPathDirty) {
      setDownloadPath(path.join(workspaceRoot, 'downloads', 'payload.txt'))
    }
  }, [workspaceRoot, downloadPathDirty])

  const banner = useMemo(() => {
    if (!status.available) {
      return {
        variant: 'warning',
        title: 'IPFS runtime unavailable',
        message:
          'The libipfs native dependency is not linked. Re-run the installer with libipfs support or set ORO_SKIP_LIBIPFS=0.'
      }
    }

    if (status.started) {
      return {
        variant: 'success',
        title: 'Node running',
        message: `Listening on port ${status.port}.`,
        prefix: status.peerId ? 'Peer' : null
      }
    }

    return {
      variant: 'neutral',
      title: 'Node stopped',
      message: 'Start the embedded daemon to add and fetch content.'
    }
  }, [status])

  const appendLog = useCallback((message, details = null, level = 'info') => {
    setLogs((current) => {
      const next = [
        ...current.slice(-99),
        createLogEntry(message, details, level)
      ]
      return next
    })
  }, [])

  const refreshStatus = useCallback(
    async (silent = false) => {
      try {
        const info = await ipfs.status()
        setStatus((prev) => ({
          ...prev,
          ...info
        }))
        if (!repoPathDirty && info.repoPath) {
          setRepoPath(info.repoPath)
        }
        if (!portDirty && info.port) {
          setPortInput(String(info.port))
        }
        if (!silent) {
          appendLog('Refreshed node status', {
            available: info.available,
            started: info.started,
            repoPath: info.repoPath,
            port: info.port,
            peerId: info.peerId
          })
        }
      } catch (error) {
        const message = formatError(error)
        setStatus((prev) => ({
          ...prev,
          available: false,
          started: false,
          peerId: ''
        }))
        if (!silent) {
          appendLog('Failed to refresh status', message, 'error')
        }
      }
    },
    [appendLog, portDirty, repoPathDirty]
  )

  useEffect(() => {
    refreshStatus(true).catch((error) =>
      appendLog('Initial status check failed', formatError(error), 'error')
    )
  }, [appendLog, refreshStatus])

  const runAction = useCallback(
    async (label, task) => {
      setBusyAction(label)
      try {
        await task()
      } catch (error) {
        appendLog(`${label} failed`, formatError(error), 'error')
        throw error
      } finally {
        setBusyAction(null)
      }
    },
    [appendLog]
  )

  const ensureDirectories = useCallback(async (dirs) => {
    for (const dir of dirs) {
      await fs.mkdir(dir, { recursive: true })
    }
  }, [])

  const handleStart = useCallback(async () => {
    await runAction('Start node', async () => {
      const portNumber = Number(portInput)
      if (
        !Number.isInteger(portNumber) ||
        portNumber <= 0 ||
        portNumber > 65535
      ) {
        throw new Error('Port must be an integer between 1 and 65535.')
      }

      const repoDir =
        repoPath.trim().length > 0 ? repoPath.trim() : DEFAULT_REPO_PATH
      const workspaceDir = deriveWorkspaceRoot(repoDir)

      await ensureDirectories([repoDir, workspaceDir])

      const result = await ipfs.start({ repoPath: repoDir, port: portNumber })
      appendLog('Started IPFS node', {
        repoPath: repoDir,
        port: portNumber,
        peerId: result?.peerId || ''
      })

      setRepoPath(repoDir)
      setStatus({
        available: true,
        started: true,
        repoPath: repoDir,
        port: portNumber,
        peerId: result?.peerId || ''
      })

      if (result?.peerId) {
        setCidHistory((current) => current)
      }
    })
  }, [appendLog, ensureDirectories, portInput, repoPath, runAction])

  const handleStop = useCallback(async () => {
    await runAction('Stop node', async () => {
      await ipfs.stop()
      appendLog('Stopped IPFS node')
      setStatus((prev) => ({
        ...prev,
        started: false,
        peerId: ''
      }))
    })
  }, [appendLog, runAction])

  const handleAddSample = useCallback(async () => {
    await runAction('Add sample file', async () => {
      if (!status.started) {
        throw new Error('Start the node before adding content.')
      }

      const sampleDir = path.join(workspaceRoot, 'samples')
      const timestamp = new Date().toISOString().replace(/[:.]/g, '-')
      const fileName = `greeting-${timestamp}.txt`
      const samplePath = path.join(sampleDir, fileName)
      const payload = [
        'Oro Runtime · IPFS example',
        `Created at ${new Date().toISOString()}`,
        '',
        'This content demonstrates pushing local data through the embedded libipfs node.'
      ].join('\n')

      await ensureDirectories([sampleDir])
      await fs.writeFile(samplePath, payload, 'utf8')

      const { cid } = await ipfs.add(samplePath)
      const bareCid = stripIpfsPrefix(cid)

      appendLog('Added sample file', {
        cid: bareCid,
        path: samplePath
      })

      setCidHistory((current) =>
        [
          {
            cid: bareCid,
            path: samplePath,
            pinned: false,
            createdAt: new Date().toISOString()
          },
          ...current
        ].slice(0, 10)
      )
      setCidInput(bareCid)

      if (!downloadPathDirty) {
        setDownloadPath(path.join(workspaceRoot, 'downloads', `${bareCid}.txt`))
      }
    })
  }, [
    downloadPathDirty,
    ensureDirectories,
    runAction,
    status.started,
    workspaceRoot
  ])

  const handleFetch = useCallback(async () => {
    await runAction('Fetch content', async () => {
      if (!status.started) {
        throw new Error('Start the node before fetching content.')
      }

      const cidValue = cidInput.trim()
      if (!cidValue) {
        throw new Error('Enter a CID to fetch.')
      }

      const destinationPath = downloadPath.trim()
      if (!destinationPath) {
        throw new Error('Provide a destination path for the fetched data.')
      }

      const destinationDir = path.dirname(destinationPath)
      await ensureDirectories([destinationDir])
      await fs
        .rm(destinationPath, { recursive: true, force: true })
        .catch(() => {})

      const ipfsPath = toIpfsPath(cidValue)
      const result = await ipfs.get(ipfsPath, {
        destination: destinationPath,
        pin: Boolean(pinOnFetch)
      })

      appendLog('Fetched CID', {
        cid: stripIpfsPrefix(result?.cid || cidValue),
        destination: result?.path || destinationPath,
        pinned: Boolean(result?.pinned || pinOnFetch)
      })
    })
  }, [
    appendLog,
    cidInput,
    downloadPath,
    ensureDirectories,
    pinOnFetch,
    runAction,
    status.started
  ])

  const handlePin = useCallback(
    async (desired) => {
      await runAction(desired ? 'Pin CID' : 'Unpin CID', async () => {
        if (!status.started) {
          throw new Error('Start the node before managing pins.')
        }
        const cidValue = cidInput.trim()
        if (!cidValue) {
          throw new Error('Enter a CID to pin or unpin.')
        }

        const ipfsPath = toIpfsPath(cidValue)
        const response = desired
          ? await ipfs.pin(ipfsPath)
          : await ipfs.unpin(ipfsPath)

        appendLog(desired ? 'Pinned CID' : 'Unpinned CID', {
          cid: stripIpfsPrefix(response?.cid || cidValue)
        })

        setCidHistory((current) =>
          current.map((entry) => {
            if (entry.cid === stripIpfsPrefix(cidValue)) {
              return { ...entry, pinned: desired }
            }
            return entry
          })
        )
      })
    },
    [cidInput, runAction, status.started, appendLog]
  )

  const handleGarbageCollect = useCallback(async () => {
    await runAction('Garbage collect', async () => {
      if (!status.started) {
        throw new Error('Start the node before running garbage collection.')
      }
      await ipfs.garbageCollect()
      appendLog('Triggered repository garbage collection')
    })
  }, [appendLog, runAction, status.started])

  const handlePeerId = useCallback(async () => {
    await runAction('Fetch peer ID', async () => {
      if (!status.started) {
        throw new Error('Start the node before querying the peer ID.')
      }
      const peer = await ipfs.peerId()
      appendLog('Current peer ID', { peerId: peer })
      setStatus((prev) => ({ ...prev, peerId: peer || '' }))
    })
  }, [appendLog, runAction, status.started])

  const renderLogEntry = useCallback((entry) => {
    const time = new Date(entry.time)
    const formattedTime = Number.isNaN(time.getTime())
      ? ''
      : time.toLocaleTimeString()
    return h(
      'div',
      { className: 'ipfs-example__log-entry' },
      h(
        'div',
        { className: 'ipfs-example__log-title' },
        formattedTime ? `[${formattedTime}] ` : '',
        entry.message
      ),
      entry.details
        ? h(
          'pre',
          { className: 'ipfs-example__log-details' },
          typeof entry.details === 'string'
            ? entry.details
            : JSON.stringify(entry.details, null, 2)
        )
        : null
    )
  }, [])

  return h(
    ExampleLayout,
    {
      title: 'IPFS Workbench',
      description:
        'Start the embedded libipfs node, add local files, and retrieve them through the runtime-managed API.',
      badge: h(
        Badge,
        {
          variant: status.started
            ? 'success'
            : status.available
              ? 'neutral'
              : 'warning'
        },
        status.started ? 'Running' : status.available ? 'Ready' : 'Unavailable'
      )
    },
    h(StatusBanner, banner),
    h(
      ExampleGrid,
      { columns: 2 },
      h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Node lifecycle',
            description:
              'Configure repository settings and control the embedded daemon.',
            actions: h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                onClick: () => refreshStatus(false),
                disabled: isBusy
              },
              'Refresh status'
            )
          },
          h(
            ExampleStack,
            { gap: 'md' },
            h(
              FormField,
              {
                label: 'Repository path',
                description: 'A writable directory for the libipfs datastore.'
              },
              h(Input, {
                value: repoPath,
                onChange: (event) => {
                  setRepoPath(event.target.value)
                  setRepoPathDirty(true)
                },
                spellCheck: false
              })
            ),
            h(
              FormField,
              {
                label: 'Swarm port',
                description: 'Libp2p will listen on this port for peers.',
                htmlFor: 'ipfs-port'
              },
              h(Input, {
                id: 'ipfs-port',
                value: portInput,
                inputMode: 'numeric',
                onChange: (event) => {
                  setPortInput(event.target.value)
                  setPortDirty(true)
                }
              })
            ),
            h(
              'div',
              { className: 'ipfs-example__actions' },
              h(
                Button,
                {
                  type: 'button',
                  onClick: handleStart,
                  disabled: isBusy || status.started
                },
                status.started ? 'Running' : isBusy ? 'Working…' : 'Start node'
              ),
              h(
                Button,
                {
                  type: 'button',
                  variant: 'secondary',
                  onClick: handleStop,
                  disabled: isBusy || !status.started
                },
                'Stop node'
              )
            ),
            h(
              'dl',
              { className: 'ipfs-example__status' },
              h(
                'div',
                null,
                h('dt', null, 'Available'),
                h('dd', null, status.available ? 'Yes' : 'No')
              ),
              h(
                'div',
                null,
                h('dt', null, 'Started'),
                h('dd', null, status.started ? 'Yes' : 'No')
              ),
              h(
                'div',
                null,
                h('dt', null, 'Peer ID'),
                h(
                  'dd',
                  { className: 'ipfs-example__mono' },
                  status.peerId || '—'
                )
              )
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Add sample content',
            description:
              'Write a demo file into a workspace directory and publish it to the local IPFS node.'
          },
          h(
            ExampleStack,
            { gap: 'md' },
            h(
              'p',
              { className: 'ipfs-example__prose' },
              'Sample files are stored under ',
              h(InlineCode, null, workspaceRoot),
              ' so you can inspect them after publishing.'
            ),
            h(
              Button,
              {
                type: 'button',
                onClick: handleAddSample,
                disabled: isBusy || !status.started
              },
              'Add sample file'
            ),
            cidHistory.length > 0
              ? h(
                'div',
                { className: 'ipfs-example__history' },
                h('h4', null, 'Recent CIDs'),
                h(
                  'ul',
                  null,
                  cidHistory.map((entry) =>
                    h(
                      'li',
                      { key: `${entry.cid}-${entry.createdAt}` },
                      h(
                        'div',
                        { className: 'ipfs-example__history-main' },
                        h(
                          'span',
                          { className: 'ipfs-example__mono' },
                          entry.cid
                        ),
                        entry.pinned &&
                            h(Badge, { variant: 'info' }, 'Pinned')
                      ),
                      h(
                        'div',
                        { className: 'ipfs-example__history-path' },
                        entry.path
                      )
                    )
                  )
                )
              )
              : h(
                'p',
                { className: 'ipfs-example__muted' },
                'Publish a sample to populate the history.'
              )
          )
        )
      ),
      h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Fetch & pin content',
            description:
              'Retrieve CIDs through the runtime and optionally pin them locally.'
          },
          h(
            ExampleStack,
            { gap: 'md' },
            h(
              FormField,
              {
                label: 'Target CID',
                description:
                  'Paste a CID or IPFS path. Bare CIDs are automatically prefixed with /ipfs/.'
              },
              h(Input, {
                value: cidInput,
                onChange: (event) => setCidInput(event.target.value.trim()),
                spellCheck: false,
                placeholder: 'bafy...'
              })
            ),
            h(
              FormField,
              {
                label: 'Destination path',
                description: 'Where the fetched payload will be written.'
              },
              h(Input, {
                value: downloadPath,
                spellCheck: false,
                onChange: (event) => {
                  setDownloadPath(event.target.value)
                  setDownloadPathDirty(true)
                }
              })
            ),
            h(
              'div',
              { className: 'ipfs-example__toggle' },
              h(Switch, {
                checked: pinOnFetch,
                onCheckedChange: setPinOnFetch,
                label: 'Pin fetched content'
              }),
              h('span', null, 'Pin after fetch')
            ),
            h(
              'div',
              { className: 'ipfs-example__actions' },
              h(
                Button,
                {
                  type: 'button',
                  onClick: handleFetch,
                  disabled: isBusy || !status.started
                },
                'Fetch CID'
              ),
              h(
                Button,
                {
                  type: 'button',
                  variant: 'secondary',
                  onClick: () => handlePin(true),
                  disabled: isBusy || !status.started
                },
                'Pin CID'
              ),
              h(
                Button,
                {
                  type: 'button',
                  variant: 'ghost',
                  onClick: () => handlePin(false),
                  disabled: isBusy || !status.started
                },
                'Unpin CID'
              )
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Maintenance',
            description:
              'Inspect runtime state and run repository maintenance commands.'
          },
          h(
            ExampleStack,
            { gap: 'md' },
            h(
              'div',
              { className: 'ipfs-example__buttons' },
              h(
                Button,
                {
                  type: 'button',
                  onClick: handlePeerId,
                  disabled: isBusy || !status.started
                },
                'Show peer ID'
              ),
              h(
                Button,
                {
                  type: 'button',
                  variant: 'secondary',
                  onClick: handleGarbageCollect,
                  disabled: isBusy || !status.started
                },
                'Garbage collect'
              )
            ),
            h(
              'p',
              { className: 'ipfs-example__muted' },
              'Operations stream through the new ',
              h(InlineCode, null, 'oro:ipfs'),
              ' module.'
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Activity log',
            description: 'Track completed operations and errors.'
          },
          h(LogViewer, {
            entries: logs,
            renderEntry: renderLogEntry,
            emptyState: () => h('span', null, 'No actions executed yet.')
          })
        )
      )
    )
  )
}

mountExample(IpfsWorkbench)
