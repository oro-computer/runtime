import { PRIMARY_SCHEME, runtimeOrigin } from './internal/runtime-schemes.js'

const toRuntimeURL = (value) => {
  const bundleIdentifier = globalThis.__args?.config?.meta_bundle_identifier
  if (!bundleIdentifier || typeof value !== 'string' || value.length === 0) {
    return value
  }

  try {
    const url = new URL(value)
    if (url.hostname !== bundleIdentifier) {
      return value
    }

    if (url.protocol === 'http:' || url.protocol === 'https:') {
      return `${runtimeOrigin(bundleIdentifier)}${url.pathname}${url.search}${url.hash}`
    }
  } catch {}

  return value
}

const originFromURL = (url) => {
  if (!url) {
    return ''
  }

  if (url.origin && url.origin !== 'null') {
    return url.origin
  }

  if (url.protocol && url.host) {
    return `${url.protocol}//${url.host}`
  }

  return ''
}

const normalizeOrigin = (value) => {
  if (typeof value !== 'string' || value.length === 0 || value === 'null') {
    return value
  }

  try {
    const url = new URL(value)

    if (url.protocol === 'blob:' && url.pathname) {
      const embedded = new URL(url.pathname)

      if (embedded.origin && embedded.origin !== 'null') {
        return `blob:${embedded.origin}`
      }

      if (embedded.protocol && embedded.host) {
        return `blob:${embedded.protocol}//${embedded.host}`
      }

      return value
    }

    if (url.origin && url.origin !== 'null') {
      return url.origin
    }

    if (url.protocol && url.host) {
      return `${url.protocol}//${url.host}`
    }
  } catch {}

  return value
}

export class Location {
  get url () {
    let url = null

    // XXX(@jwerle): should never be true...
    // @ts-ignore
    if (globalThis.location === this) {
      return url
    }

    if (globalThis.location.href.startsWith('blob:')) {
      url = new URL(
        globalThis.RUNTIME_WORKER_LOCATION ||
          globalThis.location.pathname +
            globalThis.location.search +
            globalThis.location.hash
      )
    } else if (globalThis.location.origin === 'null') {
      try {
        url = new URL(
          globalThis.location.pathname +
            globalThis.location.search +
            globalThis.location.hash,
          globalThis.__args?.config?.meta_bundle_identifier ?? 'null'
        )
      } catch {}
    } else if (
      globalThis.location.hostname ===
      globalThis.__args?.config?.meta_bundle_identifier
    ) {
      url = new URL(globalThis.location.href)
    } else if (globalThis.__args.client.host === globalThis.location.hostname) {
      url = new URL(globalThis.location.href)
    } else if (globalThis.top !== globalThis) {
      return new URL(globalThis.location.href)
    }

    if (
      !url ||
      url.hostname !== globalThis.__args?.config?.meta_bundle_identifier
    ) {
      if (globalThis.__args?.config?.meta_bundle_identifier) {
        if (globalThis.__args.config.platform === 'android') {
          url = new URL(
            `https://${globalThis.__args.config.meta_bundle_identifier}`
          )
        } else {
          url = new URL(
            runtimeOrigin(globalThis.__args.config.meta_bundle_identifier, {
              scheme: PRIMARY_SCHEME
            })
          )
        }
      }
    }

    return url
  }

  get protocol () {
    return `${PRIMARY_SCHEME}:`
  }

  get host () {
    return this.url.host
  }

  get hostname () {
    return this.url.hostname
  }

  get port () {
    return this.url.port
  }

  get pathname () {
    return this.url.pathname
  }

  get search () {
    return this.url.search
  }

  get origin () {
    const bundleIdentifier = globalThis.__args?.config?.meta_bundle_identifier

    let origin =
      originFromURL(this.url) ||
      globalThis.origin ||
      globalThis.location?.origin ||
      'null'

    origin = normalizeOrigin(toRuntimeURL(origin))

    if (
      bundleIdentifier &&
      (origin === '' ||
        origin === 'null' ||
        origin === `${PRIMARY_SCHEME}:/` ||
        origin === `${PRIMARY_SCHEME}:`)
    ) {
      origin = runtimeOrigin(bundleIdentifier, { scheme: PRIMARY_SCHEME })
    }

    return origin
  }

  get href () {
    return toRuntimeURL(this.url.href)
  }

  get hash () {
    return this.url.hash
  }

  toString () {
    return this.href
  }
}

export default new Location()
