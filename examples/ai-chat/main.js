import { Model } from 'oro:ai/llm'
import fs from 'oro:fs/promises'
import process from 'oro:process'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleStack,
  ExampleSection,
  ScrollArea,
  Button,
  Input,
  Textarea,
  Select,
  Option,
  Badge,
  Alert,
  AlertTitle,
  AlertDescription,
  FormField,
  StatusPill,
  EmptyState,
  cn
} from '../ui/index.js'

const {
  createElement: h,
  Fragment,
  useState,
  useEffect,
  useMemo,
  useCallback,
  useRef
} = React

const defaultSystemPrompt = 'You are a helpful assistant.'
const storageKey = 'ai-chat:sessions'
const lastSessionKey = 'ai-chat:last-session'
const dirStorageKey = 'ai-chat:model-dir'
const endpointStorageKey = 'ai-chat:endpoint'
const defaultEndpoint = '/ai/llama'

const MODEL_SOURCE = {
  LOADED: 'loaded',
  DISK: 'disk'
}

const hasStorage =
  typeof globalThis !== 'undefined' &&
  typeof globalThis.localStorage !== 'undefined'

function expandHome (input) {
  if (!input || input[0] !== '~') return input
  const home = process?.env?.HOME || process?.env?.USERPROFILE || ''
  return home ? `${home}${input.slice(1)}` : input
}

function defaultModelDir () {
  const home = process?.env?.HOME || process?.env?.USERPROFILE
  if (home) return `${home}/models`
  return './models'
}

function normalizeEndpoint (input) {
  if (!input) return defaultEndpoint
  const trimmed = `${input}`.trim()
  if (!trimmed) return defaultEndpoint
  if (trimmed === '/') return '/'
  return trimmed.replace(/\/+$/, '')
}

function isRemoteEndpoint (input) {
  if (!input) return false
  return /^https?:\/\//i.test(input)
}

function buildEndpointURL (base, path) {
  const normalizedBase = normalizeEndpoint(base)
  const suffix = path.startsWith('/') ? path : `/${path}`
  if (normalizedBase === '/' || normalizedBase === '') {
    return suffix
  }
  return `${normalizedBase}${suffix}`
}

function readStorage (key) {
  if (!hasStorage) return null
  try {
    return globalThis.localStorage.getItem(key)
  } catch (err) {
    console.warn('ai-chat: failed to read storage key', key, err)
  }
  return null
}

function writeStorage (key, value) {
  if (!hasStorage) return
  try {
    globalThis.localStorage.setItem(key, value)
  } catch (err) {
    console.warn('ai-chat: failed to write storage key', key, err)
  }
}

function deleteStorage (key) {
  if (!hasStorage) return
  try {
    globalThis.localStorage.removeItem(key)
  } catch (err) {
    console.warn('ai-chat: failed to delete storage key', key, err)
  }
}

function isValidMessage (message) {
  if (!message || typeof message !== 'object') return false
  const roles = ['system', 'user', 'assistant']
  return roles.includes(message.role) && typeof message.content === 'string'
}

function ensureSystemMessage (messages) {
  const entries = Array.isArray(messages) ? messages.filter(isValidMessage) : []
  if (!entries.length || entries[0].role !== 'system') {
    entries.unshift({ role: 'system', content: defaultSystemPrompt })
  }
  return entries
}

function generateSessionId () {
  return `session-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`
}

function normaliseSession (entry) {
  if (!entry || typeof entry !== 'object') return null
  const id = typeof entry.id === 'string' ? entry.id : generateSessionId()
  const title =
    typeof entry.title === 'string' && entry.title.trim().length > 0
      ? entry.title.trim()
      : 'New chat'
  const createdAt = Number(entry.createdAt) || Date.now()
  const messages = ensureSystemMessage(entry.messages)
  return { id, title, createdAt, messages }
}

function hydrateStoredSessions () {
  const raw = readStorage(storageKey)
  if (!raw) return []
  try {
    const parsed = JSON.parse(raw)
    if (!Array.isArray(parsed)) return []
    const sessions = []
    for (const entry of parsed) {
      const session = normaliseSession(entry)
      if (session) sessions.push(session)
    }
    return sessions
  } catch (err) {
    console.warn('ai-chat: failed to parse stored sessions', err)
    return []
  }
}

function createBlankSession () {
  return {
    id: generateSessionId(),
    title: 'New chat',
    createdAt: Date.now(),
    messages: ensureSystemMessage([])
  }
}

function countUserMessages (session) {
  if (!session) return 0
  return session.messages.filter((msg) => msg.role !== 'system').length
}

function truncateTitle (text) {
  if (!text) return 'Chat'
  const trimmed = text.trim()
  if (trimmed.length <= 60) return trimmed
  return `${trimmed.slice(0, 57)}…`
}

function formatRelativeTime (value) {
  if (!value) return 'just now'
  const now = Date.now()
  const diff = now - value
  if (diff < 60_000) return 'just now'
  if (diff < 3_600_000) {
    const mins = Math.round(diff / 60_000)
    return `${mins} min${mins === 1 ? '' : 's'} ago`
  }
  if (diff < 86_400_000) {
    const hours = Math.round(diff / 3_600_000)
    return `${hours} hr${hours === 1 ? '' : 's'} ago`
  }
  const days = Math.round(diff / 86_400_000)
  return `${days} day${days === 1 ? '' : 's'} ago`
}

function alertVariant (tone) {
  switch (tone) {
    case 'success':
      return 'success'
    case 'warning':
      return 'warning'
    case 'error':
      return 'danger'
    default:
      return 'default'
  }
}

const initialSessionsRef = {
  sessions: hydrateStoredSessions()
}

const initialActiveSessionId = (() => {
  const last = readStorage(lastSessionKey)
  if (
    last &&
    initialSessionsRef.sessions.some((session) => session.id === last)
  ) {
    return last
  }
  return initialSessionsRef.sessions[0]?.id || ''
})()

function AIChatApp () {
  const [sessions, setSessions] = useState(initialSessionsRef.sessions)
  const [activeSessionId, setActiveSessionId] = useState(initialActiveSessionId)
  const [endpoint, setEndpoint] = useState(() =>
    normalizeEndpoint(readStorage(endpointStorageKey) || defaultEndpoint)
  )
  const [modelDir, setModelDir] = useState(
    () => readStorage(dirStorageKey) || defaultModelDir()
  )
  const [status, setStatusState] = useState({
    message: initialSessionsRef.sessions.length
      ? 'Select a model to continue.'
      : 'Create a chat to get started.',
    tone: 'info'
  })
  const [models, setModels] = useState([])
  const [selectedModelName, setSelectedModelName] = useState('')
  const [activeModelName, setActiveModelName] = useState('')
  const [isLoadingModel, setIsLoadingModel] = useState(false)
  const [isSending, setIsSending] = useState(false)
  const [inputValue, setInputValue] = useState('')
  const [asideOpen, setAsideOpen] = useState(() =>
    typeof window !== 'undefined' ? window.innerWidth > 1024 : true
  )

  const modelRef = useRef(null)
  const chatEndRef = useRef(null)
  const sessionsRefLocal = useRef(sessions)
  const effectiveEndpoint = useMemo(
    () => normalizeEndpoint(endpoint),
    [endpoint]
  )
  const isRemote = useMemo(
    () => isRemoteEndpoint(effectiveEndpoint),
    [effectiveEndpoint]
  )

  useEffect(() => {
    sessionsRefLocal.current = sessions
  }, [sessions])

  useEffect(() => {
    writeStorage(endpointStorageKey, effectiveEndpoint)
  }, [effectiveEndpoint])

  const setStatus = useCallback((message, tone = 'info') => {
    setStatusState({ message, tone })
  }, [])

  useEffect(() => {
    if (!hasStorage) return
    try {
      const serialisable = sessions.map((session) => ({
        id: session.id,
        title: session.title,
        createdAt: session.createdAt,
        messages: session.messages.map((msg) => ({
          role: msg.role,
          content: msg.content
        }))
      }))
      writeStorage(storageKey, JSON.stringify(serialisable))
      if (activeSessionId) writeStorage(lastSessionKey, activeSessionId)
      else deleteStorage(lastSessionKey)
    } catch (err) {
      console.warn('ai-chat: failed to persist sessions', err)
    }
  }, [sessions, activeSessionId])

  useEffect(() => {
    if (initialSessionsRef.sessions.length > 0) return
    const session = createBlankSession()
    setSessions([session])
    setActiveSessionId(session.id)
  }, [])

  useEffect(() => {
    const handleResize = () => {
      if (typeof window === 'undefined') return
      if (window.innerWidth > 1024) setAsideOpen(true)
    }
    if (typeof window !== 'undefined') {
      window.addEventListener('resize', handleResize)
      return () => window.removeEventListener('resize', handleResize)
    }
    return undefined
  }, [])

  const activeSession = useMemo(() => {
    return (
      sessions.find((session) => session.id === activeSessionId) ||
      sessions[0] ||
      null
    )
  }, [sessions, activeSessionId])

  const sortedSessions = useMemo(() => {
    return [...sessions].sort((a, b) => b.createdAt - a.createdAt)
  }, [sessions])

  const metrics = useMemo(() => {
    const totalMessages = sessions.reduce(
      (sum, session) => sum + countUserMessages(session),
      0
    )
    return {
      sessions: sessions.length,
      messages: totalMessages,
      averageMessages: sessions.length
        ? Math.round((totalMessages / sessions.length) * 10) / 10
        : 0
    }
  }, [sessions])

  const handleSelectSession = useCallback((sessionId) => {
    setActiveSessionId(sessionId)
  }, [])

  const handleDeleteSession = useCallback((sessionId) => {
    setSessions((prev) => {
      const next = prev.filter((session) => session.id !== sessionId)
      if (!next.length) {
        const replacement = createBlankSession()
        setActiveSessionId(replacement.id)
        return [replacement]
      }
      setActiveSessionId((current) => {
        if (current && current !== sessionId) return current
        const sorted = [...next].sort((a, b) => b.createdAt - a.createdAt)
        return sorted[0]?.id || ''
      })
      return next
    })
  }, [])

  const handleCreateSession = useCallback(() => {
    const session = createBlankSession()
    setSessions((prev) => [...prev, session])
    setActiveSessionId(session.id)
    setStatus('New chat ready.')
    return session
  }, [setStatus])

  const handleToggleAside = useCallback(() => {
    setAsideOpen((prev) => !prev)
  }, [])

  const refreshModels = useCallback(async () => {
    const rawDir = (modelDir || '').trim() || defaultModelDir()
    setModelDir(rawDir)
    writeStorage(dirStorageKey, rawDir)

    const remoteActive = isRemote
    const directory = remoteActive ? '' : expandHome(rawDir)
    setStatus(
      remoteActive ? 'Fetching remote models...' : 'Looking for models...'
    )

    const discovered = new Map()
    let loadedModels = []
    try {
      const res = await fetch(buildEndpointURL(effectiveEndpoint, '/v1/models'))
      if (res.ok) {
        const payload = await res.json()
        if (payload && Array.isArray(payload.data)) {
          loadedModels = payload.data
          for (const entry of loadedModels) {
            const name = entry?.meta?.name || entry?.id
            if (typeof name === 'string' && name.length > 0) {
              const storedDir =
                typeof entry?.meta?.options?.directory === 'string'
                  ? entry.meta.options.directory
                  : ''
              const label = remoteActive
                ? `${name} (remote)`
                : `${name} (loaded)`
              discovered.set(name, {
                name,
                label,
                source: MODEL_SOURCE.LOADED,
                directory: storedDir
              })
            }
          }
        }
      } else {
        const payload = await res.text()
        console.warn('ai-chat: failed to query models', res.status, payload)
      }
    } catch (err) {
      console.warn('ai-chat: failed to query models', err)
      if (remoteActive) {
        setStatus(
          `Failed to reach remote endpoint: ${err?.message || err}`,
          'warning'
        )
      }
    }

    let diskModels = []
    if (!remoteActive) {
      try {
        const entries = await fs.readdir(directory)
        if (Array.isArray(entries)) {
          diskModels = entries.filter(
            (name) =>
              typeof name === 'string' && name.toLowerCase().endsWith('.gguf')
          )
        }
      } catch (err) {
        console.warn('ai-chat: failed to list models in directory', err)
        if (!discovered.size) {
          setStatus(
            `Failed to read model folder: ${err?.message || err}`,
            'error'
          )
        }
      }
    }

    if (!remoteActive) {
      for (const name of diskModels) {
        if (!discovered.has(name)) {
          discovered.set(name, {
            name,
            label: name,
            source: MODEL_SOURCE.DISK,
            directory
          })
        } else {
          const existing = discovered.get(name)
          if (existing.source === MODEL_SOURCE.LOADED && !existing.directory) {
            existing.directory = directory
          }
        }
      }
    }

    const nextModels = Array.from(discovered.values())
    setModels(nextModels)

    if (!nextModels.length) {
      setSelectedModelName('')
      setActiveModelName('')
      modelRef.current = null
      if (remoteActive) {
        setStatus(
          'No remote models reported. Ensure the remote server has models loaded.',
          'warning'
        )
      } else {
        setStatus(
          'No models available. Place a GGUF file in your models directory or load a model via the CLI.',
          'warning'
        )
      }
      return
    }

    const previous = selectedModelName
    const nextSelection =
      previous && discovered.has(previous)
        ? previous
        : nextModels[0]?.name || ''
    setSelectedModelName(nextSelection)

    const statusParts = []
    if (remoteActive) {
      if (loadedModels.length > 0) {
        statusParts.push(`${loadedModels.length} remote`)
      }
    } else {
      if (loadedModels.length > 0) {
        statusParts.push(`${loadedModels.length} loaded`)
      }
      if (diskModels.length > 0) {
        statusParts.push(`${diskModels.length} on disk`)
      }
    }
    if (statusParts.length) {
      setStatus(`Models: ${statusParts.join(', ')}`)
    } else if (!remoteActive) {
      setStatus('Select a model to continue.')
    }
  }, [modelDir, selectedModelName, setStatus, effectiveEndpoint, isRemote])

  useEffect(() => {
    refreshModels().catch((err) => {
      console.warn('ai-chat: failed to refresh models', err)
      setStatus(`Failed to refresh models: ${err?.message || err}`, 'error')
    })
  }, [refreshModels, setStatus])

  useEffect(() => {
    if (!selectedModelName) return
    const info = models.find((model) => model.name === selectedModelName)
    if (!info) return
    if (isRemote) {
      const previous = modelRef.current
      if (previous && typeof previous.unload === 'function') {
        Promise.resolve(previous.unload()).catch((err) => {
          console.warn(
            'ai-chat: failed to unload previous model before remote attach',
            err
          )
        })
      }
      setIsLoadingModel(false)
      modelRef.current = null
      setActiveModelName(info.name)
      setStatus(`Remote model "${info.name}" selected.`, 'success')
      return
    }
    if (activeModelName === info.name && modelRef.current) return

    let cancelled = false
    const loadModel = async () => {
      setIsLoadingModel(true)
      setStatus(
        info.source === MODEL_SOURCE.LOADED
          ? `Attaching to ${info.name}...`
          : `Loading ${info.name}...`
      )

      const previous = modelRef.current
      if (previous && typeof previous.unload === 'function') {
        try {
          await previous.unload()
        } catch (err) {
          console.warn('ai-chat: failed to unload previous model', err)
        }
      }

      try {
        const model = new Model({ name: info.name })
        const options =
          info.source === MODEL_SOURCE.DISK
            ? { directory: expandHome(info.directory || modelDir) }
            : undefined
        await model.load(options)
        if (cancelled) return
        modelRef.current = model
        setActiveModelName(info.name)
        setStatus(`Model "${info.name}" ready.`, 'success')
      } catch (err) {
        if (cancelled) return
        modelRef.current = null
        setActiveModelName('')
        setStatus(`Failed to load model: ${err?.message || err}`, 'error')
      } finally {
        if (!cancelled) setIsLoadingModel(false)
      }
    }

    loadModel().catch((err) => {
      if (cancelled) return
      console.warn('ai-chat: model load failed', err)
      setIsLoadingModel(false)
      setStatus(`Model load failed: ${err?.message || err}`, 'error')
    })

    return () => {
      cancelled = true
    }
  }, [
    selectedModelName,
    models,
    modelDir,
    activeModelName,
    setStatus,
    isRemote
  ])

  useEffect(() => {
    if (!chatEndRef.current) return
    chatEndRef.current.scrollIntoView({ behavior: 'smooth', block: 'end' })
  }, [activeSession?.id, activeSession?.messages.length])

  const handleInputChange = useCallback((event) => {
    setInputValue(event.target.value)
  }, [])

  const handlePromptSubmit = useCallback(
    async (event) => {
      event.preventDefault()
      if (isSending || isLoadingModel) return
      const text = inputValue.trim()
      if (!text) return

      const session = sessions.find((item) => item.id === activeSessionId)
      if (!session) {
        setStatus('Create a chat before sending messages.', 'error')
        return
      }

      if (!modelRef.current && !isRemote) {
        setStatus('Load a model before chatting.', 'error')
        return
      }

      const userMessage = { role: 'user', content: text }
      const nextMessages = [...session.messages, userMessage]
      const nextTitle =
        session.title === 'New chat' ? truncateTitle(text) : session.title

      setInputValue('')
      setSessions((prev) =>
        prev.map((item) => {
          if (item.id !== session.id) return item
          return {
            ...item,
            title: nextTitle,
            createdAt: Date.now(),
            messages: nextMessages
          }
        })
      )
      setStatus('Thinking...')
      setIsSending(true)

      const modelName =
        modelRef.current?.name || selectedModelName || activeModelName
      if (!modelName) {
        setStatus('Select a model before chatting.', 'error')
        setIsSending(false)
        return
      }

      const url = buildEndpointURL(
        effectiveEndpoint,
        `/v1/chat/completions?model=${encodeURIComponent(modelName)}`
      )
      try {
        const body = JSON.stringify({ messages: nextMessages, max_tokens: 256 })
        const res = await fetch(url, {
          method: 'POST',
          headers: { 'content-type': 'application/json' },
          body
        })

        if (!res.ok) {
          const payload = await res.text()
          throw new Error(payload || `Request failed (${res.status})`)
        }

        const data = await res.json()
        const reply = data?.choices?.[0]?.message?.content || '(no reply)'
        const assistantMessage = { role: 'assistant', content: reply }
        setSessions((prev) =>
          prev.map((item) => {
            if (item.id !== session.id) return item
            return {
              ...item,
              createdAt: Date.now(),
              messages: [...nextMessages, assistantMessage]
            }
          })
        )
        setStatus('Ready', 'success')
      } catch (err) {
        console.warn('ai-chat: chat request failed', err)
        setStatus(`Chat request failed: ${err?.message || err}`, 'error')
      } finally {
        setIsSending(false)
      }
    },
    [
      inputValue,
      sessions,
      activeSessionId,
      isSending,
      isLoadingModel,
      selectedModelName,
      activeModelName,
      setStatus,
      effectiveEndpoint,
      isRemote
    ]
  )

  const handleTextareaKeyDown = useCallback(
    (event) => {
      if (event.key === 'Enter' && !event.shiftKey) {
        event.preventDefault()
        handlePromptSubmit(event)
      }
    },
    [handlePromptSubmit]
  )

  const sessionList = sortedSessions.length
    ? sortedSessions.map((session) => {
      const isActive = session.id === (activeSession?.id || activeSessionId)
      return h(
        'div',
        { key: session.id, className: 'ai-chat__sessions-item' },
        h(
          'button',
          {
            type: 'button',
            onClick: () => handleSelectSession(session.id),
            className: cn('ai-chat__session-button', isActive && 'is-active')
          },
          h(
            'div',
            { className: 'ai-chat__session-info' },
            h('span', { className: 'ai-chat__session-title' }, session.title),
            h(
              'div',
              { className: 'ai-chat__session-meta' },
              h('span', null, `${countUserMessages(session)} msg`),
              h('span', null, formatRelativeTime(session.createdAt))
            )
          ),
          h(
            'button',
            {
              type: 'button',
              className: 'ai-chat__session-delete',
              onClick: (event) => {
                event.stopPropagation()
                handleDeleteSession(session.id)
              }
            },
            'Delete'
          )
        )
      )
    })
    : null

  const modelAside = h(
    ExampleStack,
    { gap: 'md' },
    h(
      ExamplePanel,
      {
        title: 'Sessions',
        description: 'Switch between saved chats or start a new one.',
        actions: h(
          Button,
          { variant: 'outline', size: 'sm', onClick: handleCreateSession },
          'New chat'
        )
      },
      h(
        ScrollArea,
        { className: 'ai-chat__sessions', style: { maxHeight: '320px' } },
        sessionList ||
          h(EmptyState, {
            title: 'No sessions yet',
            description:
              'Start a conversation and your chats will appear here.'
          })
      )
    ),
    h(
      ExamplePanel,
      {
        title: 'Model Control',
        description:
          'Point to a GGUF directory or connect to a remote LLaMA server.'
      },
      h(
        'div',
        { className: 'ai-chat__model-controls' },
        h(
          FormField,
          {
            label: 'API endpoint',
            description:
              'Relative (/ai/llama) or full URL of the LLaMA server.',
            required: true
          },
          h(Input, {
            value: endpoint,
            onChange: (event) => setEndpoint(event.target.value),
            onBlur: () => setEndpoint((prev) => normalizeEndpoint(prev)),
            placeholder: defaultEndpoint
          })
        ),
        h(
          FormField,
          {
            label: 'Model directory',
            description: isRemote
              ? 'Directory selection is disabled for remote endpoints.'
              : 'Directory scanned for .gguf weights.'
          },
          h(Input, {
            value: modelDir,
            onChange: (event) => setModelDir(event.target.value),
            onBlur: () => writeStorage(dirStorageKey, modelDir.trim()),
            placeholder: '~/models',
            disabled: isRemote
          })
        ),
        h(
          FormField,
          {
            label: 'Model',
            description: models.length
              ? 'Select a model to load or attach.'
              : 'No models detected yet.',
            required: !!models.length
          },
          h(
            Select,
            {
              value: selectedModelName,
              onChange: (event) => setSelectedModelName(event.target.value),
              disabled: !models.length || isLoadingModel
            },
            models.length
              ? models.map((model) =>
                h(Option, { key: model.name, value: model.name }, model.label)
              )
              : h(Option, { value: '' }, 'No models available')
          )
        ),
        h(
          Button,
          {
            variant: 'secondary',
            size: 'sm',
            onClick: refreshModels,
            disabled: isLoadingModel
          },
          'Reload models'
        ),
        h(
          'div',
          { className: 'ai-chat__model-meta' },
          h(
            'div',
            null,
            h('strong', null, 'Endpoint:'),
            ' ',
            effectiveEndpoint || defaultEndpoint
          ),
          activeModelName
            ? h(
              Fragment,
              null,
              h(
                'div',
                null,
                h('strong', null, 'Active model:'),
                ' ',
                activeModelName
              ),
              h(
                'div',
                null,
                h('strong', null, 'Source:'),
                ' ',
                models.find((model) => model.name === activeModelName)
                  ?.source || 'runtime'
              )
            )
            : h('div', null, 'No model loaded yet.')
        )
      )
    )
  )

  const messageNodes = activeSession
    ? activeSession.messages.map((message, index) => {
      const role = message.role || 'assistant'
      const key = `${message.role}-${index}`
      const ts =
          role === 'system'
            ? 'system prompt'
            : index === activeSession.messages.length - 1
              ? 'just now'
              : ''
      return h(
        'div',
        {
          key,
          className: cn('ai-chat__message', `ai-chat__message--${role}`)
        },
        h(
          'div',
          { className: 'ai-chat__message-meta' },
          h(
            Badge,
            {
              variant:
                  role === 'user'
                    ? 'info'
                    : role === 'assistant'
                      ? 'neutral'
                      : 'warning'
            },
            role
          ),
          ts && h('span', null, ts)
        ),
        h('div', { className: 'ai-chat__message-bubble' }, message.content)
      )
    })
    : null

  const isInputDisabled = !activeModelName || isLoadingModel || isSending

  return h(
    ExampleLayout,
    {
      title: 'AI Chat',
      description:
        'Talk to an LLM served by the Oro Runtime or a remote endpoint. Load models from disk or attach to ones you have already initialised.',
      badge: h(
        StatusPill,
        {
          status: activeModelName ? 'running' : 'idle'
        },
        activeModelName ? `Model: ${activeModelName}` : 'No model loaded'
      ),
      toolbarActions: h(
        Fragment,
        null,
        h(
          Button,
          {
            variant: 'ghost',
            className: 'ai-chat__aside-toggle',
            onClick: handleToggleAside
          },
          asideOpen ? 'Hide controls' : 'Show controls'
        ),
        h(
          Button,
          {
            variant: 'outline',
            onClick: handleCreateSession,
            disabled: isSending || isLoadingModel
          },
          'New chat'
        )
      ),
      aside: asideOpen ? modelAside : null,
      footer: h(
        'div',
        { className: 'ai-chat__status-meta' },
        h('span', null, 'Commands: ↩︎ send, ⇧↩︎ newline'),
        h('span', null, `Sessions: ${metrics.sessions}`),
        h('span', null, `Messages: ${metrics.messages}`)
      )
    },
    h(
      ExampleStack,
      { gap: 'lg' },
      status?.message &&
        h(
          Alert,
          { variant: alertVariant(status.tone) },
          h(
            AlertTitle,
            null,
            status.tone === 'error' ? 'Something went wrong' : 'Status'
          ),
          h(AlertDescription, null, status.message)
        ),
      h(
        ExamplePanel,
        {
          title: activeSession?.title || 'Conversation',
          description: activeModelName
            ? `Chatting with ${activeModelName}.`
            : 'Load a model to begin chatting.'
        },
        h(
          ExampleSection,
          {
            title: 'Conversation history',
            description: `${countUserMessages(activeSession)} messages in this chat.`
          },
          h(
            ScrollArea,
            { className: 'ai-chat__log' },
            messageNodes ||
              h(EmptyState, {
                title: 'No messages yet',
                description: 'Send a prompt to start the conversation.'
              }),
            h('div', { ref: chatEndRef })
          )
        ),
        h(
          'form',
          {
            className: 'ai-chat__composer',
            onSubmit: handlePromptSubmit
          },
          h(
            FormField,
            {
              label: 'Your message',
              required: true,
              hint: activeModelName
                ? 'Press Enter to send, Shift + Enter for a new line.'
                : 'Load a model to enable chat.'
            },
            h(Textarea, {
              value: inputValue,
              placeholder: activeModelName
                ? 'Ask me anything…'
                : 'Load a model before sending a message.',
              onChange: handleInputChange,
              onKeyDown: handleTextareaKeyDown,
              disabled: isInputDisabled,
              rows: 5
            })
          ),
          h(
            'div',
            { className: 'ai-chat__composer-actions' },
            h(
              Button,
              {
                type: 'submit',
                disabled: isInputDisabled || !inputValue.trim()
              },
              isSending ? 'Sending…' : 'Send'
            )
          )
        )
      )
    )
  )
}

mountExample(AIChatApp)
