/**
 * @module window/appearance
 * Android inset and predictive Back observations for the current window.
 */

/**
 * @typedef {object} WindowInsets
 * @property {'native'|'web'} ownership Which layer consumes the insets.
 * @property {'change'|'start'|'progress'|'end'} phase IME animation or ordinary inset change.
 * @property {number} progress Animation fraction, bounded to [0, 1].
 * @property {number} left CSS pixels, including cutouts.
 * @property {number} top CSS pixels, including status bars.
 * @property {number} right CSS pixels, including cutouts.
 * @property {number} bottom Navigation-bar CSS pixels.
 * @property {number} imeBottom Keyboard bottom inset in CSS pixels.
 * @property {boolean} imeVisible
 */

/**
 * @typedef {object} BackGesture
 * @property {'start'|'progress'|'cancel'|'commit'} phase
 * @property {number} progress Gesture fraction, bounded to [0, 1].
 * @property {number|null} edge Android swipe edge; null for non-gesture Back.
 */

function listen (event, callback) {
  if (typeof callback !== 'function') throw new TypeError('callback must be a function')
  const listener = event => callback(event.detail)
  globalThis.addEventListener(event, listener)
  return () => globalThis.removeEventListener(event, listener)
}

/**
 * Observes changes after `ApplicationWindow.setSystemAppearance()` selects an
 * inset owner. An `end` event also covers interrupted platform IME animations;
 * use the reported inset values as the current state, not the target fraction.
 * @param {(insets: WindowInsets) => void} callback
 * @returns {() => void} Unsubscribe.
 */
export function onInsetsChange (callback) {
  return listen('windowinsetschange', callback)
}

/**
 * Observes the runtime activity's Back callback. Progress requires Android 14+.
 * Start/progress/cancel never navigate; commit follows native history/activity
 * handling. This observation does not intercept Back or replace an application's
 * modal/drawer dismissal priority. Custom native callbacks must forward events.
 * @param {(gesture: BackGesture) => void} callback
 * @returns {() => void} Unsubscribe.
 */
export function onBackGesture (callback) {
  return listen('backgesture', callback)
}

export default { onInsetsChange, onBackGesture }
