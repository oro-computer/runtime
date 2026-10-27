/**
 * @module window/appearance
 * Android insets, predictive Back and bounded edge gesture requests for the current document.
 */

import request from '../internal/platform-request.js'

/**
 * @typedef {object} GestureExclusionRegion
 * @property {'left'|'right'} edge Visible handle's edge in the native WebView.
 * @property {number} top Fraction of native client height in [0, 1).
 * @property {number} height Positive fraction of native client height; top + height must be <= 1.
 */

/**
 * @typedef {object} GestureExclusionRect
 * @property {number} left Native view pixels.
 * @property {number} top Native view pixels.
 * @property {number} right Native view pixels.
 * @property {number} bottom Native view pixels.
 */

/**
 * @typedef {object} GestureExclusionState
 * @property {boolean} supported Android 10+ supports requesting exclusion.
 * @property {boolean} active This document owns a current native request, not proof that Android accepted it.
 * @property {number} viewportWidth Native WebView width in pixels.
 * @property {number} viewportHeight Native WebView height in pixels.
 * @property {number} density Native display density.
 * @property {number} maximumRegions Four regions in total.
 * @property {number} maximumWidthDp Fixed maximum edge-strip width: 24dp, also capped at a quarter of the viewport.
 * @property {number} maximumEdgeHeightDp Maximum combined height per edge: 200dp, also capped at half the viewport.
 * @property {boolean} osAcceptanceKnown Always false; Android owns the effective gesture policy.
 * @property {GestureExclusionRect[]} rects Registered, clipped rectangles in native view pixels.
 */

/**
 * Requests system-gesture exclusion for up to four small visible edge handles
 * in the current trusted foreground document. Fractions refer to native client
 * height, so app code must map its handle geometry after layout. Android retains
 * mandatory navigation gestures and may further restrict or ignore a request.
 * Empty regions clear this document's request. Navigation, pause, hiding, focus
 * loss, detachment or native geometry/configuration changes clear it; returning
 * to the app never restores it automatically. Reapply only for a visible handle.
 * Oversized requests reject without replacing the existing request. Other
 * platforms reject with ENOSYS; nonempty requests below Android 10 use ENOTSUP.
 * @param {GestureExclusionRegion[]} regions
 * @returns {Promise<GestureExclusionState>}
 */
export async function setGestureExclusion (regions) {
  if (!Array.isArray(regions) || regions.length > 4) throw new TypeError('At most four edge regions are supported')
  const normalized = regions.map(region => {
    if (!region || typeof region !== 'object' || Array.isArray(region) ||
        Object.keys(region).sort().join(',') !== 'edge,height,top' ||
        !['left', 'right'].includes(region.edge) || !Number.isFinite(region.top) || !Number.isFinite(region.height) ||
        region.top < 0 || region.height <= 0 || region.top + region.height > 1) {
      throw new TypeError('An edge region requires edge, top and height fractions')
    }
    return { edge: region.edge, top: region.top, height: region.height }
  })
  return request('gestures', 'set', { regions: normalized })
}

/**
 * Reads this document's exclusion request without renewing its ownership.
 * Rectangles describe registered view regions, not OS acceptance or a gesture
 * navigation mode. App drawer/focus/animation behavior remains application code.
 * @returns {Promise<GestureExclusionState>}
 */
export async function getGestureExclusion () {
  return request('gestures', 'get', {})
}

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

export default { onInsetsChange, onBackGesture, setGestureExclusion, getGestureExclusion }
