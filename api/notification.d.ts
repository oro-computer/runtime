/**
 * Show a notification. Creates a `Notification` instance and displays
 * it to the user.
 * @param {string} title
 * @param {NotificationOptions=} [options]
 * @param {function(Event)=} [onclick]
 * @param {function(Event)=} [onclose]
 * @return {Promise}
 */
export function showNotification(title: string, options?: NotificationOptions | undefined, onclick?: ((arg0: Event) => any) | undefined, onshow?: any): Promise<any>;
/**
 * The global event dispatched when a `Notification` is presented to
 * the user.
 * @ignore
 * @type {string}
 */
export const NOTIFICATION_PRESENTED_EVENT: string;
/**
 * The global event dispatched when a `Notification` has a response
 * from the user.
 * @ignore
 * @type {string}
 */
export const NOTIFICATION_RESPONSE_EVENT: string;
/**
 * An enumeratino of notification test directions:
 * - 'auto'  Automatically determined by the operating system
 * - 'ltr'   Left-to-right text direction
 * - 'rtl'   Right-to-left text direction
 * @type {Enumeration}
 * @ignore
 */
export const NotificationDirection: Enumeration;
/**
 * An enumeration of permission types granted by the user for the current
 * origin to display notifications to the end user.
 * - 'granted'  The user has explicitly granted permission for the current
 *              origin to display system notifications.
 * - 'denied'   The user has explicitly denied permission for the current
 *              origin to display system notifications.
 * - 'default'  The user decision is unknown; in this case the application
 *              will act as if permission was denied.
 * @type {Enumeration}
 * @ignore
 */
export const NotificationPermission: Enumeration;
/**
 * A validated notification action object container.
 * You should never need to construct this.
 * @ignore
 */
export class NotificationAction {
    /**
     * `NotificationAction` class constructor.
     * @ignore
     * @param {object} options
     * @param {string} options.action
     * @param {string} options.title
     * @param {string|URL=} [options.icon = '']
     */
    constructor(options: {
        action: string;
        title: string;
        icon?: (string | URL) | undefined;
    });
    /**
     * A string identifying a user action to be displayed on the notification.
     * @type {string}
     */
    get action(): string;
    /**
     * A string containing action text to be shown to the user.
     * @type {string}
     */
    get title(): string;
    /**
     * A string containing the URL of an icon to display with the action.
     * @type {string}
     */
    get icon(): string;
    /**
     * Serialize this action for native transport.
     * @ignore
     * @return {{action: string, title: string, icon: string}}
     */
    toJSON(): {
        action: string;
        title: string;
        icon: string;
    };
    #private;
}
/**
 * A validated notification options object container.
 * You should never need to construct this.
 * @ignore
 */
export class NotificationOptions {
    /**
     * `NotificationOptions` class constructor.
     * @ignore
     * @param {object} [options = {}]
     * @param {string=} [options.dir = 'auto']
     * @param {NotificationAction[]=} [options.actions = []]
     * @param {string|URL=} [options.badge = '']
     * @param {string=} [options.body = '']
     * @param {?any=} [options.data = null]
     * @param {string|URL=} [options.icon = '']
     * @param {string|URL=} [options.image = '']
     * @param {string=} [options.lang = '']
     * @param {string=} [options.tag = '']
     * @param {boolean=} [options.boolean = '']
     * @param {boolean=} [options.requireInteraction = false]
     * @param {boolean=} [options.silent = false]
     * @param {number[]=} [options.vibrate = []]
     */
    constructor(options?: {
        dir?: string | undefined;
        actions?: NotificationAction[] | undefined;
        badge?: (string | URL) | undefined;
        body?: string | undefined;
        data?: (any | null) | undefined;
        icon?: (string | URL) | undefined;
        image?: (string | URL) | undefined;
        lang?: string | undefined;
        tag?: string | undefined;
        boolean?: boolean | undefined;
        requireInteraction?: boolean | undefined;
        silent?: boolean | undefined;
        vibrate?: number[] | undefined;
    }, allowServiceWorkerGlobalScope?: boolean);
    /**
     * An array of actions to display in the notification.
     * @type {NotificationAction[]}
     */
    get actions(): NotificationAction[];
    /**
     * A string containing the URL of the image used to represent
     * the notification when there isn't enough space to display the
     * notification itself.
     * @type {string}
     */
    get badge(): string;
    /**
     * A string representing the body text of the notification,
     * which is displayed below the title.
     * @type {string}
     */
    get body(): string;
    /**
     * Arbitrary data that you want associated with the notification.
     * This can be of any data type.
     * @type {?any}
     */
    get data(): any | null;
    /**
     * The direction in which to display the notification.
     * It defaults to 'auto', which just adopts the environments
     * language setting behavior, but you can override that behavior
     * by setting values of 'ltr' and 'rtl'.
     * @type {'auto'|'ltr'|'rtl'}
     */
    get dir(): "auto" | "ltr" | "rtl";
    /**
      A string containing the URL of an icon to be displayed in the notification.
     * @type {string}
     */
    get icon(): string;
    /**
     * The URL of an image to be displayed as part of the notification, as
     * specified in the constructor's options parameter.
     * @type {string}
     */
    get image(): string;
    /**
     * The notification's language, as specified using a string representing a
     * language tag according to RFC 5646.
     * @type {string}
     */
    get lang(): string;
    /**
     * A boolean value specifying whether the user should be notified after a
     * new notification replaces an old one. The default is `false`, which means
     * they won't be notified. If `true`, then tag also must be set.
     * @type {boolean}
     */
    get renotify(): boolean;
    /**
     * Indicates that a notification should remain active until the user clicks
     * or dismisses it, rather than closing automatically.
     * The default value is `false`.
     * @type {boolean}
     */
    get requireInteraction(): boolean;
    /**
     * A boolean value specifying whether the notification is silent (no sounds
     * or vibrations issued), regardless of the device settings.
     * The default is `false`, which means it won't be silent. If `true`, then
     * vibrate must not be present.
     * @type {boolean}
     */
    get silent(): boolean;
    /**
     * A string representing an identifying tag for the notification.
     * The default is the empty string.
     * @type {string}
     */
    get tag(): string;
    /**
     * A vibration pattern for the device's vibration hardware to emit with
     * the notification. If specified, silent must not be `true`.
     * @type {number[]}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Vibration_API#vibration_patterns}
     */
    get vibrate(): number[];
    /**
     * @ignore
     * @return {object}
     */
    toJSON(): object;
    #private;
}
/**
 * The Notification interface is used to configure and display
 * desktop and mobile notifications to the user.
 */
export class Notification extends EventTarget {
    /**
     * A read-only property that indicates the current permission granted
     * by the user to display notifications.
     * @type {'prompt'|'granted'|'denied'}
     */
    static get permission(): "prompt" | "granted" | "denied";
    /**
     * The maximum number of actions supported by the device.
     * @type {number}
     */
    static get maxActions(): number;
    /**
     * Requests permission from the user to display notifications.
     * @param {object=} [options]
     * @param {boolean=} [options.alert = true] - (macOS/iOS only)
     * @param {boolean=} [options.sound = false] - (macOS/iOS only)
     * @param {boolean=} [options.badge = false] - (macOS/iOS only)
     * @param {boolean=} [options.force = false]
     * @return {Promise<'granted'|'default'|'denied'>}
     */
    static requestPermission(options?: object | undefined): Promise<"granted" | "default" | "denied">;
    /**
     * `Notification` class constructor.
     * @param {string} title
     * @param {NotificationOptions=} [options]
     */
    constructor(title: string, options?: NotificationOptions | undefined, existingState?: any, ...args: any[]);
    /**
     * @ignore
     */
    get options(): any;
    /**
     * A unique identifier for this notification.
     * @type {string}
     */
    get id(): string;
    /**
     * `true` if the notification was closed, otherwise `false`.
     * @type {boolean}
     */
    get closed(): boolean;
    /**
     * The last action identifier associated with this notification.
     * Empty string represents the default action.
     * @type {string}
     */
    get action(): string;
    /**
     * The raw action identifier delivered by the platform.
     * @ignore
     * @type {string}
     */
    get rawAction(): string;
    set onclick(onclick: Function | null);
    /**
     * The click event is dispatched when the user clicks on
     * displayed notification.
     * @type {?function}
     */
    get onclick(): Function | null;
    set onclose(onclose: Function | null);
    /**
     * The close event is dispatched when the notification closes.
     * @type {?function}
     */
    get onclose(): Function | null;
    set onerror(onerror: Function | null);
    /**
     * The eror event is dispatched when the notification fails to display
     * or encounters an error.
     * @type {?function}
     */
    get onerror(): Function | null;
    set onshow(onshow: Function | null);
    /**
     * The click event is dispatched when the notification is displayed.
     * @type {?function}
     */
    get onshow(): Function | null;
    /**
     * An array of actions to display in the notification.
     * @type {NotificationAction[]}
     */
    get actions(): NotificationAction[];
    /**
     * A string containing the URL of the image used to represent
     * the notification when there isn't enough space to display the
     * notification itself.
     * @type {string}
     */
    get badge(): string;
    /**
     * A string representing the body text of the notification,
     * which is displayed below the title.
     * @type {string}
     */
    get body(): string;
    /**
     * Arbitrary data that you want associated with the notification.
     * This can be of any data type.
     * @type {?any}
     */
    get data(): any | null;
    /**
     * The direction in which to display the notification.
     * It defaults to 'auto', which just adopts the environments
     * language setting behavior, but you can override that behavior
     * by setting values of 'ltr' and 'rtl'.
     * @type {'auto'|'ltr'|'rtl'}
     */
    get dir(): "auto" | "ltr" | "rtl";
    /**
     * A string containing the URL of an icon to be displayed in the notification.
     * @type {string}
     */
    get icon(): string;
    /**
     * The URL of an image to be displayed as part of the notification, as
     * specified in the constructor's options parameter.
     * @type {string}
     */
    get image(): string;
    /**
     * The notification's language, as specified using a string representing a
     * language tag according to RFC 5646.
     * @type {string}
     */
    get lang(): string;
    /**
     * A boolean value specifying whether the user should be notified after a
     * new notification replaces an old one. The default is `false`, which means
     * they won't be notified. If `true`, then tag also must be set.
     * @type {boolean}
     */
    get renotify(): boolean;
    /**
     * Indicates that a notification should remain active until the user clicks
     * or dismisses it, rather than closing automatically.
     * The default value is `false`.
     * @type {boolean}
     */
    get requireInteraction(): boolean;
    /**
     * A boolean value specifying whether the notification is silent (no sounds
     * or vibrations issued), regardless of the device settings.
     * The default is `false`, which means it won't be silent. If `true`, then
     * vibrate must not be present.
     * @type {boolean}
     */
    get silent(): boolean;
    /**
     * A string representing an identifying tag for the notification.
     * The default is the empty string.
     * @type {string}
     */
    get tag(): string;
    /**
     * A vibration pattern for the device's vibration hardware to emit with
     * the notification. If specified, silent must not be `true`.
     * @type {number[]}
     * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Vibration_API#vibration_patterns}
     */
    get vibrate(): number[];
    /**
     * The timestamp of the notification.
     * @type {number}
     */
    get timestamp(): number;
    /**
     * The title read-only property of the `Notification` instace indicates
     * the title of the notification, as specified in the `title` parameter
     * of the `Notification` constructor.
     * @type {string}
     */
    get title(): string;
    /**
     * Closes the notification programmatically.
     */
    close(): Promise<any>;
    #private;
}
export default Notification;
import { Enumeration } from './enumeration.js';
import URL from './url.js';
