/**
 * Add an application event `type` callback `listener` with `options`.
 * @param {string} type
 * @param {function(Event|MessageEvent|CustomEvent|ApplicationURLEvent): boolean} listener
 * @param {{ once?: boolean }|boolean=} [options]
 */
export function addEventListener(type: string, listener: (arg0: Event | MessageEvent | CustomEvent | ApplicationURLEvent) => boolean, options?: ({
    once?: boolean;
} | boolean) | undefined): void;
/**
 * Remove an application event `type` callback `listener` with `options`.
 * @param {string} type
 * @param {function(Event|MessageEvent|CustomEvent|ApplicationURLEvent): boolean} listener
 */
export function removeEventListener(type: string, listener: (arg0: Event | MessageEvent | CustomEvent | ApplicationURLEvent) => boolean): void;
/**
 * Returns the current window index
 * @return {number}
 */
export function getCurrentWindowIndex(): number;
/**
 * Creates a new window and returns an instance of ApplicationWindow.
 * @param {object} opts - an options object
 * @param {string=} opts.aspectRatio - a string (split on ':') provides two float values which set the window's aspect ratio.
 * @param {boolean=} opts.closable - deterime if the window can be closed.
 * @param {boolean=} opts.minimizable - deterime if the window can be minimized.
 * @param {boolean=} opts.maximizable - deterime if the window can be maximized.
 * @param {number} [opts.margin] - a margin around the webview. (Private)
 * @param {number} [opts.radius] - a radius on the webview. (Private)
 * @param {number=} [opts.index = -1] - the index of the window, if not provided or the value is `-1`, then one will be assigned
 * @param {string} opts.path - the path to the HTML file to load into the window.
 * @param {string=} opts.title - the title of the window.
 * @param {string=} opts.titlebarStyle - determines the style of the titlebar (MacOS only).
 * @param {string=} opts.windowControlOffsets - a string (split on 'x') provides the x and y position of the traffic lights (MacOS only).
 * @param {string=} opts.backgroundColorDark - determines the background color of the window in dark mode.
 * @param {string=} opts.backgroundColorLight - determines the background color of the window in light mode.
 * @param {boolean=} opts.followSystemTheme - whether the window should follow the desktop theme (default: true).
 * @param {boolean=} opts.preferDarkTheme - whether the window should prefer a dark theme when not following the system theme.
 * @param {(number|string)=} opts.width - the width of the window. If undefined, the window will have the main window width.
 * @param {(number|string)=} opts.height - the height of the window. If undefined, the window will have the main window height.
 * @param {(number|string)=} [opts.minWidth = 0] - the minimum width of the window
 * @param {(number|string)=} [opts.minHeight = 0] - the minimum height of the window
 * @param {(number|string)=} [opts.maxWidth = '100%'] - the maximum width of the window
 * @param {(number|string)=} [opts.maxHeight = '100%'] - the maximum height of the window
 * @param {boolean=} [opts.resizable=true] - whether the window is resizable
 * @param {boolean=} [opts.frameless=false] - whether the window is frameless
 * @param {boolean=} [opts.utility=false] - whether the window is utility (macOS only)
 * @param {boolean=} [opts.shouldExitApplicationOnClose=false] - whether the window can exit the app
 * @param {boolean=} [opts.headless=false] - whether the window will be headless or not (no frame)
 * @param {string=} [opts.userScript=null] - A user script that will be injected into the window (desktop only)
 * @param {string[]=} [opts.protocolHandlers] - An array of protocol handler schemes to register with the new window (requires service worker)
 * @param {Record<string, string|number|boolean|(string|number|boolean)[]>=} [opts.config] - additional configuration key/value pairs
 * @param {string=} [opts.resourcesDirectory]
 * @param {boolean=} [opts.shouldPreferServiceWorker=false]
 * @return {Promise<ApplicationWindow>}
 */
export function createWindow(opts: {
    aspectRatio?: string | undefined;
    closable?: boolean | undefined;
    minimizable?: boolean | undefined;
    maximizable?: boolean | undefined;
    margin?: number;
    radius?: number;
    index?: number | undefined;
    path: string;
    title?: string | undefined;
    titlebarStyle?: string | undefined;
    windowControlOffsets?: string | undefined;
    backgroundColorDark?: string | undefined;
    backgroundColorLight?: string | undefined;
    followSystemTheme?: boolean | undefined;
    preferDarkTheme?: boolean | undefined;
    width?: (number | string) | undefined;
    height?: (number | string) | undefined;
    minWidth?: (number | string) | undefined;
    minHeight?: (number | string) | undefined;
    maxWidth?: (number | string) | undefined;
    maxHeight?: (number | string) | undefined;
    resizable?: boolean | undefined;
    frameless?: boolean | undefined;
    utility?: boolean | undefined;
    shouldExitApplicationOnClose?: boolean | undefined;
    headless?: boolean | undefined;
    userScript?: string | undefined;
    protocolHandlers?: string[] | undefined;
    config?: Record<string, string | number | boolean | (string | number | boolean)[]> | undefined;
    resourcesDirectory?: string | undefined;
    shouldPreferServiceWorker?: boolean | undefined;
}): Promise<ApplicationWindow>;
/**
 * Returns the current screen size.
 * @returns {Promise<{ width: number, height: number }>}
 */
export function getScreenSize(): Promise<{
    width: number;
    height: number;
}>;
/**
 * Returns the ApplicationWindow instances for the given indices or all windows if no indices are provided.
 * @param {number[]} [indices] - the indices of the windows
 * @param {ApplicationWindowQueryOptions=} [options]
 * @throws {Error} - if indices is not an array of integer numbers
 * @return {Promise<ApplicationWindowList>}
 */
export function getWindows(indices?: number[], options?: ApplicationWindowQueryOptions | undefined): Promise<ApplicationWindowList>;
/**
 * Returns the ApplicationWindow instance for the given index
 * @param {number} index - the index of the window
 * @param {ApplicationWindowQueryOptions=} [options]
 * @throws {Error} - if index is not a valid integer number
 * @returns {Promise<ApplicationWindow|undefined>} - the ApplicationWindow instance or `undefined` if the window does not exist
 */
export function getWindow(index: number, options?: ApplicationWindowQueryOptions | undefined): Promise<ApplicationWindow | undefined>;
/**
 * Returns the ApplicationWindow instance for the current window.
 * @return {Promise<ApplicationWindow>}
 */
export function getCurrentWindow(): Promise<ApplicationWindow>;
/**
 * Quits the backend process and then quits the render process, the exit code used is the final exit code to the OS.
 * @param {number} [code = 0] - an exit code
 * @return {Promise<ipc.Result['data']>}
 */
export function exit(code?: number): Promise<ipc.Result["data"]>;
/**
 * Set the native menu for the app.
 *
 * @param {ApplicationMenuOptions} options - an options object
 * @return {Promise<ipc.Result>}
 *
 * Oro Runtime provides a minimalist DSL that makes it easy to create cross
 * platform native system and context menus.
 *
 * Menus are created at run time. They can be created from either the Main or
 * Render process. The can be recreated instantly by calling the `setSystemMenu` method.
 *
 * The method takes a string. Here's an example of a menu. The semi colon is
 * significant indicates the end of the menu. Use an underscore when there is no
 * accelerator key. Modifiers are optional. And well known OS menu options like
 * the edit menu will automatically get accelerators you dont need to specify them.
 *
 *
 * ```js
 * oro.application.setSystemMenu({ index: 0, value: `
 *   App:
 *     Foo: f;
 *
 *   Edit:
 *     Cut: x
 *     Copy: c
 *     Paste: v
 *     Delete: _
 *     Select All: a;
 *
 *   Other:
 *     Apple: _
 *     Another Test: T
 *     !Im Disabled: I
 *     Some Thing: S + Meta
 *     ---
 *     Bazz: s + Meta, Control, Alt;
 * `)
 * ```
 *
 * Separators
 *
 * To create a separator, use three dashes `---`.
 *
 *
 * Accelerator Modifiers
 *
 * Accelerator modifiers are used as visual indicators but don't have a
 * material impact as the actual key binding is done in the event listener.
 *
 * A capital letter implies that the accelerator is modified by the `Shift` key.
 *
 * Additional accelerators are `Meta`, `Control`, `Option`, each separated
 * by commas. If one is not applicable for a platform, it will just be ignored.
 *
 * On MacOS `Meta` is the same as `Command`.
 *
 *
 * Disabled Items
 *
 * If you want to disable a menu item just prefix the item with the `!` character.
 * This will cause the item to appear disabled when the system menu renders.
 *
 *
 * Submenus
 *
 * We feel like nested menus are an anti-pattern. We don't use them. If you have a
 * strong argument for them and a very simple pull request that makes them work we
 * may consider them.
 *
 *
 * Event Handling
 *
 * When a menu item is activated, it raises the `menuItemSelected` event in
 * the front end code, you can then communicate with your backend code if you
 * want from there.
 *
 * For example, if the `Apple` item is selected from the `Other` menu...
 *
 * ```js
 * window.addEventListener('menuItemSelected', event => {
 *   assert(event.detail.parent === 'Other')
 *   assert(event.detail.title === 'Apple')
 * })
 * ```
 *
 */
export function setSystemMenu(options: ApplicationMenuOptions): Promise<ipc.Result>;
/**
 * An alias to `setSystemMenu()` for creating a tray menu.
 * @param {ApplicationMenuOptions} options - an options object
 * @return {Promise<ipc.Result>}
 */
export function setTrayMenu(options: ApplicationMenuOptions): Promise<ipc.Result>;
/**
 * Set the enabled state of the system menu.
 * @param {object} value - an options object
 * @return {Promise<ipc.Result>}
 */
export function setSystemMenuItemEnabled(value: object): Promise<ipc.Result>;
/**
 * Predicate function to determine if application is in a "paused" state.
 * @return {boolean}
 */
export function isPaused(): boolean;
/**
 * Options for `getWindows()` and `getWindow()`.
 * @typedef {object} ApplicationWindowQueryOptions
 * @property {number|false} [max=MAX_WINDOWS] Maximum window index to hydrate from
 * the native response. Pass `false` to disable the cap and include all windows.
 */
/**
 * Options for `setSystemMenu()` and `setTrayMenu()`.
 * @typedef {object} ApplicationMenuOptions
 * @property {string} value - Menu layout expressed with the native menu DSL.
 * @property {number=} [index] - Window index to target when the menu is
 * window-scoped on the active platform.
 */
/**
 * Maximum number of concurrently tracked application windows.
 *
 * The runtime currently caps window indices at this value when enumerating
 * or creating windows through the high-level application APIs.
 * @type {64}
 */
export const MAX_WINDOWS: 64;
/**
 * Ordered collection of `ApplicationWindow` instances keyed by window index.
 *
 * The list is iterable, preserves ascending window-index order, and also
 * exposes each window at `list[window.index]` for direct indexed lookup.
 */
export class ApplicationWindowList {
    /**
     * Creates a window list from either a single array or variadic window
     * arguments.
     * @param {...ApplicationWindow|ApplicationWindow[]} args
     * @returns {ApplicationWindowList}
     */
    static from(...args: (ApplicationWindow | ApplicationWindow[])[]): ApplicationWindowList;
    /**
     * @param {ApplicationWindow[]=} [items]
     */
    constructor(items?: ApplicationWindow[] | undefined);
    /**
     * Number of windows currently stored in the list.
     * @returns {number}
     */
    get length(): number;
    /**
     * Alias for `length`.
     * @returns {number}
     */
    get size(): number;
    /**
     * Invokes `callback` once for each window in the list.
     * @param {(window: ApplicationWindow, index: number, list: ApplicationWindow[]) => void} callback
     * @param {any=} [thisArg]
     */
    forEach(callback: (window: ApplicationWindow, index: number, list: ApplicationWindow[]) => void, thisArg?: any | undefined): void;
    /**
     * Returns the window stored at `index`, if present.
     * @param {number} index
     * @returns {ApplicationWindow|undefined}
     */
    item(index: number): ApplicationWindow | undefined;
    /**
     * Returns `[window.index, window]` pairs for the current list contents.
     * @returns {Array<[number, ApplicationWindow]>}
     */
    entries(): Array<[number, ApplicationWindow]>;
    /**
     * Returns the ordered window indices contained in the list.
     * @returns {number[]}
     */
    keys(): number[];
    /**
     * Returns the ordered window instances contained in the list.
     * @returns {ApplicationWindow[]}
     */
    values(): ApplicationWindow[];
    /**
     * Inserts or replaces a window in the list using its `window.index`.
     * @param {ApplicationWindow} window
     * @returns {ApplicationWindowList}
     */
    add(window: ApplicationWindow): ApplicationWindowList;
    /**
     * Removes a window from the list by instance or numeric index.
     * @param {ApplicationWindow|number} windowOrIndex
     * @returns {boolean}
     */
    remove(windowOrIndex: ApplicationWindow | number): boolean;
    /**
     * Returns `true` when the list contains a window for the given instance or
     * numeric index.
     * @param {ApplicationWindow|number} windowOrIndex
     * @returns {boolean}
     */
    contains(windowOrIndex: ApplicationWindow | number): boolean;
    /**
     * Removes all windows from the list.
     * @returns {ApplicationWindowList}
     */
    clear(): ApplicationWindowList;
    /**
     * Iterates over windows in ascending index order.
     * @returns {IterableIterator<ApplicationWindow>}
     */
    [Symbol.iterator](): IterableIterator<ApplicationWindow>;
    #private;
}
/**
 * Oro Runtime semantic version metadata mirrored from `process.versions.oro`.
 * The legacy `process.versions.socket` string remains frozen for compatibility.
 * @type {object} - an object containing the version information
 */
export const runtimeVersion: object;
/**
 * Runtime debug flag.
 * @type {boolean}
 */
export const debug: boolean;
/**
 * Application configuration.
 * @type {Record<string, string|number|boolean|(string|number|boolean)[]>}
 */
export const config: Record<string, string | number | boolean | (string | number | boolean)[]>;
export namespace backend {
    /**
     * @param {object} opts - an options object
     * @param {boolean} [opts.force = false] - whether to force the existing process to close
     * @return {Promise<ipc.Result>}
     */
    function open(opts?: {
        force?: boolean;
    }): Promise<ipc.Result>;
    /**
     * @return {Promise<ipc.Result>}
     */
    function close(): Promise<ipc.Result>;
}
export default exports;
/**
 * Options for `getWindows()` and `getWindow()`.
 */
export type ApplicationWindowQueryOptions = {
    /**
     * Maximum window index to hydrate from
     * the native response. Pass `false` to disable the cap and include all windows.
     */
    max?: number | false;
};
/**
 * Options for `setSystemMenu()` and `setTrayMenu()`.
 */
export type ApplicationMenuOptions = {
    /**
     * - Menu layout expressed with the native menu DSL.
     */
    value: string;
    /**
     * - Window index to target when the menu is
     * window-scoped on the active platform.
     */
    index?: number | undefined;
};
import { ApplicationURLEvent } from './internal/events.js';
import ApplicationWindow from './window.js';
import ipc from './ipc.js';
import client from './application/client.js';
import menu from './application/menu.js';
import * as exports from './application.js';
export { client, menu };
