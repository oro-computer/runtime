/**
 * @typedef {object} AndroidNotificationChannel
 * @property {string} id Immutable channel identity; at most 128 characters.
 * @property {string} name User-visible channel name.
 * @property {string} [description]
 * @property {'min'|'low'|'default'|'high'} [importance='default'] Initial default; user settings remain authoritative.
 * @property {string} [sound='default'] 'default', 'none', or a packaged raw/name resource.
 * @property {boolean} [vibration=false]
 * @property {boolean} [badge=true]
 */
/**
 * @typedef {object} AndroidNotificationChannelStatus
 * @property {string} id
 * @property {number} importance Current system importance.
 * @property {boolean} sound
 * @property {boolean} vibration
 * @property {boolean} blocked
 */
/**
 * @typedef {object} AndroidPublicNotification
 * @property {string} title Redacted public lock-screen title; at most 160 characters.
 * @property {string} [body] Redacted public body; at most 512 characters.
 */
/**
 * @typedef {object} AndroidNotificationAction
 * @property {string} action Opaque action identity.
 * @property {string} title User-visible action label.
 */
/**
 * @typedef {object} AndroidNotificationOptions
 * @property {string} id Opaque application identity, returned by the existing response ledger.
 * @property {string} title
 * @property {string} channel Existing channel identity, or 'default' for the built-in channel.
 * @property {string} [body]
 * @property {string} [tag]
 * @property {string} [category]
 * @property {boolean} [silent=false]
 * @property {boolean} [renotify=false]
 * @property {boolean} [requireInteraction=false]
 * @property {AndroidNotificationAction[]} [actions] At most three opaque routing actions.
 * @property {string} [group] Application-defined group key.
 * @property {boolean} [groupSummary=false] Submit a summary explicitly; summaries are not generated automatically.
 * @property {'all'|'summary'|'children'} [groupAlertBehavior='all'] Apply consistently to group members.
 * @property {string} [smallIcon] Packaged drawable resource name.
 * @property {AndroidPublicNotification} [publicVersion] Redacted replacement; private visibility is always retained.
 */
/**
 * @typedef {object} AndroidNotificationSubmission
 * @property {string} id
 * @property {boolean} submitted OS submission succeeded.
 * @property {boolean} presentationObserved False; submission is not proof of display, sound or user attention.
 * @property {string} [visibilityPolicy] Linux uses publicVersion on every surface when supplied; otherwise all supplied content must be public.
 * @property {boolean} [groupingSupported] False on Linux; Android grouping policies reject there.
 */
/**
 * Registers at most 32 immutable named policies. Android uses OS channels;
 * Linux stores application policy without changing desktop user preferences.
 * Linux supports default/none sound, no vibration, and no badge guarantee.
 * Reusing an ID with
 * changed defaults rejects EEXIST; choose a new versioned ID instead. Existing
 * user changes are preserved. Requires notification capability; does not prompt.
 * @param {AndroidNotificationChannel[]} channels
 * @returns {Promise<AndroidNotificationChannelStatus[]>}
 */
export function configureChannels(channels: AndroidNotificationChannel[]): Promise<AndroidNotificationChannelStatus[]>;
/**
 * Submits an Android notification with channel/group/public-version policy.
 * Reads and acknowledgement use getResponses()/acknowledgeResponse(). This
 * function does not establish account authority or deduplicate server events.
 * Linux uses freedesktop notifications in the GUI or configured native host. It cannot enforce
 * lock-screen visibility: publicVersion replaces all content on every surface;
 * otherwise supply only public content. Group summaries/group alert policies
 * reject ENOSYS. Other unimplemented platforms reject ENOSYS.
 * Compiled Android executors can use the same
 * NativeNotifications.submit(Context, JSONObject) implementation without a WebView.
 * @param {AndroidNotificationOptions} options
 * @returns {Promise<AndroidNotificationSubmission>}
 */
export function submit(options: AndroidNotificationOptions): Promise<AndroidNotificationSubmission>;
/**
 * @typedef {object} AndroidNotificationCancelOptions
 * @property {string} [tag=''] Exact tag used for submission.
 */
/**
 * Cancels an Android notification by its exact application ID/tag. Missing
 * notifications are allowed. It does not create a dismissal receipt, revoke
 * server state or acknowledge existing responses. Compiled receivers can use
 * NativeNotifications.cancel(Context, id, tag) without an Activity or WebView.
 * Linux also supports exact ID/tag cancellation. Other backends may reject ENOSYS.
 * @param {string} id
 * @param {AndroidNotificationCancelOptions} [options]
 * @returns {Promise<void>}
 */
export function cancel(id: string, options?: AndroidNotificationCancelOptions): Promise<void>;
/**
 * @typedef {object} AndroidNotificationStatusOptions
 * @property {string[]} [channels] At most 32 exact channel IDs; no channel is created.
 */
/**
 * @typedef {AndroidNotificationChannelStatus & { exists: boolean }} AndroidExistingNotificationChannel
 */
/**
 * @typedef {object} AndroidNotificationStatus
 * @property {boolean} enabled Current OS package notification setting.
 * @property {AndroidExistingNotificationChannel[]} channels Current requested channel settings.
 */
/**
 * @typedef {object} AndroidActiveNotification
 * @property {string} id Opaque application ID.
 * @property {string} tag Exact tag used for cancellation.
 * @property {string} channel
 * @property {string} group
 * @property {boolean} summary
 * @property {boolean} private Private lock-screen visibility was requested.
 * @property {boolean} openAction A content PendingIntent exists; this is not authorization to navigate.
 */
/**
 * Read Android package/channel settings without prompting or changing channels.
 * Other platform metadata backends may reject ENOSYS. An enabled setting is
 * not proof that a notification was displayed or heard.
 * @param {AndroidNotificationStatusOptions} [options]
 * @returns {Promise<AndroidNotificationStatus>}
 */
export function getStatus(options?: AndroidNotificationStatusOptions): Promise<AndroidNotificationStatus>;
/**
 * Read bounded Android OS metadata for this application's runtime notifications.
 * Content, private target references and legacy non-runtime tags are excluded.
 * Existing private correlation and historical compatibility inspection remain
 * native-only SDK operations. Other metadata backends may reject ENOSYS.
 * @returns {Promise<AndroidActiveNotification[]>}
 */
export function getActive(): Promise<AndroidActiveNotification[]>;
/**
 * @typedef {object} NotificationResponseReceipt
 * @property {string} receiptId Opaque identity used to acknowledge this receipt.
 * @property {string} id Application notification identity.
 * @property {string} action Action identity, `default`, or `dismiss`.
 * @property {number} receivedAt Unix timestamp in milliseconds.
 */
/**
 * Reads Android or Linux notification responses retained across process restarts. Call
 * after restoring authorization, resolve IDs against authorized application
 * state, then acknowledge handled or obsolete receipts. Reading does not remove
 * receipts. At most 64 responses are retained for seven days. Notification
 * content and arbitrary `data` are not persisted. Linux actions require the
 * notification-owning process to remain alive; saved receipts survive its restart.
 * A native session host can receive actions while the GUI is closed and launch
 * it with an opaque receipt hint. A `notificationresponse` hint with
 * `requiresRefresh: true` means query these retained receipts after restoring
 * authorization; it does not carry the complete response or authorize opening.
 * Other unimplemented platforms reject with
 * `ENOSYS`. A receipt does not establish authorization or verified presentation.
 * @returns {Promise<NotificationResponseReceipt[]>}
 */
export function getResponses(): Promise<NotificationResponseReceipt[]>;
/**
 * Removes an Android or Linux response after application reconciliation. Repeated
 * acknowledgements are allowed. Other platforms reject with `ENOSYS`.
 * @param {string} receiptId Opaque identity returned by `getResponses()`.
 * @returns {Promise<void>}
 */
export function acknowledgeResponse(receiptId: string): Promise<void>;
/**
 * @typedef {object} NotificationDeliveryStatus
 * @property {boolean} enabled Application notifications and runtime capability are enabled.
 * @property {string} channelId Native channel identity.
 * @property {boolean} exists
 * @property {boolean} blocked Channel importance is disabled.
 * @property {number|null} importance Android importance, or null for a missing channel.
 * @property {boolean} sound Whether the channel has a sound configured.
 * @property {boolean} vibration Whether channel vibration is enabled.
 * @property {boolean} canSubmit Current permission/channel state permits submission.
 * @property {'submission-only'} deliveryBoundary Submission never proves presentation or delivery.
 * @property {boolean} pushAvailable Configured native transport, not a trusted subscription or guaranteed delivery.
 * @property {boolean} [systemChannel] False on Linux; policies are application-local.
 * @property {boolean} [systemPolicyKnown] False on Linux; DND, audio and presentation decisions remain desktop-owned.
 */
/**
 * Reads Android application/channel or Linux application/transport policy without asking for consent. Refresh
 * after resume because the user can change it in Settings. Other platforms
 * reject with `ENOSYS`; a true `canSubmit` does not establish server authorization.
 * @param {string} [channel='default'] Native channel ID, or the application's default.
 * @returns {Promise<NotificationDeliveryStatus>}
 */
export function getDeliveryStatus(channel?: string): Promise<NotificationDeliveryStatus>;
/**
 * Opens this Android application's channel controls in system Settings. Call
 * from a user action. This never changes sound, DND, permission, or importance.
 * Other platforms reject with `ENOSYS`.
 * @param {string} [channel='default']
 * @returns {Promise<void>}
 */
export function openChannelSettings(channel?: string): Promise<void>;
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
export type AndroidNotificationChannel = {
    /**
     * Immutable channel identity; at most 128 characters.
     */
    id: string;
    /**
     * User-visible channel name.
     */
    name: string;
    description?: string;
    /**
     * Initial default; user settings remain authoritative.
     */
    importance?: "min" | "low" | "default" | "high";
    /**
     * 'default', 'none', or a packaged raw/name resource.
     */
    sound?: string;
    vibration?: boolean;
    badge?: boolean;
};
export type AndroidNotificationChannelStatus = {
    id: string;
    /**
     * Current system importance.
     */
    importance: number;
    sound: boolean;
    vibration: boolean;
    blocked: boolean;
};
export type AndroidPublicNotification = {
    /**
     * Redacted public lock-screen title; at most 160 characters.
     */
    title: string;
    /**
     * Redacted public body; at most 512 characters.
     */
    body?: string;
};
export type AndroidNotificationAction = {
    /**
     * Opaque action identity.
     */
    action: string;
    /**
     * User-visible action label.
     */
    title: string;
};
export type AndroidNotificationOptions = {
    /**
     * Opaque application identity, returned by the existing response ledger.
     */
    id: string;
    title: string;
    /**
     * Existing channel identity, or 'default' for the built-in channel.
     */
    channel: string;
    body?: string;
    tag?: string;
    category?: string;
    silent?: boolean;
    renotify?: boolean;
    requireInteraction?: boolean;
    /**
     * At most three opaque routing actions.
     */
    actions?: AndroidNotificationAction[];
    /**
     * Application-defined group key.
     */
    group?: string;
    /**
     * Submit a summary explicitly; summaries are not generated automatically.
     */
    groupSummary?: boolean;
    /**
     * Apply consistently to group members.
     */
    groupAlertBehavior?: "all" | "summary" | "children";
    /**
     * Packaged drawable resource name.
     */
    smallIcon?: string;
    /**
     * Redacted replacement; private visibility is always retained.
     */
    publicVersion?: AndroidPublicNotification;
};
export type AndroidNotificationSubmission = {
    id: string;
    /**
     * OS submission succeeded.
     */
    submitted: boolean;
    /**
     * False; submission is not proof of display, sound or user attention.
     */
    presentationObserved: boolean;
    /**
     * Linux uses publicVersion on every surface when supplied; otherwise all supplied content must be public.
     */
    visibilityPolicy?: string;
    /**
     * False on Linux; Android grouping policies reject there.
     */
    groupingSupported?: boolean;
};
export type AndroidNotificationCancelOptions = {
    /**
     * Exact tag used for submission.
     */
    tag?: string;
};
export type AndroidNotificationStatusOptions = {
    /**
     * At most 32 exact channel IDs; no channel is created.
     */
    channels?: string[];
};
export type AndroidExistingNotificationChannel = AndroidNotificationChannelStatus & {
    exists: boolean;
};
export type AndroidNotificationStatus = {
    /**
     * Current OS package notification setting.
     */
    enabled: boolean;
    /**
     * Current requested channel settings.
     */
    channels: AndroidExistingNotificationChannel[];
};
export type AndroidActiveNotification = {
    /**
     * Opaque application ID.
     */
    id: string;
    /**
     * Exact tag used for cancellation.
     */
    tag: string;
    channel: string;
    group: string;
    summary: boolean;
    /**
     * Private lock-screen visibility was requested.
     */
    private: boolean;
    /**
     * A content PendingIntent exists; this is not authorization to navigate.
     */
    openAction: boolean;
};
export type NotificationResponseReceipt = {
    /**
     * Opaque identity used to acknowledge this receipt.
     */
    receiptId: string;
    /**
     * Application notification identity.
     */
    id: string;
    /**
     * Action identity, `default`, or `dismiss`.
     */
    action: string;
    /**
     * Unix timestamp in milliseconds.
     */
    receivedAt: number;
};
export type NotificationDeliveryStatus = {
    /**
     * Application notifications and runtime capability are enabled.
     */
    enabled: boolean;
    /**
     * Native channel identity.
     */
    channelId: string;
    exists: boolean;
    /**
     * Channel importance is disabled.
     */
    blocked: boolean;
    /**
     * Android importance, or null for a missing channel.
     */
    importance: number | null;
    /**
     * Whether the channel has a sound configured.
     */
    sound: boolean;
    /**
     * Whether channel vibration is enabled.
     */
    vibration: boolean;
    /**
     * Current permission/channel state permits submission.
     */
    canSubmit: boolean;
    /**
     * Submission never proves presentation or delivery.
     */
    deliveryBoundary: "submission-only";
    /**
     * Configured native transport, not a trusted subscription or guaranteed delivery.
     */
    pushAvailable: boolean;
    /**
     * False on Linux; policies are application-local.
     */
    systemChannel?: boolean;
    /**
     * False on Linux; DND, audio and presentation decisions remain desktop-owned.
     */
    systemPolicyKnown?: boolean;
};
import { Enumeration } from './enumeration.js';
import URL from './url.js';
