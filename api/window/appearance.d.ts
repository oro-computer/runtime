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
}
export default _default;
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
