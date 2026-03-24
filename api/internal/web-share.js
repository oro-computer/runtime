/* global File */
import application from '../application.js'
import { NotAllowedError, NotSupportedError } from '../errors.js'

const SUPPORTED_PLATFORMS = new Set(['ios', 'macos', 'visionos', 'android'])

function getRuntimePlatform () {
  const configPlatform = globalThis.__args?.config?.platform
  if (typeof configPlatform === 'string' && configPlatform.length > 0) {
    return configPlatform.toLowerCase()
  }

  const processPlatform = globalThis.process?.platform
  if (typeof processPlatform === 'string' && processPlatform.length > 0) {
    switch (processPlatform) {
      case 'darwin':
        return 'macos'
      case 'win32':
        return 'windows'
      default:
        return processPlatform.toLowerCase()
    }
  }

  return ''
}

function platformSupportsShare () {
  return SUPPORTED_PLATFORMS.has(getRuntimePlatform())
}

function ensureUserActivation () {
  if (globalThis.navigator?.userActivation?.isActive === false) {
    throw new NotAllowedError(
      'The request is not allowed by the user agent or the platform in the current context.'
    )
  }
}

function normalizeShareData (input = {}, { allowEmpty = false } = {}) {
  if (input == null) input = {}
  if (typeof input !== 'object') {
    throw new TypeError('ShareData must be an object')
  }

  let title
  let text
  let url
  const files = []
  let hasData = false

  if ('title' in input && input.title != null) {
    title = String(input.title)
    if (title.length > 0) {
      hasData = true
    } else {
      title = undefined
    }
  }

  if ('text' in input && input.text != null) {
    text = String(input.text)
    if (text.length > 0) {
      hasData = true
    } else {
      text = undefined
    }
  }

  if ('url' in input && input.url != null) {
    const value = String(input.url)
    if (value.length > 0) {
      try {
        const parsed = new URL(value, globalThis.location?.href)
        url = parsed.href
        hasData = true
      } catch {
        throw new TypeError('ShareData url must be a valid URL')
      }
    }
  }

  if ('files' in input) {
    if (!Array.isArray(input.files)) {
      throw new TypeError('ShareData.files must be an array of File objects')
    }

    for (const item of input.files) {
      if (item == null) continue
      if (typeof File === 'function' && !(item instanceof File)) {
        throw new TypeError('ShareData.files entries must be File objects')
      }
      files.push(item)
    }

    if (files.length > 0) {
      hasData = true
    }
  }

  if (!allowEmpty && !hasData) {
    throw new TypeError(
      'ShareData must include at least one of title, text, url, or files'
    )
  }

  return { title, text, url, files, hasData }
}

async function share (data = {}) {
  ensureUserActivation()

  const normalized = normalizeShareData(data)

  if (!platformSupportsShare()) {
    throw new NotSupportedError(
      'Web Share API is not available on this platform'
    )
  }

  if (normalized.files.length > 0) {
    throw new NotSupportedError(
      'Sharing files is not supported by this runtime yet'
    )
  }

  const currentWindow = await application.getCurrentWindow()

  if (!currentWindow || typeof currentWindow.share !== 'function') {
    throw new NotSupportedError('Sharing is not available for this window')
  }

  await currentWindow.share({
    title: normalized.title,
    text: normalized.text,
    url: normalized.url
  })
}

function canShare (data = {}) {
  if (!platformSupportsShare()) {
    return false
  }

  const normalized = normalizeShareData(data, { allowEmpty: true })

  if (!normalized.hasData) {
    return false
  }

  if (normalized.files.length > 0) {
    return false
  }

  return true
}

if (globalThis.navigator && typeof globalThis.navigator.share !== 'function') {
  Object.defineProperties(globalThis.navigator, {
    share: {
      configurable: true,
      enumerable: false,
      value: share
    },
    canShare: {
      configurable: true,
      enumerable: false,
      value: canShare
    }
  })
}

export default {
  share,
  canShare,
  normalizeShareData,
  platformSupportsShare
}
