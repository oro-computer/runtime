/**
 * An event dispatched when an application URL is opening the application.
 */
export class ApplicationURLEvent extends Event {
    /**
     * `ApplicationURLEvent` class constructor.
     * @param {string=} [type]
     * @param {object=} [options]
     */
    constructor(type?: string | undefined, options?: object | undefined);
    /**
     * `true` if the application URL is valid (parses correctly).
     * @type {boolean}
     */
    get isValid(): boolean;
    /**
     * Data associated with the `ApplicationURLEvent`.
     * @type {?any}
     */
    get data(): any | null;
    /**
     * The original source URI
     * @type {?string}
     */
    get source(): string | null;
    /**
     * The `URL` for the `ApplicationURLEvent`.
     * @type {?URL}
     */
    get url(): URL | null;
    /**
     * String tag name for an `ApplicationURLEvent` instance.
     * @type {string}
     */
    get [Symbol.toStringTag](): string;
    #private;
}
/**
 * An event dispacted for a registered global hotkey expression.
 */
export class HotKeyEvent extends MessageEvent<any> {
    /**
     * `HotKeyEvent` class constructor.
     * @ignore
     * @param {string=} [type]
     * @param {object=} [data]
     */
    constructor(type?: string | undefined, data?: object | undefined);
    /**
     * The global unique ID for this hotkey binding.
     * @type {number?}
     */
    get id(): number | null;
    /**
     * The computed hash for this hotkey binding.
     * @type {number?}
     */
    get hash(): number | null;
    /**
     * The normalized hotkey expression as a sequence of tokens.
     * @type {string[]}
     */
    get sequence(): string[];
    /**
     * The original expression of the hotkey binding.
     * @type {string?}
     */
    get expression(): string | null;
}
/**
 * An event dispacted when a menu item is selected.
 */
export class MenuItemEvent extends MessageEvent<any> {
    /**
     * `MenuItemEvent` class constructor
     * @ignore
     * @param {string=} [type]
     * @param {object=} [data]
     * @param {import('../application/menu.js').Menu} menu
     */
    constructor(type?: string | undefined, data?: object | undefined, menu?: import("../application/menu.js").Menu);
    /**
     * The `Menu` this event has been dispatched for.
     * @type {import('../application/menu.js').Menu?}
     */
    get menu(): import("../application/menu.js").Menu | null;
    /**
     * The title of the menu item.
     * @type {string?}
     */
    get title(): string | null;
    /**
     * An optional tag value for the menu item that may also be the
     * parent menu item title.
     * @type {string?}
     */
    get tag(): string | null;
    /**
     * The parent title of the menu item.
     * @type {string?}
     */
    get parent(): string | null;
    #private;
}
/**
 * An event dispacted when the application receives an OS signal
 */
export class SignalEvent extends MessageEvent<any> {
    /**
     * `SignalEvent` class constructor
     * @ignore
     * @param {string=} [type]
     * @param {object=} [options]
     */
    constructor(type?: string | undefined, options?: object | undefined);
    /**
     * The code of the signal.
     * @type {import('../process/signal.js').signal}
     */
    get code(): import("../process/signal.js").signal;
    /**
     * The name of the signal.
     * @type {string}
     */
    get name(): string;
    /**
     * An optional message describing the signal
     * @type {string}
     */
    get message(): string;
    #private;
}
declare namespace _default {
    export { ApplicationURLEvent };
    export { MenuItemEvent };
    export { SignalEvent };
    export { HotKeyEvent };
}
export default _default;
