/**
 * @module cookies
 * WebView cookie helpers backed by the native platform cookie store.
 */

import ipc from './ipc.js'

/**
 * Get cookies for a URL as a `Cookie` header value ("a=b; c=d").
 *
 * @param {string} url
 * @returns {Promise<{ value: string }>}
 */
export async function get (url) {
  if (typeof url !== 'string' || url.trim().length === 0) {
    throw new TypeError('url must be a non-empty string')
  }

  const { data, err } = await ipc.request('cookies.get', { url })

  if (err) {
    const error = new Error(err?.message || 'Failed to get cookies')
    error.code = err?.code || 'COOKIE_ERROR'
    throw error
  }

  return data
}

/**
 * Set a cookie for a URL from a `Set-Cookie` header value.
 *
 * @param {string} url
 * @param {string} cookie - a Set-Cookie header value
 * @returns {Promise<{ ok: boolean }>}
 */
export async function set (url, cookie) {
  if (typeof url !== 'string' || url.trim().length === 0) {
    throw new TypeError('url must be a non-empty string')
  }

  if (typeof cookie !== 'string' || cookie.trim().length === 0) {
    throw new TypeError('cookie must be a non-empty string')
  }

  const { data, err } = await ipc.request('cookies.set', { url, cookie })

  if (err) {
    const error = new Error(err?.message || 'Failed to set cookie')
    error.code = err?.code || 'COOKIE_ERROR'
    throw error
  }

  return data
}

/**
 * Remove cookies matching `name` for a URL.
 *
 * @param {string} url
 * @param {string} name
 * @returns {Promise<{ ok: boolean }>}
 */
export async function remove (url, name) {
  if (typeof url !== 'string' || url.trim().length === 0) {
    throw new TypeError('url must be a non-empty string')
  }

  if (typeof name !== 'string' || name.trim().length === 0) {
    throw new TypeError('name must be a non-empty string')
  }

  const { data, err } = await ipc.request('cookies.remove', { url, name })

  if (err) {
    const error = new Error(err?.message || 'Failed to remove cookie')
    error.code = err?.code || 'COOKIE_ERROR'
    throw error
  }

  return data
}

/**
 * Clear all cookies in the current WebView data store.
 *
 * @returns {Promise<{ ok: boolean }>}
 */
export async function clear () {
  const { data, err } = await ipc.request('cookies.clear', {})

  if (err) {
    const error = new Error(err?.message || 'Failed to clear cookies')
    error.code = err?.code || 'COOKIE_ERROR'
    throw error
  }

  return data
}

export default {
  get,
  set,
  remove,
  clear
}
