/* global Worker */
import { channel } from './index.js'
import application from '../application.js'
import globals from '../internal/globals.js'
import hooks from '../hooks.js'

const WORKER_INSTALL_TIMEOUT = 10_000

export const workers = new Map()
export { channel }

const installations = new Map()

hooks.onReady(async () => {
  const currentWindow = await application.getCurrentWindow()
  const announceReady = () => {
    channel.postMessage({ ready: currentWindow.index })
  }

  channel.addEventListener('message', (event) => {
    if (event.data?.probe === currentWindow.index) {
      announceReady()
    }
  })

  announceReady()
})

globals.register('SharedWorkerContext.workers', workers)
globals.register('SharedWorkerContext.info', new Map())

globalThis.addEventListener('connect', (event) => {
  if (event.detail?.connect) {
    onConnect(new MessageEvent('connect', { data: event.detail })).catch(
      (err) => console.error(err)
    )
  }
})

export class SharedWorkerInstance extends Worker {
  #info = null

  constructor (filename, options) {
    super(filename, {
      name: `SharedWorker (${options?.info?.pathname ?? filename})`,
      ...options,
      [Symbol.for('oro.runtime.internal.worker.type')]: 'sharedWorker'
    })

    this.#info = options?.info ?? null
    this.addEventListener('message', this.onMessage.bind(this))
    this.addEventListener('error', (event) => {
      event.preventDefault?.()
      const message =
        event.error?.message ?? event.message ?? 'SharedWorker bootstrap failed'
      try {
        channel.postMessage({
          error: { id: this.#info?.id, message }
        })
      } catch {}
    })
  }

  get info () {
    return this.#info
  }

  async onMessage (event) {
    if (Array.isArray(event.data?.__shared_worker_debug)) {
      const log = document.querySelector('#log')
      if (log) {
        for (const entry of event.data.__shared_worker_debug) {
          const lines = entry.split('\n')
          const span = document.createElement('span')
          let target = span

          for (const line of lines) {
            if (!line) continue
            const item = document.createElement('code')
            item.innerHTML = line
              .replace(/\s/g, '&nbsp;')
              .replace(/\\s/g, ' ')
              .replace(/<anonymous>/g, '&lt;anonymous&gt;')
              .replace(
                /([a-z|A-Z|_|0-9]+(Error|Exception)):/g,
                '<span class="red"><b>$1</b>:</span>'
              )

            if (target === span && lines.length > 1) {
              target = document.createElement('details')
              const summary = document.createElement('summary')
              summary.appendChild(item)
              target.appendChild(summary)
              span.appendChild(target)
            } else {
              target.appendChild(item)
            }
          }

          log.appendChild(span)
        }

        log.scrollTop = log.scrollHeight
      }
    }
  }
}

export class SharedWorkerInfo {
  id = null
  port = null
  client = null
  scriptURL = null

  url = null
  hash = null

  constructor (data) {
    for (const key in data) {
      const value = data[key]
      if (key in this) {
        this[key] = value
      }
    }

    const url = new URL(this.scriptURL)
    this.url = url.toString()
    this.hash = this.id
  }

  get pathname () {
    return new URL(this.url).pathname
  }
}

function waitForWorkerInstallation (info, start) {
  return new Promise((resolve, reject) => {
    let settled = false
    const timeout = setTimeout(() => {
      const error = new Error(
        `SharedWorker (${info.pathname}) did not finish installing`
      )
      settle(reject, error)
      try {
        channel.postMessage({
          error: { id: info.id, message: error.message }
        })
      } catch {}
    }, WORKER_INSTALL_TIMEOUT)

    function settle (callback, value) {
      if (settled) {
        return
      }

      settled = true
      clearTimeout(timeout)
      channel.removeEventListener('message', onMessage)
      callback(value)
    }

    function onMessage (event) {
      if (event.data?.error?.id === info.id) {
        settle(reject, new Error(event.data.error.message))
      } else if (event.data?.installed?.id === info.id) {
        settle(resolve, null)
      }
    }

    channel.addEventListener('message', onMessage)

    try {
      start()
    } catch (err) {
      const error = err instanceof Error ? err : new Error(String(err))
      settle(reject, error)
      try {
        channel.postMessage({
          error: { id: info.id, message: error.message }
        })
      } catch {}
    }
  })
}

function ensureWorkerInstalled (info) {
  if (installations.has(info.hash)) {
    return installations.get(info.hash)
  }

  if (workers.has(info.hash)) {
    return Promise.resolve(workers.get(info.hash))
  }

  let worker = null
  const installation = waitForWorkerInstallation(info, () => {
    worker = new SharedWorkerInstance('./worker.js', {
      info
    })

    workers.set(info.hash, worker)
    globals.get('SharedWorkerContext.info').set(info.hash, info)
    worker.postMessage({ install: info })
  })
    .then(() => worker)
    .catch((err) => {
      if (workers.get(info.hash) === worker) {
        workers.delete(info.hash)
      }

      globals.get('SharedWorkerContext.info').delete(info.hash)
      try {
        worker?.terminate()
      } catch {}
      throw err
    })
    .finally(() => {
      if (installations.get(info.hash) === installation) {
        installations.delete(info.hash)
      }
    })

  installations.set(info.hash, installation)
  return installation
}

export async function onInstall (event) {
  const info = new SharedWorkerInfo(event.data.install)

  if (info.id === null || info.id === undefined) {
    return
  }

  await ensureWorkerInstalled(info)
}

export async function onUninstall (event) {
  const info = new SharedWorkerInfo(event.data.uninstall)

  if (installations.has(info.hash)) {
    try {
      await installations.get(info.hash)
    } catch {
      return
    }
  }

  if (!workers.has(info.hash)) {
    return
  }

  const worker = workers.get(info.hash)
  workers.delete(info.hash)
  globals.get('SharedWorkerContext.info').delete(info.hash)

  worker.postMessage({ uninstall: info })
}

export async function onConnect (event) {
  const info = new SharedWorkerInfo(event.data.connect)

  if (info.id === null || info.id === undefined) {
    return
  }

  const worker = await ensureWorkerInstalled(info)
  worker.postMessage({ connect: info })
}

export default null
