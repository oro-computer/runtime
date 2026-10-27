/**
 * @typedef {object} FullscreenState
 * @property {boolean} supported Android 11+ native presentation is supported.
 * @property {boolean} active Whether a window currently owns fullscreen.
 * @property {number|null} owner Current fullscreen window index.
 * @property {boolean} customView Whether a browser custom view is being shown.
 * @property {boolean} requiresManagedInsets Call setSystemAppearance before entry.
 * @property {boolean} pointerCapture False; native fullscreen does not capture a pointer.
 */
/**
 * @typedef {object} ContentProtectionState
 * @property {boolean} enabled Protection requested for this window.
 * @property {boolean} activityProtected Android currently protects the entire activity.
 */
/**
 * @typedef {object} ShareOptions
 * @property {string} [title]
 * @property {string} [text]
 * @property {string} [url]
 * @property {File[]} [files] Android only: up to 16 files, 8 MiB total.
 */
/**
 * @typedef {object} SystemAppearanceOptions
 * @property {'system'|'light'|'dark'} theme System bar icon contrast.
 * @property {string} color Opaque surface color, exactly `#rrggbb`.
 * @property {'native'|'web'} insets Explicit inset owner. Native pads content;
 * web emits CSS-pixel insets for the app to consume. Stop app-owned native inset
 * listeners before adopting this API to prevent double padding. Runtime disables
 * automatic IME resizing while the opted-in window is foreground.
 */
/**
 * @param {string} url
 * @return {string}
 * @ignore
 */
export function formatURL(url: string): string;
/**
 * @class ApplicationWindow
 * Represents a window in the application
 */
export class ApplicationWindow extends EventTarget {
    static constants: typeof statuses;
    static hotkey: import("./window/hotkey.js").Bindings;
    constructor({ index, ...state }: {
        [x: string]: any;
        index: any;
    });
    /**
     * The unique ID of this window.
     * @type {string}
     */
    get id(): string;
    /**
     * Get the index of the window
     * @return {number} - the index of the window
     */
    get index(): number;
    /**
     * @type {import('./window/hotkey.js').default}
     */
    get hotkey(): import("./window/hotkey.js").Bindings;
    get state(): {
        [x: string]: any;
    };
    /**
     * The broadcast channel for this window.
     * @type {BroadcastChannel}
     */
    get channel(): BroadcastChannel;
    /**
     * Get the size of the window
     * @type {{ width: number, height: number }} - the size of the window
     */
    get size(): {
        width: number;
        height: number;
    };
    get location(): any;
    /**
     * get  the position of the window
     * @type {{ x: number, y: number }} - the position of the window
     */
    get position(): {
        x: number;
        y: number;
    };
    /**
     * get  the title of the window
     * @type {string}
     */
    get title(): string;
    /**
     * Indicates whether the window follows the host desktop theme.
     * @type {boolean}
     */
    get followSystemTheme(): boolean;
    /**
     * Indicates whether the window prefers a dark theme when not
     * following the system theme.
     * @type {boolean}
     */
    get preferDarkTheme(): boolean;
    /**
     * Whether the window is currently in dark mode.
     * @type {boolean}
     */
    get isDarkMode(): boolean;
    /**
     * Current appearance metadata for the window.
     * @type {{ followSystemTheme: boolean, preferDarkTheme: boolean, isDarkMode: boolean, backgroundColor: { red: number, green: number, blue: number, alpha: number } }}
     */
    get appearance(): {
        followSystemTheme: boolean;
        preferDarkTheme: boolean;
        isDarkMode: boolean;
        backgroundColor: {
            red: number;
            green: number;
            blue: number;
            alpha: number;
        };
    };
    /**
     * @type {string}
     */
    get token(): string;
    /**
     * get  the status of the window
     * @type {number} - the status of the window
     */
    get status(): number;
    /**
     * Get the size of the window
     * @return {{ width: number, height: number }} - the size of the window
     */
    getSize(): {
        width: number;
        height: number;
    };
    /**
     * Get the position of the window
     * @return {{ x: number, y: number }} - the position of the window
     */
    getPosition(): {
        x: number;
        y: number;
    };
    /**
     * Get the title of the window
     * @return {string} - the title of the window
     */
    getTitle(): string;
    /**
     * Get the status of the window
     * @return {number} - the status of the window
     */
    getStatus(): number;
    /**
     * Close the window
     * Android rejects with `ENOTREADY` while Activity state is saved or unavailable.
     * @return {Promise<object>} - the options of the window
     */
    close(): Promise<object>;
    /**
     * Shows the window
     * Android rejects with `ENOTREADY` while Activity state is saved or unavailable.
     * @return {Promise<ipc.Result>}
     */
    show(): Promise<ipc.Result>;
    /**
     * Hides the window
     * Android rejects with `ENOTREADY` while Activity state is saved or unavailable.
     * @return {Promise<ipc.Result>}
     */
    hide(): Promise<ipc.Result>;
    /**
     * Brings the window to the foreground and focuses it.
     * @return {Promise<ipc.Result>}
     */
    focus(): Promise<ipc.Result>;
    /**
     * Removes focus from the window (desktop: sends to back; mobile: hides).
     * @return {Promise<ipc.Result>}
     */
    blur(): Promise<ipc.Result>;
    /**
     * Maximize the window
     * @return {Promise<ipc.Result>}
     */
    maximize(): Promise<ipc.Result>;
    /**
     * Minimize the window
     * @return {Promise<ipc.Result>}
     */
    minimize(): Promise<ipc.Result>;
    /**
     * Restore the window
     * @return {Promise<ipc.Result>}
     */
    restore(): Promise<ipc.Result>;
    /**
     * Sets the title of the window
     * @param {string} title - the title of the window
     * @return {Promise<ipc.Result>}
     */
    setTitle(title: string): Promise<ipc.Result>;
    /**
     * Sets the size of the window
     * @param {object} opts - an options object
     * @param {(number|string)=} opts.width - the width of the window
     * @param {(number|string)=} opts.height - the height of the window
     * @return {Promise<ipc.Result>}
     * @throws {Error} - if the width or height is invalid
     */
    setSize(opts: {
        width?: (number | string) | undefined;
        height?: (number | string) | undefined;
    }): Promise<ipc.Result>;
    /**
     * Sets the position of the window
     * @param {object} opts - an options object
     * @param {(number|string)=} opts.x - the x position of the window
     * @param {(number|string)=} opts.y - the y position of the window
     * @return {Promise<object>}
     * @throws {Error} - if the x or y is invalid
     */
    setPosition(opts: {
        x?: (number | string) | undefined;
        y?: (number | string) | undefined;
    }): Promise<object>;
    /**
     * Navigate the window to a given path
     * @param {object} path - file path
     * @return {Promise<ipc.Result>}
     */
    navigate(path: object): Promise<ipc.Result>;
    /**
     * Opens the Web Inspector for the window (desktop only).
     * @return {Promise<object>}
     */
    showInspector(): Promise<object>;
    /**
     * Acknowledges that this window has a usable application screen. Call after
     * mounting the UI and loading required styles. Android apps configured with
     * `[webview] require_ready = true` keep native startup recovery armed until this
     * acknowledgement. Only the current window can acknowledge its own readiness.
     * This does not replay operations or establish application authorization.
     * @returns {Promise<void>}
     */
    markReady(): Promise<void>;
    /**
     * Reveals this Android window's verified bundled startup screen after a native
     * visual-state fence. Call after installing its inline styles and startup UI.
     * This does not acknowledge application readiness or stop the 30-second native
     * watchdog. Keep application controls inert until the usable screen calls
     * markReady(). Failed startup restores native recovery and its Reload control.
     * Requires the current trusted top-level document; foreign origins, headless
     * windows and stale documents cannot reveal a startup screen. Other platforms
     * reject with ENOSYS. Hidden windows do not gain focus from this call.
     * @returns {Promise<void>}
     */
    markPresented(): Promise<void>;
    /**
     * Sets the background color of the window
     * @param {object} opts - an options object
     * @param {number} opts.red - the red value
     * @param {number} opts.green - the green value
     * @param {number} opts.blue - the blue value
     * @param {number} opts.alpha - the alpha value
     * @return {Promise<object>}
     */
    setBackgroundColor(opts: {
        red: number;
        green: number;
        blue: number;
        alpha: number;
    }): Promise<object>;
    /**
     * Gets the background color of the window
     * @return {Promise<string>}
     */
    getBackgroundColor(): Promise<string>;
    /**
     * Opens a native context menu.
     * @param {object} options - an options object
     * @return {Promise<object>}
     */
    setContextMenu(options: object): Promise<object>;
    /**
     * Sets whether the window should stay always on top (desktop only).
     * @param {boolean} enabled
     * @return {Promise<ipc.Result>}
     */
    setAlwaysOnTop(enabled: boolean): Promise<ipc.Result>;
    /**
     * Checks if the window is set to always be on top (desktop only).
     * @return {Promise<boolean>}
     */
    isAlwaysOnTop(): Promise<boolean>;
    /**
     * Shows a native open file dialog.
     * @param {object} options - an options object
     * @return {Promise<string[]>} - an array of file paths
     */
    showOpenFilePicker(options: object): Promise<string[]>;
    /**
     * Shows a native save file dialog.
     * @param {object} options - an options object
     * @return {Promise<string|null>} - the selected file path or null
     */
    showSaveFilePicker(options: object): Promise<string | null>;
    /**
     * Shows a native directory dialog.
     * @param {object} options - an options object
     * @return {Promise<string[]>} - an array of file paths
     */
    showDirectoryFilePicker(options: object): Promise<string[]>;
    /**
     * Opens the platform share sheet for the current window.
     * New Android file opens expire after 24 hours. Recipients can retain copies.
     * Resolution means a share target was selected, not that delivery completed.
     * @param {ShareOptions} [options]
     * @return {Promise<void>}
     */
    share(options?: ShareOptions): Promise<void>;
    /**
     * Sets Android 11+ system surfaces for this window. The foreground window owns
     * appearance; `system` follows configuration changes. Insets/IME updates arrive
     * as `windowinsetschange` CustomEvents. Earlier Android versions and other
     * platforms reject with `ENOSYS`.
     * @param {SystemAppearanceOptions} options
     * @returns {Promise<void>}
     */
    setSystemAppearance(options: SystemAppearanceOptions): Promise<void>;
    /**
     * Enters or exits Android 11+ native fullscreen for the current foreground
     * window. First select explicit inset ownership with setSystemAppearance.
     * A native Exit fullscreen control, Back, suspension, hiding, and renderer
     * destruction exit presentation. Entry never captures a pointer or hides IME.
     * Observe windowfullscreenchange for native exits. Browser DOM fullscreen is
     * a separate capability. Other platforms reject with ENOSYS.
     * @param {boolean} enabled
     * @returns {Promise<FullscreenState>}
     */
    setFullscreen(enabled: boolean): Promise<FullscreenState>;
    /**
     * Reads Android native fullscreen state for the current foreground window.
     * Other platforms reject with ENOSYS.
     * @returns {Promise<FullscreenState>}
     */
    getFullscreenState(): Promise<FullscreenState>;
    /**
     * Requests Android FLAG_SECURE while this window is visible. The whole native
     * activity is protected, including recents previews and ordinary screenshots.
     * This does not protect clipboard, shared files, external displays, or secrets
     * already exported by the application. Custom native flag owners must coordinate
     * with this runtime API. Other platforms reject with ENOSYS.
     * @param {boolean} enabled
     * @returns {Promise<ContentProtectionState>}
     */
    setContentProtection(enabled: boolean): Promise<ContentProtectionState>;
    /**
     * This is a high-level API that you should use instead of `ipc.request` when
     * you want to send a message to another window or to the backend.
     *
     * @param {object} options - an options object
     * @param {number=} options.window - the window to send the message to
     * @param {boolean=} [options.backend = false] - whether to send the message to the backend
     * @param {string} options.event - the event to send
     * @param {(string|object)=} options.value - the value to send
     * @returns
     */
    send(options: {
        window?: number | undefined;
        backend?: boolean | undefined;
        event: string;
        value?: (string | object) | undefined;
    }): Promise<any>;
    /**
     * Post a message to a window
     * TODO(@jwerle): research using `BroadcastChannel` instead
     * @param {object} data
     * @return {Promise}
     */
    postMessage(data: object): Promise<any>;
    /**
     * Opens an URL in the default application associated with the URL protocol,
     * such as 'https:' for the default web browser.
     * @param {string} value
     * @returns {Promise<{ url: string }>}
     */
    openExternal(value: string): Promise<{
        url: string;
    }>;
    /**
     * Opens a file in the default file explorer.
     * @param {string} value
     * @returns {Promise}
     */
    revealFile(value: string): Promise<any>;
    /**
     * Updates window state
     * @return {Promise<ipc.Result>}
     */
    update(): Promise<ipc.Result>;
    /**
     * Adds a listener to the window.
     * @param {string} event - the event to listen to
     * @param {function(*): void} cb - the callback to call
     * @returns {void}
     */
    addListener(event: string, cb: (arg0: any) => void): void;
    /**
     * Adds a listener to the window. An alias for `addListener`.
     * @param {string} event - the event to listen to
     * @param {function(*): void} cb - the callback to call
     * @returns {void}
     * @see addListener
     */
    on(event: string, cb: (arg0: any) => void): void;
    /**
     * Adds a listener to the window. The listener is removed after the first call.
     * @param {string} event - the event to listen to
     * @param {function(*): void} cb - the callback to call
     * @returns {void}
     */
    once(event: string, cb: (arg0: any) => void): void;
    /**
     * Removes a listener from the window.
     * @param {string} event - the event to remove the listener from
     * @param {function(*): void} cb - the callback to remove
     * @returns {void}
     */
    removeListener(event: string, cb: (arg0: any) => void): void;
    /**
     * Removes all listeners from the window.
     * @param {string} event - the event to remove the listeners from
     * @returns {void}
     */
    removeAllListeners(event: string): void;
    /**
     * Removes a listener from the window. An alias for `removeListener`.
     * @param {string} event - the event to remove the listener from
     * @param {function(*): void} cb - the callback to remove
     * @returns {void}
     * @see removeListener
     */
    off(event: string, cb: (arg0: any) => void): void;
    #private;
}
export default ApplicationWindow;
/**
 * @ignore
 */
export const constants: typeof statuses;
export type FullscreenState = {
    /**
     * Android 11+ native presentation is supported.
     */
    supported: boolean;
    /**
     * Whether a window currently owns fullscreen.
     */
    active: boolean;
    /**
     * Current fullscreen window index.
     */
    owner: number | null;
    /**
     * Whether a browser custom view is being shown.
     */
    customView: boolean;
    /**
     * Call setSystemAppearance before entry.
     */
    requiresManagedInsets: boolean;
    /**
     * False; native fullscreen does not capture a pointer.
     */
    pointerCapture: boolean;
};
export type ContentProtectionState = {
    /**
     * Protection requested for this window.
     */
    enabled: boolean;
    /**
     * Android currently protects the entire activity.
     */
    activityProtected: boolean;
};
export type ShareOptions = {
    title?: string;
    text?: string;
    url?: string;
    /**
     * Android only: up to 16 files, 8 MiB total.
     */
    files?: File[];
};
export type SystemAppearanceOptions = {
    /**
     * System bar icon contrast.
     */
    theme: "system" | "light" | "dark";
    /**
     * Opaque surface color, exactly `#rrggbb`.
     */
    color: string;
    /**
     * Explicit inset owner. Native pads content;
     * web emits CSS-pixel insets for the app to consume. Stop app-owned native inset
     * listeners before adopting this API to prevent double padding. Runtime disables
     * automatic IME resizing while the opted-in window is foreground.
     */
    insets: "native" | "web";
};
import ipc from './ipc.js';
import * as statuses from './window/constants.js';
import client from './application/client.js';
import hotkey from './window/hotkey.js';
export { client, hotkey };
