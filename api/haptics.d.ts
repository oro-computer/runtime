/**
 * @typedef {object} HapticOptions
 * @property {boolean} [enabled=false] The application's saved user preference.
 */
/**
 * Requests one feedback pulse. Android respects system/view preferences and
 * suppresses pulses less than 100 ms apart. `true` means accepted by the view,
 * not independently measured physical feedback. Other platforms reject `ENOSYS`.
 * @param {'selection'|'confirmation'|'rejection'} kind
 * @param {HapticOptions} [options]
 * @returns {Promise<boolean>}
 */
export function perform(kind: "selection" | "confirmation" | "rejection", options?: HapticOptions): Promise<boolean>;
declare namespace _default {
    export { perform };
}
export default _default;
export type HapticOptions = {
    /**
     * The application's saved user preference.
     */
    enabled?: boolean;
};
