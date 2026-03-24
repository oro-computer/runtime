import { ErrnoError } from './errors.js'

/**
 * Returns an `ErrnoError` rejecting promise for unimplemented handlers.
 * @param {string} method
 * @returns {Promise<never>}
 */
function rejectNotImplemented (method) {
  return Promise.reject(
    new ErrnoError('ENOSYS', `background.${method} is not implemented yet`)
  )
}

const background = {
  available: false,
  register () {
    return rejectNotImplemented('register')
  },
  schedule () {
    return rejectNotImplemented('schedule')
  },
  cancel () {
    return rejectNotImplemented('cancel')
  },
  status () {
    return rejectNotImplemented('status')
  }
}

export { background }
export default background
