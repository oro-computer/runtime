/**
 * Internal IPC for setting an application menu
 * @ignore
 */
export function setMenu(options: any, type: any): Promise<ipc.Result>;
/**
 * Internal IPC for setting an application context menu
 * @ignore
 */
export function setContextMenu(options: any): Promise<any>;
/**
 * A `Menu` is base class for a `ContextMenu`, `SystemMenu`, or `TrayMenu`.
 */
export class Menu extends EventTarget {
    /**
     * `Menu` class constructor.
     * @ignore
     * @param {string} type
     */
    constructor(type: string);
    /**
     * The broadcast channel for this menu.
     * @ignore
     * @type {BroadcastChannel}
     */
    get channel(): BroadcastChannel;
    /**
     * The `Menu` instance type.
     * @type {('context'|'system'|'tray')?}
     */
    get type(): ("context" | "system" | "tray") | null;
    /**
     * Setter for the level 1 'error'` event listener.
     * @ignore
     * @type {function(ErrorEvent)?}
     */
    set onerror(onerror: ((arg0: ErrorEvent) => any) | null);
    /**
     * Level 1 'error'` event listener.
     * @type {function(ErrorEvent)?}
     */
    get onerror(): ((arg0: ErrorEvent) => any) | null;
    /**
     * Setter for the level 1 'menuitem'` event listener.
     * @ignore
     * @type {function(MenuItemEvent)?}
     */
    set onmenuitem(onmenuitem: ((arg0: menuitemEvent) => any) | null);
    /**
     * Level 1 'menuitem'` event listener.
     * @type {function(menuitemEvent)?}
     */
    get onmenuitem(): ((arg0: menuitemEvent) => any) | null;
    /**
     * Set the menu layout for this `Menu` instance.
     * @param {string|object} layoutOrOptions
     * @param {object=} [options]
     */
    set(layoutOrOptions: string | object, options?: object | undefined): Promise<any>;
    #private;
}
/**
 * A container for various `Menu` instances.
 */
export class MenuContainer extends EventTarget {
    /**
     * `MenuContainer` class constructor.
     * @param {EventTarget} [sourceEventTarget]
     * @param {object=} [options]
     */
    constructor(sourceEventTarget?: EventTarget, options?: object | undefined);
    /**
     * Setter for the level 1 'error'` event listener.
     * @ignore
     * @type {function(ErrorEvent)?}
     */
    set onerror(onerror: ((arg0: ErrorEvent) => any) | null);
    /**
     * Level 1 'error'` event listener.
     * @type {function(ErrorEvent)?}
     */
    get onerror(): ((arg0: ErrorEvent) => any) | null;
    /**
     * Setter for the level 1 'menuitem'` event listener.
     * @ignore
     * @type {function(MenuItemEvent)?}
     */
    set onmenuitem(onmenuitem: ((arg0: menuitemEvent) => any) | null);
    /**
     * Level 1 'menuitem'` event listener.
     * @type {function(menuitemEvent)?}
     */
    get onmenuitem(): ((arg0: menuitemEvent) => any) | null;
    /**
     * The `TrayMenu` instance for the application.
     * @type {TrayMenu}
     */
    get tray(): TrayMenu;
    /**
     * The `SystemMenu` instance for the application.
     * @type {SystemMenu}
     */
    get system(): SystemMenu;
    /**
     * The `ContextMenu` instance for the application.
     * @type {ContextMenu}
     */
    get context(): ContextMenu;
    #private;
}
/**
 * A `Menu` instance that represents a context menu.
 */
export class ContextMenu extends Menu {
    constructor();
}
/**
 * A `Menu` instance that represents the system menu.
 */
export class SystemMenu extends Menu {
    constructor();
}
/**
 * A `Menu` instance that represents the tray menu.
 */
export class TrayMenu extends Menu {
    constructor();
}
/**
 * The application tray menu.
 * @type {TrayMenu}
 */
export const tray: TrayMenu;
/**
 * The application system menu.
 * @type {SystemMenu}
 */
export const system: SystemMenu;
/**
 * The application context menu.
 * @type {ContextMenu}
 */
export const context: ContextMenu;
/**
 * The application menus container.
 * @type {MenuContainer}
 */
export const container: MenuContainer;
export default container;
import ipc from '../ipc.js';
