/**
 * @module signal
 * @deprecated Use `oro:process/signal` instead.
 */

import signal from './process/signal.js'
export * from './process/signal.js'
export default signal

// XXX(@jwerle): we should probably use a standard `deprecated()` function
// like nodejs' `util.deprecate()` instead of `console.warn()`
console.warn(
  'The module "oro:signal" is deprecated. ' +
    'Please use "oro:process/signal" instead"'
)
