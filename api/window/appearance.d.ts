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
export function setGestureExclusion(regions: GestureExclusionRegion[]): Promise<GestureExclusionState>;
/**
 * Reads this document's exclusion request without renewing its ownership.
 * Rectangles describe registered view regions, not OS acceptance or a gesture
 * navigation mode. App drawer/focus/animation behavior remains application code.
 * @returns {Promise<GestureExclusionState>}
 */
export function getGestureExclusion(): Promise<GestureExclusionState>;
/**
 * Observes changes after `ApplicationWindow.setSystemAppearance()` selects an
 * inset owner. An `end` event also covers interrupted platform IME animations;
 * use the reported inset values as the current state, not the target fraction.
 * @param {(insets: WindowInsets) => void} callback
 * @returns {() => void} Unsubscribe.
 */
export function onInsetsChange(callback: (insets: WindowInsets) => void): () => void;
/**
 * Observes the runtime activity's Back callback. Progress requires Android 14+.
 * Start/progress/cancel never navigate; commit follows native history/activity
 * handling. This observation does not intercept Back or replace an application's
 * modal/drawer dismissal priority. Custom native callbacks must forward events.
 * @param {(gesture: BackGesture) => void} callback
 * @returns {() => void} Unsubscribe.
 */
export function onBackGesture(callback: (gesture: BackGesture) => void): () => void;
declare namespace _default {
    export { onInsetsChange };
    export { onBackGesture };
    export { setGestureExclusion };
    export { getGestureExclusion };
}
export default _default;
export type GestureExclusionRegion = {
    /**
     * Visible handle's edge in the native WebView.
     */
    edge: "left" | "right";
    /**
     * Fraction of native client height in [0, 1).
     */
    top: number;
    /**
     * Positive fraction of native client height; top + height must be <= 1.
     */
    height: number;
};
export type GestureExclusionRect = {
    /**
     * Native view pixels.
     */
    left: number;
    /**
     * Native view pixels.
     */
    top: number;
    /**
     * Native view pixels.
     */
    right: number;
    /**
     * Native view pixels.
     */
    bottom: number;
};
export type GestureExclusionState = {
    /**
     * Android 10+ supports requesting exclusion.
     */
    supported: boolean;
    /**
     * This document owns a current native request, not proof that Android accepted it.
     */
    active: boolean;
    /**
     * Native WebView width in pixels.
     */
    viewportWidth: number;
    /**
     * Native WebView height in pixels.
     */
    viewportHeight: number;
    /**
     * Native display density.
     */
    density: number;
    /**
     * Four regions in total.
     */
    maximumRegions: number;
    /**
     * Fixed maximum edge-strip width: 24dp, also capped at a quarter of the viewport.
     */
    maximumWidthDp: number;
    /**
     * Maximum combined height per edge: 200dp, also capped at half the viewport.
     */
    maximumEdgeHeightDp: number;
    /**
     * Always false; Android owns the effective gesture policy.
     */
    osAcceptanceKnown: boolean;
    /**
     * Registered, clipped rectangles in native view pixels.
     */
    rects: GestureExclusionRect[];
};
export type WindowInsets = {
    /**
     * Which layer consumes the insets.
     */
    ownership: "native" | "web";
    /**
     * IME animation or ordinary inset change.
     */
    phase: "change" | "start" | "progress" | "end";
    /**
     * Animation fraction, bounded to [0, 1].
     */
    progress: number;
    /**
     * CSS pixels, including cutouts.
     */
    left: number;
    /**
     * CSS pixels, including status bars.
     */
    top: number;
    /**
     * CSS pixels, including cutouts.
     */
    right: number;
    /**
     * Navigation-bar CSS pixels.
     */
    bottom: number;
    /**
     * Keyboard bottom inset in CSS pixels.
     */
    imeBottom: number;
    imeVisible: boolean;
};
export type BackGesture = {
    phase: "start" | "progress" | "cancel" | "commit";
    /**
     * Gesture fraction, bounded to [0, 1].
     */
    progress: number;
    /**
     * Android swipe edge; null for non-gesture Back.
     */
    edge: number | null;
};
