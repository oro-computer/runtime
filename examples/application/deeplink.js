// Application URL helpers for deep-linking demos.

import { onApplicationURL } from 'oro:hooks'

function defaultLog (message) {
  console.log(`[applicationurl] ${message}`)
}

export function createApplicationUrlListener ({
  log = defaultLog,
  onLink
} = {}) {
  const handler = (event) => {
    const url = event?.url
    log(`received: ${url}`)
    let parsed = null
    if (url) {
      try {
        parsed = new URL(url)
        if (parsed.pathname === '/open' && parsed.searchParams.get('file')) {
          log(`open file: ${parsed.searchParams.get('file')}`)
        }
      } catch (error) {
        log(`parse error: ${error?.message || error}`)
      }
    }
    if (onLink) {
      onLink({ url, parsed })
    }
  }

  const dispose = onApplicationURL(handler)
  return { dispose }
}

const defaultListener = createApplicationUrlListener()

export function demo () {
  return defaultListener
}

export default demo
