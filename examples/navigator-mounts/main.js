import fs from 'oro:fs/promises'
import os from 'oro:os'
import path from 'oro:path'

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
  StatusPill,
  EmptyState,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  Code,
  LogViewer
} from '../ui/index.js'

const { createElement: h, useState, useMemo, useEffect, useCallback } = React

const MOUNT_URL = '/navigator-mounts'
const HOST_ROOT = (() => {
  try {
    return path.join(os.homedir(), '.oro', 'navigator-mounts')
  } catch {
    return '.oro/navigator-mounts'
  }
})()

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function resolvePlatform () {
  try {
    if (typeof os.platform === 'function') return os.platform()
  } catch {}
  try {
    if (typeof os.type === 'function') return os.type()
  } catch {}
  return 'unknown'
}

function NavigatorMountsDemo () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Preparing mount…',
    message: 'Seeding host directory and refreshing the listing.'
  })
  const [entries, setEntries] = useState([])
  const [preview, setPreview] = useState({
    title: 'Select a file to preview',
    body: 'Choose a file from the mounted directory to fetch it through the runtime.'
  })
  const [logs, setLogs] = useState([])
  const [isSeeding, setIsSeeding] = useState(false)
  const [isRefreshing, setIsRefreshing] = useState(false)

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

  const seedHostDirectory = useCallback(async () => {
    setIsSeeding(true)
    updateStatus(
      'warning',
      'Seeding host directory…',
      `Writing sample files into ${HOST_ROOT}.`
    )
    appendLog('Seeding host directory with demo assets.')
    try {
      const seeds = [
        {
          pathname: 'hello.txt',
          contents:
            'Hello from the host file system!\nEdit this file in your favourite editor and press "Refresh listing" to see the change.\n'
        },
        {
          pathname: 'info.json',
          contents: JSON.stringify(
            {
              updatedAt: new Date().toISOString(),
              platform: resolvePlatform(),
              note: 'Example metadata served via the navigator mount.'
            },
            null,
            2
          )
        },
        {
          pathname: path.join('articles', 'index.html'),
          contents: `<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Articles</title>
  </head>
  <body>
    <h1>Articles served from the host file system</h1>
    <p>Because /navigator-mounts is mounted, this page is loaded from
    <code>${HOST_ROOT}</code>.</p>
    <p><a href="${MOUNT_URL}/hello.txt">Back to hello.txt</a></p>
  </body>
</html>\n`
        }
      ]

      await fs.mkdir(HOST_ROOT, { recursive: true })
      for (const seed of seeds) {
        const target = path.join(HOST_ROOT, seed.pathname)
        await fs.mkdir(path.dirname(target), { recursive: true })
        await fs.writeFile(target, seed.contents, 'utf8')
        appendLog(`Seeded ${seed.pathname}`)
      }

      updateStatus(
        'success',
        'Host directory seeded',
        'Sample files are ready to browse.'
      )
    } catch (error) {
      const message = formatError(error)
      appendLog(`Seeding failed: ${message}`)
      updateStatus('danger', 'Failed to seed host directory', message)
      throw error
    } finally {
      setIsSeeding(false)
    }
  }, [appendLog, updateStatus])

  const describeEntries = useCallback(async () => {
    try {
      const names = await fs.readdir(HOST_ROOT)
      const listed = await Promise.all(
        names.map(async (name) => {
          const absolute = path.join(HOST_ROOT, name)
          const stats = await fs.stat(absolute).catch(() => null)
          return {
            name,
            isDirectory: Boolean(stats?.isDirectory?.()),
            size: stats?.size ?? 0
          }
        })
      )

      const sorted = listed.sort((a, b) => a.name.localeCompare(b.name))
      appendLog(`Listing refreshed (${sorted.length} entries).`)
      return sorted
    } catch (error) {
      const message = formatError(error)
      appendLog(`Failed to read mounted directory: ${message}`)
      throw new Error(
        `Unable to read mounted directory at ${HOST_ROOT}: ${message}`
      )
    }
  }, [appendLog])

  const refreshEntries = useCallback(async () => {
    setIsRefreshing(true)
    try {
      const list = await describeEntries()
      setEntries(list)
      if (list.length === 0) {
        updateStatus(
          'warning',
          'Host directory is empty',
          'Seed files to populate the mounted directory.'
        )
      } else {
        updateStatus(
          'success',
          'Mounted directory ready',
          `Serving ${list.length} entries from ${HOST_ROOT}.`
        )
      }
      return list
    } catch (error) {
      const message = formatError(error)
      setEntries([])
      updateStatus('danger', 'Failed to refresh listing', message)
      throw error
    } finally {
      setIsRefreshing(false)
    }
  }, [describeEntries, updateStatus])

  const fetchMountedFile = useCallback(
    async (entry) => {
      if (!entry || entry.isDirectory) {
        updateStatus(
          'info',
          'Directory entry',
          `Open ${MOUNT_URL}/${entry?.name || ''}/ in the runtime to browse.`
        )
        return
      }

      try {
        updateStatus(
          'info',
          'Fetching file…',
          `Requesting ${entry.name} through ${MOUNT_URL}.`
        )
        appendLog(`Fetching ${entry.name}`)
        const response = await fetch(
          `${MOUNT_URL}/${encodeURIComponent(entry.name)}`
        )
        if (!response.ok) {
          throw new Error(`${response.status} ${response.statusText}`)
        }

        const contentType = response.headers.get('content-type') || 'text/plain'
        let body
        if (contentType.includes('application/json')) {
          body = JSON.stringify(await response.json(), null, 2)
        } else {
          body = await response.text()
        }

        setPreview({
          title: `${entry.name} (${contentType})`,
          body
        })
        appendLog(`Fetched ${entry.name} (${contentType})`)
        updateStatus(
          'success',
          'Fetch complete',
          `${entry.name} served via navigator mount.`
        )
      } catch (error) {
        const message = formatError(error)
        appendLog(`Failed to fetch ${entry?.name}: ${message}`)
        updateStatus('danger', 'Fetch failed', message)
      }
    },
    [appendLog, updateStatus]
  )

  useEffect(() => {
    let cancelled = false
    ;(async () => {
      try {
        await seedHostDirectory()
      } catch {}
      if (cancelled) return
      try {
        await refreshEntries()
      } catch {}
    })()
    return () => {
      cancelled = true
    }
  }, [refreshEntries, seedHostDirectory])

  const statusBanner = useMemo(() => {
    return h(StatusBanner, {
      variant: status.variant,
      title: status.title,
      message: status.message
    })
  }, [status])

  const mountInfo = useMemo(() => {
    return h(
      ExampleProse,
      null,
      h(
        'ul',
        null,
        h('li', null, 'Navigator mount URL: ', h(InlineCode, null, MOUNT_URL)),
        h('li', null, 'Host directory: ', h(InlineCode, null, HOST_ROOT)),
        h(
          'li',
          null,
          'Detected platform: ',
          h(InlineCode, null, resolvePlatform())
        )
      )
    )
  }, [])

  const entriesTable = useMemo(() => {
    if (!entries.length) {
      return h(EmptyState, {
        title: 'Host directory empty',
        description: 'Seed the directory to create sample files.'
      })
    }

    return h(
      Table,
      null,
      h(
        TableHead,
        null,
        h(
          TableRow,
          null,
          h(TableHeader, null, 'Name'),
          h(TableHeader, null, 'Type'),
          h(TableHeader, null, 'Size'),
          h(TableHeader, null, 'Actions')
        )
      ),
      h(
        TableBody,
        null,
        entries.map((entry) =>
          h(
            TableRow,
            { key: entry.name },
            h(
              TableCell,
              null,
              entry.isDirectory ? `${entry.name}/` : entry.name
            ),
            h(
              TableCell,
              null,
              h(
                StatusPill,
                {
                  status: entry.isDirectory ? 'info' : 'success'
                },
                entry.isDirectory ? 'directory' : 'file'
              )
            ),
            h(TableCell, null, entry.isDirectory ? '—' : `${entry.size} bytes`),
            h(
              TableCell,
              null,
              entry.isDirectory
                ? h(
                  Button,
                  {
                    type: 'button',
                    variant: 'ghost',
                    size: 'sm',
                    onClick: () => fetchMountedFile(entry)
                  },
                  'Open path'
                )
                : h(
                  Button,
                  {
                    type: 'button',
                    size: 'sm',
                    onClick: () => fetchMountedFile(entry)
                  },
                  'Fetch'
                )
            )
          )
        )
      )
    )
  }, [entries, fetchMountedFile])

  return h(
    ExampleLayout,
    {
      title: 'Navigator Mounts',
      description:
        'Expose host directories to the runtime via navigator mounts and inspect the served files.'
    },
    h(
      ExamplePanel,
      {
        title: 'Mount Explorer',
        description:
          'Seed the host directory, list mounted files, and fetch content through the runtime.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        h(
          ExampleSection,
          {
            title: 'Mount configuration'
          },
          mountInfo
        ),
        h(
          ExampleSection,
          {
            title: 'Actions',
            description: 'Seed or refresh the mounted directory.'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () =>
                  seedHostDirectory()
                    .then(() => refreshEntries())
                    .catch(() => {}),
                disabled: isSeeding || isRefreshing
              },
              isSeeding ? 'Seeding…' : 'Re-seed host folder'
            ),
            h(
              Button,
              {
                type: 'button',
                variant: 'secondary',
                onClick: () => refreshEntries().catch(() => {}),
                disabled: isRefreshing || isSeeding
              },
              isRefreshing ? 'Refreshing…' : 'Refresh listing'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Mounted files',
            description: 'Entries detected in the host directory.',
            actions: h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                size: 'sm',
                onClick: () => refreshEntries().catch(() => {}),
                disabled: isRefreshing || isSeeding
              },
              'Refresh'
            )
          },
          entriesTable
        ),
        h(
          ExampleSection,
          {
            title: preview.title || 'Preview',
            description: 'Fetched through navigator.mounts.'
          },
          h(Code, null, preview.body || '')
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Operations performed by this example.',
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
            emptyState: 'No activity yet.'
          })
        )
      )
    )
  )
}

mountExample(NavigatorMountsDemo)
