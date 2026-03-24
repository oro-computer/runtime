export default api;
export type EventEmitter = import("../events.js").EventEmitter;
/**
 * @typedef {import('../events.js').EventEmitter} EventEmitter
 */
/**
 * Initializes and returns the network bus.
 *
 * @async
 * @function
 * @param {object} options - Configuration options for the network bus.
 * @param {object} events - A nodejs compatibe implementation of the events module.
 * @param {object} dgram - A nodejs compatible implementation of the dgram module.
 * @returns {Promise<EventEmitter>} - A promise that resolves to the initialized network bus.
 */
export function api(options: object, events: object, dgram: object): Promise<EventEmitter>;
