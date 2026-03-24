// Thin adapter that exposes a "oro:tcp" module surface.
// Internally, this re-exports the Node-like TCP implementation from
// oro:net. This is intentionally minimal to avoid duplicating logic.
import {
  createConnection as _createConnection,
  createServer as _createServer
} from './net.js'

export function connect (options, cb) {
  // Supports (port, host?, cb?) and ({ port, host? }, cb?) signatures.
  if (typeof options === 'number') {
    const port = options
    const host =
      arguments.length >= 2 && typeof arguments[1] === 'string'
        ? arguments[1]
        : '127.0.0.1'
    const fn =
      typeof cb === 'function'
        ? cb
        : typeof arguments[2] === 'function'
          ? arguments[2]
          : undefined
    return _createConnection({ port, host }, fn)
  }
  const { port, host = '127.0.0.1' } = options || {}
  return _createConnection({ port, host }, cb)
}

export const createServer = _createServer

export default { connect, createServer }
