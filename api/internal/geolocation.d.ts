/**
 * Get the current position of the device.
 * @param {function(GeolocationPosition)} onSuccess
 * @param {onError(Error)} onError
 * @param {object=} options
 * @param {number=} options.timeout
 * @return {Promise}
 */
export function getCurrentPosition(onSuccess: (arg0: GeolocationPosition) => any, onError: any, options?: object | undefined, ...args: any[]): Promise<any>;
/**
 * Register a handler function that will be called automatically each time the
 * position of the device changes. You can also, optionally, specify an error
 * handling callback function.
 * @param {function(GeolocationPosition)} onSuccess
 * @param {function(Error)} onError
 * @param {object=} [options]
 * @param {number=} [options.timeout = null]
 * @return {number}
 */
export function watchPosition(onSuccess: (arg0: GeolocationPosition) => any, onError: (arg0: Error) => any, options?: object | undefined, ...args: any[]): number;
/**
 * Unregister location and error monitoring handlers previously installed
 * using `watchPosition`.
 * @param {number} id
 */
export function clearWatch(id: number, ...args: any[]): any;
export namespace platform {
    let getCurrentPosition: Function;
    let watchPosition: Function;
    let clearWatch: Function;
}
declare namespace _default {
    export { getCurrentPosition };
    export { watchPosition };
    export { clearWatch };
}
export default _default;
