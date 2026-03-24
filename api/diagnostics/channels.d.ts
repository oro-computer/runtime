/**
 * Normalizes a channel name to lower case replacing white space,
 * hyphens (-), underscores (_), with dots (.).
 * @ignore
 */
export function normalizeName(group: any, name: any): string;
/**
 * Used to preallocate a minimum sized array of subscribers for
 * a channel.
 * @ignore
 */
export const MIN_CHANNEL_SUBSCRIBER_SIZE: 64;
/**
 * A general interface for diagnostic channels that can be subscribed to.
 */
export class Channel {
    constructor(name: any);
    name: any;
    group: any;
    /**
     * Computed subscribers for all channels in this group.
     * @type {Array<function>}
     */
    get subscribers(): Array<Function>;
    /**
     * Accessor for determining if channel has subscribers. This
     * is always `false` for `Channel instances and `true` for `ActiveChannel`
     * instances.
     */
    get hasSubscribers(): boolean;
    /**
     * Computed number of subscribers for this channel.
     */
    get length(): number;
    /**
     * Resets channel state.
     * @param {(boolean)} [shouldOrphan = false]
     */
    reset(shouldOrphan?: (boolean)): void;
    channel(name: any): Channel;
    /**
     * Adds an `onMessage` subscription callback to the channel.
     * @return {boolean}
     */
    subscribe(_: any, onMessage: any): boolean;
    /**
     * Removes an `onMessage` subscription callback from the channel.
     * @param {function} onMessage
     * @return {boolean}
     */
    unsubscribe(_: any, onMessage: Function): boolean;
    /**
     * A no-op for `Channel` instances. This function always returns `false`.
     * @param {string|object} name
     * @param {object=} [message]
     * @return Promise<boolean>
     */
    publish(_name: any, _message?: any): Promise<boolean>;
    /**
     * Returns a string representation of the `ChannelRegistry`.
     * @ignore
     */
    toString(): any;
    /**
     * Iterator interface
     * @ignore
     */
    get [Symbol.iterator](): any[];
    /**
     * The `Channel` string tag.
     * @ignore
     */
    [Symbol.toStringTag](): string;
    #private;
}
/**
 * An `ActiveChannel` is a prototype implementation for a `Channel`
 * that provides an interface what is considered an "active" channel. The
 * `hasSubscribers` accessor always returns `true` for this class.
 */
export class ActiveChannel extends Channel {
    unsubscribe(onMessage: any): boolean;
    /**
     * @param {object|any} message
     * @return Promise<boolean>
     */
    publish(message: object | any): Promise<boolean>;
}
/**
 * A container for a grouping of channels that are named and owned
 * by this group. A `ChannelGroup` can also be a regular channel.
 */
export class ChannelGroup extends Channel {
    /**
     * @param {Array<Channel>} channels
     * @param {string} name
     */
    constructor(name: string, channels: Array<Channel>);
    channels: Channel[];
    /**
     * Subscribe to a channel or selection of channels in this group.
     * @param {string} name
     * @return {boolean}
     */
    subscribe(name: string, onMessage: any): boolean;
    /**
     * Unsubscribe from a channel or selection of channels in this group.
     * @param {string} name
     * @return {boolean}
     */
    unsubscribe(name: string, onMessage: any): boolean;
    /**
     * Gets or creates a channel for this group.
     * @param {string} name
     * @return {Channel}
     */
    channel(name: string): Channel;
    /**
     * Select a test of channels from this group.
     * The following syntax is supported:
     *   - One Channel: `group.channel`
     *   - All Channels: `*`
     *   - Many Channel: `group.*`
     *   - Collections: `['group.a', 'group.b', 'group.c'] or `group.a,group.b,group.c`
     * @param {string|Array<string>} keys
     * @param {(boolean)} [hasSubscribers = false] - Enforce subscribers in selection
     * @return {Array<{name: string, channel: Channel}>}
     */
    select(keys: string | Array<string>, hasSubscribers?: (boolean)): Array<{
        name: string;
        channel: Channel;
    }>;
}
/**
 * An object mapping of named channels to `WeakRef<Channel>` instances.
 */
export const registry: {
    /**
     * Subscribes callback `onMessage` to channel of `name`.
     * @param {string} name
     * @param {function} onMessage
     * @return {boolean}
     */
    subscribe(name: string, onMessage: Function): boolean;
    /**
     * Unsubscribes callback `onMessage` from channel of `name`.
     * @param {string} name
     * @param {function} onMessage
     * @return {boolean}
     */
    unsubscribe(name: string, onMessage: Function): boolean;
    /**
     * Predicate to determine if a named channel has subscribers.
     * @param {string} name
     */
    hasSubscribers(name: string): boolean;
    /**
     * Get or set a channel by `name`.
     * @param {string} name
     * @return {Channel}
     */
    channel(name: string): Channel;
    /**
     * Creates a `ChannelGroup` for a set of channels
     * @param {string} name
     * @param {Array<string>} [channels]
     * @return {ChannelGroup}
     */
    group(name: string, channels?: Array<string>): ChannelGroup;
    /**
     * Get a channel by name. The name is normalized.
     * @param {string} name
     * @return {Channel?}
     */
    get(name: string): Channel | null;
    /**
     * Checks if a channel is known by  name. The name is normalized.
     * @param {string} name
     * @return {boolean}
     */
    has(name: string): boolean;
    /**
     * Set a channel by name. The name is normalized.
     * @param {string} name
     * @param {Channel} channel
     * @return {Channel?}
     */
    set(name: string, channel: Channel): Channel | null;
    /**
     * Removes a channel by `name`
     * @return {boolean}
     */
    remove(name: any): boolean;
    /**
     * Returns a string representation of the `ChannelRegistry`.
     * @ignore
     */
    toString(): any;
    /**
     * Returns a JSON representation of the `ChannelRegistry`.
     * @return {object}
     */
    toJSON(): object;
    /**
     * The `ChannelRegistry` string tag.
     * @ignore
     */
    [Symbol.toStringTag](): string;
};
export default registry;
