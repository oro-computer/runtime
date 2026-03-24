import { open } from 'oro:sqlite'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleGrid,
  ExamplePanel,
  ExampleStack,
  StatusBanner,
  FormField,
  Textarea,
  Button,
  Badge,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  EmptyState,
  LogViewer,
  Code,
  InlineCode
} from '../ui/index.js'

const { createElement: h, useState, useEffect, useCallback, useMemo } = React

const RESET_SQL = `
DROP TABLE IF EXISTS task_audit;
DROP TABLE IF EXISTS tasks;
DROP TABLE IF EXISTS projects;
`

const SCHEMA_SQL = `
PRAGMA foreign_keys = ON;

CREATE TABLE projects (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE tasks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  title TEXT NOT NULL,
  assignee TEXT,
  priority INTEGER NOT NULL DEFAULT 2,
  completed INTEGER NOT NULL DEFAULT 0,
  due_date TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE task_audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  task_id INTEGER NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
  action TEXT NOT NULL,
  note TEXT,
  occurred_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
`

const SEED_SQL = `
INSERT INTO projects (name, status, created_at) VALUES
  ('Desktop App Revamp', 'active', datetime('now', '-8 days')),
  ('Device Bridge', 'active', datetime('now', '-21 days')),
  ('QA Automation', 'paused', datetime('now', '-35 days'));

INSERT INTO tasks (project_id, title, assignee, priority, completed, due_date, created_at) VALUES
  (1, 'Migrate settings store to secure storage', 'mina', 1, 0, date('now', '+4 days'), datetime('now', '-2 days')),
  (1, 'Refine window layout presets', 'ramon', 2, 0, date('now', '+9 days'), datetime('now', '-1 days')),
  (1, 'Ship beta build to design team', 'mina', 1, 1, date('now', '-1 days'), datetime('now', '-6 days')),
  (2, 'Add HID controller support', 'sasha', 1, 0, date('now', '+2 days'), datetime('now', '-4 days')),
  (2, 'Stream USB diagnostics to dashboard', 'leah', 2, 0, date('now', '+11 days'), datetime('now', '-12 days')),
  (2, 'Document bridge protocol', 'amir', 3, 1, null, datetime('now', '-14 days')),
  (3, 'Stabilise flaky smoke tests', 'nina', 2, 0, date('now', '+6 days'), datetime('now', '-10 days')),
  (3, 'Spin up dedicated metrics collector', 'matteo', 3, 0, null, datetime('now', '-7 days'));

INSERT INTO task_audit (task_id, action, note, occurred_at) VALUES
  (1, 'created', 'Seed task inserted for secure storage migration', datetime('now', '-2 days')),
  (3, 'completed', 'Task marked as done during dataset seeding', datetime('now', '-6 days')),
  (4, 'created', 'Seed task inserted for HID support work', datetime('now', '-4 days'));
`

const SAMPLE_QUERIES = [
  {
    label: 'Active tasks by priority',
    description: 'Join projects and list all open tasks ordered by urgency.',
    sql: `
SELECT
  p.name AS project,
  t.title,
  t.assignee,
  CASE t.priority
    WHEN 1 THEN 'P1 – Critical'
    WHEN 2 THEN 'P2 – High'
    ELSE 'P3 – Normal'
  END AS priority_label,
  CASE t.completed WHEN 1 THEN 'Done' ELSE 'Open' END AS status,
  t.due_date
FROM tasks t
JOIN projects p ON p.id = t.project_id
WHERE t.completed = 0
ORDER BY t.priority ASC, t.due_date ASC, t.created_at ASC;
`.trim()
  },
  {
    label: 'Project completion rate',
    description: 'Track total and completed tasks per project.',
    sql: `
SELECT
  p.name AS project,
  COUNT(t.id) AS total_tasks,
  SUM(CASE WHEN t.completed = 1 THEN 1 ELSE 0 END) AS completed_tasks,
  ROUND(SUM(CASE WHEN t.completed = 1 THEN 1 ELSE 0 END) * 1.0 / NULLIF(COUNT(t.id), 0), 2) AS completion_rate
FROM projects p
LEFT JOIN tasks t ON t.project_id = p.id
GROUP BY p.id
ORDER BY completion_rate IS NULL, completion_rate DESC;
`.trim()
  },
  {
    label: 'Upcoming deadlines (7d)',
    description: 'Surface outstanding work due within the next week.',
    sql: `
SELECT
  t.title,
  p.name AS project,
  t.assignee,
  t.due_date,
  ROUND(julianday(t.due_date) - julianday('now'), 1) AS days_remaining
FROM tasks t
JOIN projects p ON p.id = t.project_id
WHERE
  t.completed = 0
  AND t.due_date IS NOT NULL
  AND date(t.due_date) <= date('now', '+7 days')
ORDER BY t.due_date ASC;
`.trim()
  },
  {
    label: 'Recent task audit trail',
    description: 'Inspect audit entries to see how tasks evolved.',
    sql: `
SELECT
  a.occurred_at,
  t.title,
  a.action,
  COALESCE(a.note, '—') AS note
FROM task_audit a
JOIN tasks t ON t.id = a.task_id
ORDER BY a.occurred_at DESC
LIMIT 25;
`.trim()
  }
]

const DEFAULT_QUERY = SAMPLE_QUERIES[0].sql

function formatError (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function truncateSql (sql, limit = 120) {
  const trimmed = sql.replace(/\s+/g, ' ').trim()
  if (trimmed.length <= limit) return trimmed
  return `${trimmed.slice(0, limit)}…`
}

function formatDuration (ms) {
  if (!Number.isFinite(ms)) return '0 ms'
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`
  }
  return `${ms.toFixed(2)} ms`
}

function formatRowid (value) {
  if (typeof value === 'bigint') {
    return value === 0n ? '—' : `${value}n`
  }
  if (typeof value === 'number') {
    return value === 0 ? '—' : String(value)
  }
  return value && value !== 0 ? String(value) : '—'
}

function formatCellValue (value) {
  if (value === null) return 'NULL'
  if (typeof value === 'bigint') return `${value}n`
  if (ArrayBuffer.isView(value)) {
    return `Uint8Array(${value.byteLength})`
  }
  if (typeof value === 'object') {
    try {
      return JSON.stringify(value)
    } catch {
      return '[object]'
    }
  }
  return String(value)
}

function SqliteNotebook () {
  const [database, setDatabase] = useState(null)
  const [query, setQuery] = useState(DEFAULT_QUERY)
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Opening database…',
    message: 'Creating an in-memory SQLite connection managed by the runtime.'
  })
  const [logs, setLogs] = useState([])
  const [result, setResult] = useState(null)
  const [isBusy, setIsBusy] = useState(false)
  const [prepared, setPrepared] = useState(false)

  const appendLog = useCallback((message) => {
    setLogs((prev) => {
      const timestamp = new Date().toISOString().slice(11, 23)
      const entry = {
        id: `${Date.now()}-${Math.random().toString(16).slice(2)}`,
        text: `[${timestamp}] ${message}`
      }
      const next = [...prev, entry]
      return next.length > 200 ? next.slice(next.length - 200) : next
    })
  }, [])

  useEffect(() => {
    let db = null
    try {
      db = open(':memory:')
      setDatabase(db)
      setStatus({
        variant: 'info',
        title: 'Preparing dataset…',
        message: 'Applying schema and inserting starter records.'
      })
      appendLog('Opened in-memory database.')
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Failed to open database',
        message
      })
      appendLog(`Failed to open database: ${message}`)
      return () => {}
    }

    return () => {
      if (!db) return
      try {
        db.close()
        appendLog('Closed database connection.')
      } catch (error) {
        appendLog(`Error while closing database: ${formatError(error)}`)
      }
    }
  }, [appendLog])

  useEffect(() => {
    if (!database || prepared) return
    try {
      database.exec(RESET_SQL)
      database.exec(SCHEMA_SQL)
      database.exec(SEED_SQL)
      setPrepared(true)
      setStatus({
        variant: 'success',
        title: 'Dataset ready',
        message: 'Execute SQL, inspect results, or pick a starter query.'
      })
      appendLog('Schema created (projects, tasks, task_audit).')
      appendLog('Seed data inserted with sample workflow records.')
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Failed to prepare dataset',
        message
      })
      appendLog(`Failed to prepare dataset: ${message}`)
    }
  }, [database, prepared, appendLog])

  const runQuery = useCallback(
    (inputSql, meta = {}) => {
      if (!database) return
      const sql = (inputSql ?? query ?? '').trim()
      if (!sql) {
        setStatus({
          variant: 'warning',
          title: 'Missing SQL statement',
          message: 'Write a query or choose one of the starter snippets.'
        })
        return
      }
      setIsBusy(true)
      const start =
        typeof performance !== 'undefined' && performance.now
          ? performance.now()
          : Date.now()
      try {
        const output = database.exec(sql)
        const end =
          typeof performance !== 'undefined' && performance.now
            ? performance.now()
            : Date.now()
        const duration = end - start
        setResult({
          ...output,
          sql,
          label: meta.label ?? null,
          duration
        })
        const rowSummary =
          output.rows.length === 1 ? '1 row' : `${output.rows.length} rows`
        setStatus({
          variant: 'success',
          title: 'Query executed',
          message: `${rowSummary} returned in ${formatDuration(duration)}.`
        })
        appendLog(
          `Query ok (${formatDuration(duration)}): ${meta.label ? `${meta.label} → ` : ''}${truncateSql(sql)}`
        )
      } catch (error) {
        const message = formatError(error)
        setResult(null)
        setStatus({
          variant: 'danger',
          title: 'Query failed',
          message
        })
        appendLog(`Query failed: ${message}`)
      } finally {
        setIsBusy(false)
      }
    },
    [database, query, appendLog]
  )

  const handleSubmit = useCallback(
    (event) => {
      event.preventDefault()
      runQuery(query)
    },
    [runQuery, query]
  )

  const handleShortcut = useCallback(
    (event) => {
      if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') {
        event.preventDefault()
        runQuery(query)
      }
    },
    [runQuery, query]
  )

  const handleReset = useCallback(() => {
    if (!database) return
    try {
      database.exec(RESET_SQL)
      database.exec(SCHEMA_SQL)
      database.exec(SEED_SQL)
      setQuery(DEFAULT_QUERY)
      setResult(null)
      setStatus({
        variant: 'info',
        title: 'Dataset reset',
        message:
          'Starter records restored. Run a query to inspect the fresh state.'
      })
      appendLog('Dataset reset to seeded state.')
    } catch (error) {
      const message = formatError(error)
      setStatus({
        variant: 'danger',
        title: 'Reset failed',
        message
      })
      appendLog(`Reset failed: ${message}`)
    }
  }, [database, appendLog])

  const handleSample = useCallback(
    (sample) => {
      setQuery(sample.sql)
      runQuery(sample.sql, { label: sample.label })
    },
    [runQuery]
  )

  const resultSummary = useMemo(() => {
    if (!result) return []
    return [
      { label: 'Rows returned', value: result.rows.length },
      { label: 'Columns', value: result.columns.length },
      { label: 'Rows changed', value: result.changes },
      {
        label: 'Last insert rowid',
        value: formatRowid(result.lastInsertRowid)
      },
      { label: 'Duration', value: formatDuration(result.duration ?? 0) }
    ]
  }, [result])

  const renderedTable = useMemo(() => {
    if (!result) {
      return h(EmptyState, {
        title: 'Run a query',
        description:
          'Results appear here. Try the starter query or write your own SQL.'
      })
    }

    if (!result.rows || result.rows.length === 0) {
      const message =
        result.changes > 0
          ? `Statement affected ${result.changes} row${result.changes === 1 ? '' : 's'}.`
          : 'No rows returned.'
      return h(EmptyState, {
        title: 'No result rows',
        description: message
      })
    }

    const headers = result.columns.length
      ? result.columns
      : Object.keys(result.rows[0] || {})

    return h(
      Table,
      { className: 'sqlite-app__table' },
      h(
        TableHead,
        null,
        h(
          TableRow,
          null,
          headers.map((column, index) => {
            const meta = result.columnsMeta?.[index]
            const metaLabel = meta?.declType || meta?.type || ''
            return h(
              TableHeader,
              { key: column },
              column,
              metaLabel
                ? h(
                  'span',
                  {
                    style: {
                      display: 'block',
                      fontSize: '11px',
                      opacity: 0.72
                    }
                  },
                  metaLabel
                )
                : null
            )
          })
        )
      ),
      h(
        TableBody,
        null,
        result.rows.map((row, rowIndex) =>
          h(
            TableRow,
            { key: `row-${rowIndex}` },
            headers.map((column, columnIndex) => {
              const value =
                row && typeof row === 'object' && !Array.isArray(row)
                  ? row[column]
                  : Array.isArray(row)
                    ? row[columnIndex]
                    : undefined
              return h(
                TableCell,
                { key: `cell-${column}-${rowIndex}` },
                formatCellValue(value)
              )
            })
          )
        )
      )
    )
  }, [result])

  return h(
    ExampleLayout,
    {
      title: 'SQLite Notebook',
      description:
        'Interact with the runtime-managed SQLite engine, run ad-hoc queries, and inspect result metadata.',
      badge: h(Badge, { variant: 'info' }, 'In-memory database')
    },
    h(StatusBanner, {
      variant: status.variant,
      title: status.title,
      message: status.message
    }),
    h(
      ExampleGrid,
      { columns: 2 },
      h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Run SQL',
            description:
              'Shift focus into the editor, write a statement, and press Ctrl/⌘ + Enter to execute.'
          },
          h(
            'form',
            { onSubmit: handleSubmit },
            h(
              ExampleStack,
              { gap: 'md' },
              h(
                FormField,
                {
                  label: 'SQL statement',
                  description:
                    "Statements execute synchronously via the runtime's sqlite service."
                },
                h(Textarea, {
                  className: 'sqlite-app__editor',
                  value: query,
                  onChange: (event) => setQuery(event.target.value),
                  onKeyDown: handleShortcut,
                  spellCheck: false,
                  placeholder: 'SELECT * FROM tasks;'
                })
              ),
              h(
                'div',
                { className: 'sqlite-app__actions' },
                h(Button, {
                  type: 'submit',
                  disabled: isBusy,
                  children: isBusy ? 'Running…' : 'Run query'
                }),
                h(Button, {
                  type: 'button',
                  variant: 'ghost',
                  onClick: handleReset,
                  disabled: isBusy,
                  children: 'Reset & reseed dataset'
                })
              )
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Starter queries',
            description:
              'Each snippet runs immediately and populates the editor so you can tweak it.'
          },
          h(
            'div',
            { className: 'sqlite-app__samples' },
            SAMPLE_QUERIES.map((sample) =>
              h(Button, {
                key: sample.label,
                type: 'button',
                variant: 'secondary',
                onClick: () => handleSample(sample),
                children: [
                  sample.label,
                  h(
                    'span',
                    { style: { fontSize: '12px', opacity: 0.7 } },
                    'Run & copy'
                  )
                ]
              })
            )
          ),
          h(
            'p',
            {
              style: {
                margin: 0,
                color: 'var(--ui-foreground-muted)',
                fontSize: '13px'
              }
            },
            'Need inspiration? Each sample uses the seeded projects/tasks dataset.'
          )
        )
      ),
      h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Result set',
            description:
              'Inspect the rows, metadata, and execution summary produced by the last query.'
          },
          result?.label &&
            h(
              Badge,
              {
                variant: 'neutral',
                style: { alignSelf: 'flex-start', marginBottom: '12px' }
              },
              result.label
            ),
          result &&
            h(
              'div',
              { className: 'sqlite-app__summary' },
              resultSummary.map((item) =>
                h(
                  'div',
                  { key: item.label, className: 'sqlite-app__summary-item' },
                  h(
                    'span',
                    { className: 'sqlite-app__summary-label' },
                    item.label
                  ),
                  h(
                    'span',
                    { className: 'sqlite-app__summary-value' },
                    item.value
                  )
                )
              )
            ),
          renderedTable,
          result &&
            h(
              'div',
              { style: { marginTop: '18px' } },
              h(
                'p',
                {
                  style: {
                    marginBottom: '8px',
                    fontSize: '13px',
                    color: 'var(--ui-foreground-muted)'
                  }
                },
                'Executed SQL'
              ),
              h(Code, null, result.sql)
            )
        ),
        h(
          ExamplePanel,
          {
            title: 'Activity log',
            description:
              'Trace executed statements, dataset resets, and lifecycle events.'
          },
          h(LogViewer, {
            entries: logs,
            emptyState: () => h('span', null, 'No statements executed yet.'),
            renderEntry: (entry) => entry.text
          }),
          h(
            'p',
            {
              style: {
                marginTop: '12px',
                fontSize: '13px',
                color: 'var(--ui-foreground-muted)'
              }
            },
            'Statements execute via the ',
            h(InlineCode, null, 'oro:sqlite'),
            ' API, keeping the UI responsive while the runtime performs synchronous work.'
          )
        )
      )
    )
  )
}

mountExample(SqliteNotebook)
