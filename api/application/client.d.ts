/**
 * @typedef {{
 *  id?: string | null,
 *  type?: 'window' | 'worker',
 *  parent?: object | null,
 *  top?: object | null,
 *  frameType?: 'top-level' | 'nested' | 'none'
 * }} ClientState
 */
export class Client {
    /**
     * `Client` class constructor
     * @private
     * @param {ClientState} state
     */
    private constructor();
    /**
     * The unique ID of the client.
     * @type {string|null}
     */
    get id(): string | null;
    /**
     * The frame type of the client.
     * @type {'top-level'|'nested'|'none'}
     */
    get frameType(): "top-level" | "nested" | "none";
    /**
     * The type of the client.
     * @type {'window'|'worker'}
     */
    get type(): "window" | "worker";
    /**
     * The parent client of the client.
     * @type {Client|null}
     */
    get parent(): Client | null;
    /**
     * The top client of the client.
     * @type {Client|null}
     */
    get top(): Client | null;
    /**
     * A readonly `URL` of the current location of this client.
     * @type {URL}
     */
    get location(): URL;
    /**
     * Converts this `Client` instance to JSON.
     * @return {object}
     */
    toJSON(): object;
    #private;
}
declare const _default: any;
export default _default;
export type ClientState = {
    id?: string | null;
    type?: "window" | "worker";
    parent?: object | null;
    top?: object | null;
    frameType?: "top-level" | "nested" | "none";
};
