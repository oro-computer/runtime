/**
 * Computes rate limit predicate value for a port and address pair for a given
 * threshold updating an input rates map. This method is accessed concurrently,
 * the rates object makes operations atomic to avoid race conditions.
 *
 * @param {Map} rates
 * @param {number} type
 * @param {number} port
 * @param {string} address
 * @return {boolean}
 */
export function rateLimit(rates: Map<any, any>, type: number, port: number, address: string, subclusterIdQuota: any): boolean;
/**
 * Retry delay in milliseconds for ping.
 * @type {number}
 */
export const PING_RETRY: number;
/**
 * Probe wait timeout in milliseconds.
 * @type {number}
 */
export const PROBE_WAIT: number;
/**
 * Default keep alive timeout.
 * @type {number}
 */
export const DEFAULT_KEEP_ALIVE: number;
/**
 * Default rate limit threshold in milliseconds.
 * @type {number}
 */
export const DEFAULT_RATE_LIMIT_THRESHOLD: number;
export function getRandomPort(ports: object, p: number | null): number;
/**
 * A `RemotePeer` represents an initial, discovered, or connected remote peer.
 * Typically, you will not need to create instances of this class directly.
 */
export class RemotePeer {
    /**
     * `RemotePeer` class constructor.
     * @param {{
     *   peerId?: string,
     *   address?: string,
     *   port?: number,
     *   natType?: number,
     *   clusters: object,
     *   reflectionId?: string,
     *   distance?: number,
     *   publicKey?: string,
     *   privateKey?: string,
     *   clock?: number,
     *   lastUpdate?: number,
     *   lastRequest?: number
     * }} o
     */
    constructor(o: {
        peerId?: string;
        address?: string;
        port?: number;
        natType?: number;
        clusters: object;
        reflectionId?: string;
        distance?: number;
        publicKey?: string;
        privateKey?: string;
        clock?: number;
        lastUpdate?: number;
        lastRequest?: number;
    }, peer: any);
    peerId: any;
    address: any;
    port: number;
    natType: any;
    clusters: {};
    pingId: any;
    distance: number;
    connected: boolean;
    opening: number;
    probed: number;
    proxy: any;
    clock: number;
    uptime: number;
    lastUpdate: number;
    lastRequest: number;
    localPeer: any;
    write(sharedKey: any, args: any): Promise<any[]>;
}
/**
 * `Peer` class factory.
 * @param {{ createSocket: function('udp4', null, object?): object }} options
 */
export class Peer {
    /**
     * Test a peerID is valid
     *
     * @param {string} pid
     * @returns boolean
     */
    static isValidPeerId(pid: string): boolean;
    /**
     * Test a reflectionID is valid
     *
     * @param {string} rid
     * @returns boolean
     */
    static isValidReflectionId(rid: string): boolean;
    /**
     * Test a pingID is valid
     *
     * @param {string} pid
     * @returns boolean
     */
    static isValidPingId(pid: string): boolean;
    /**
     * Returns the online status of the browser, else true.
     *
     * note: globalThis.navigator was added to node in v22.
     *
     * @returns boolean
     */
    static onLine(): boolean;
    /**
     * `Peer` class constructor.
     * @param {object=} opts - Options
     * @param {Buffer} opts.peerId - A 32 byte buffer (ie, `Encryption.createId()`).
     * @param {Buffer} opts.clusterId - A 32 byte buffer (ie, `Encryption.createClusterId()`).
     * @param {number=} opts.port - A port number.
     * @param {number=} opts.probeInternalPort - An internal port number (semi-private for testing).
     * @param {number=} opts.probeExternalPort - An external port number (semi-private for testing).
     * @param {number=} opts.natType - A nat type.
     * @param {string=} opts.address - An ipv4 address.
     * @param {number=} opts.keepalive - The interval of the main loop.
     * @param {function=} opts.siblingResolver - A function that can be used to determine canonical data in case two packets have concurrent clock values.
     * @param {object} dgram - A nodejs compatible implementation of the dgram module (sans multicast).
     */
    constructor(persistedState: {}, dgram: object);
    port: any;
    address: any;
    natType: number;
    nextNatType: number;
    clusters: {};
    syncs: {};
    reflectionId: any;
    reflectionTimeout: any;
    reflectionStage: number;
    reflectionRetry: number;
    reflectionFirstResponder: any;
    peerId: string;
    isListening: boolean;
    ctime: number;
    lastUpdate: number;
    lastSync: number;
    closing: boolean;
    clock: number;
    unpublished: {};
    cache: any;
    uptime: number;
    maxHops: number;
    bdpCache: number[];
    dgram: any;
    onListening: any;
    onDelete: any;
    sendQueue: any[];
    firewall: any;
    rates: Map<any, any>;
    streamBuffer: Map<any, any>;
    gate: Map<any, any>;
    returnRoutes: Map<any, any>;
    metrics: {
        i: {
            0: number;
            1: number;
            2: number;
            3: number;
            4: number;
            5: number;
            6: number;
            7: number;
            8: number;
            DROPPED: number;
        };
        o: {
            0: number;
            1: number;
            2: number;
            3: number;
            4: number;
            5: number;
            6: number;
            7: number;
            8: number;
        };
    };
    peers: any;
    encryption: Encryption;
    config: any;
    _onError: (err: any) => any;
    socket: any;
    probeSocket: any;
    /**
     * An implementation for clearing an interval that can be overridden by the test suite
     * @param Number the number that identifies the timer
     * @return {undefined}
     * @ignore
     */
    _clearInterval(tid: any): undefined;
    /**
     * An implementation for clearing a timeout that can be overridden by the test suite
     * @param Number the number that identifies the timer
     * @return {undefined}
     * @ignore
     */
    _clearTimeout(tid: any): undefined;
    /**
     * An implementation of an internal timer that can be overridden by the test suite
     * @return {Number}
     * @ignore
     */
    _setInterval(fn: any, t: any): number;
    /**
     * An implementation of an timeout timer that can be overridden by the test suite
     * @return {Number}
     * @ignore
     */
    _setTimeout(fn: any, t: any): number;
    _onDebug(...args: any[]): void;
    _stableStringify(value: any): string;
    _cpPayload(type: any, clusterId: any, subclusterId: any, message: any): Uint8Array<any>;
    _applyControlAuth(PacketCtor: any, props: any): any;
    _verifyControlAuth(packet: any): any;
    /**
     * A method that encapsulates the listing procedure
     * @return {undefined}
     * @ignore
     */
    _listen(): undefined;
    init(cb: any): Promise<any>;
    onReady: any;
    mainLoopTimer: number;
    /**
     * Continuously evaluate the state of the peer and its network
     * @return {undefined}
     * @ignore
     */
    _mainLoop(ts: any): undefined;
    /**
     * Enqueue packets to be sent to the network
     * @param {Buffer} data - An encoded packet
     * @param {number} port - The desination port of the remote host
     * @param {string} address - The destination address of the remote host
     * @param {Socket=this.socket} socket - The socket to send on
     * @return {undefined}
     * @ignore
     */
    send(data: Buffer, port: number, address: string, socket?: any): undefined;
    /**
     * @private
     */
    private stream;
    /**
     * @private
     */
    private _scheduleSend;
    sendTimeout: number;
    /**
     * @private
     */
    private _dequeue;
    /**
     * Send any unpublished packets
     * @return {undefined}
     * @ignore
     */
    sendUnpublished(): undefined;
    /**
     * Get the serializable state of the peer (can be passed to the constructor or create method)
     * @return {undefined}
     */
    getState(): undefined;
    getInfo(): Promise<{
        address: any;
        port: any;
        clock: number;
        uptime: number;
        natType: number;
        natName: string;
        peerId: string;
    }>;
    cacheInsert(packet: any): Promise<void>;
    addIndexedPeer(info: any): Promise<void>;
    reconnect(): Promise<void>;
    disconnect(): Promise<void>;
    probeReflectionTimeout: any;
    sealUnsigned(...args: any[]): Promise<any>;
    openUnsigned(...args: any[]): Promise<Buffer>;
    seal(...args: any[]): Promise<Buffer>;
    open(...args: any[]): Promise<Buffer>;
    addEncryptionKey(...args: any[]): Promise<void>;
    /**
     * Get a selection of known peers
     * @return {Array<RemotePeer>}
     * @ignore
     */
    getPeers(packet: any, peers: any, ignorelist: any, filter?: (o: any) => any): Array<RemotePeer>;
    /**
     * Send an eventually consistent packet to a selection of peers (fanout)
     * @return {undefined}
     * @ignore
     */
    mcast(packet: any, ignorelist?: any[]): undefined;
    /**
     * The process of determining this peer's NAT behavior (firewall and dependentness)
     * @return {undefined}
     * @ignore
     */
    requestReflection(): undefined;
    /**
     * Ping another peer
     * @return {PacketPing}
     * @ignore
     */
    ping(peer: any, withRetry: any, props: any, socket: any): PacketPing;
    /**
     * Get a peer
     * @return {RemotePeer}
     * @ignore
     */
    getPeer(id: any): RemotePeer;
    /**
     * This should be called at least once when an app starts to multicast
     * this peer, and starts querying the network to discover peers.
     * @param {object} keys - Created by `Encryption.createKeyPair()`.
     * @param {object=} args - Options
     * @param {number=MAX_BANDWIDTH} args.rateLimit - How many requests per second to allow for this subclusterId.
     * @return {RemotePeer}
     */
    join(sharedKey: any, args?: object | undefined): RemotePeer;
    /**
     * @param {typeof Packet} T - The constructor to be used to create packets.
     * @param {any} message - The message to be split and packaged.
     * @return {Promise<Packet[]>}
     * @ignore
     */
    _message2packets(T: typeof Packet, message: any, args: any): Promise<Packet[]>;
    /**
     * Sends a packet into the network that will be replicated and buffered.
     * Each peer that receives it will buffer it until TTL and then replicate
     * it provided it has has not exceeded their maximum number of allowed hops.
     *
     * @param {object} keys - the public and private key pair created by `Encryption.createKeyPair()`.
     * @param {object} args - The arguments to be applied.
     * @param {Buffer} args.message - The message to be encrypted by keys and sent.
     * @param {Packet=} args.packet - The previous packet in the packet chain.
     * @param {Buffer} args.usr1 - 32 bytes of arbitrary clusterId in the protocol framing.
     * @param {Buffer} args.usr2 - 32 bytes of arbitrary clusterId in the protocol framing.
     * @return {Array<PacketPublish>}
     */
    publish(sharedKey: any, args: {
        message: Buffer;
        packet?: Packet | undefined;
        usr1: Buffer;
        usr2: Buffer;
    }): Array<PacketPublish>;
    /**
     * @return {undefined}
     */
    sync(peer: any, ptime?: number): undefined;
    close(): void;
    /**
     * Deploy a query into the network
     * @return {undefined}
     *
     */
    query(query: any): undefined;
    /**
     *
     * This is a default implementation for deciding what to summarize
     * from the cache when receiving a request to sync. that can be overridden
     *
     */
    cachePredicate(ts: any): (packet: any) => boolean;
    /**
     * A connection was made, add the peer to the local list of known
     * peers and call the onConnection if it is defined by the user.
     *
     * @return {undefined}
     * @ignore
     */
    _onConnection(packet: any, peerId: any, port: any, address: any, proxy: any, socket: any): undefined;
    /**
     * Received a Sync Packet
     * @return {undefined}
     * @ignore
     */
    _onSync(packet: any, port: any, address: any): undefined;
    /**
     * Received a Query Packet
     *
     * a -> b -> c -> (d) -> c -> b -> a
     *
     * @return {undefined}
     * @example
     *
     * ```js
     * peer.onQuery = (packet) => {
     *   //
     *   // read a database or something
     *   //
     *   return {
     *     message: Buffer.from('hello'),
     *     publicKey: '',
     *     privateKey: ''
     *   }
     * }
     * ```
     */
    _onQuery(packet: any, port: any, address: any): undefined;
    /**
     * Received a Ping Packet
     * @return {undefined}
     * @ignore
     */
    _onPing(packet: any, port: any, address: any): undefined;
    /**
     * Received a Pong Packet
     * @return {undefined}
     * @ignore
     */
    _onPong(packet: any, port: any, address: any): undefined;
    reflectionFirstReponderTimeout: number;
    /**
     * Received an Intro Packet
     * @return {undefined}
     * @ignore
     */
    _onIntro(packet: any, port: any, address: any, _: any, opts?: {
        attempts: number;
    }): undefined;
    socketPool: any[];
    /**
     * Received an Join Packet
     * @return {undefined}
     * @ignore
     */
    _onJoin(packet: any, port: any, address: any, _data: any): undefined;
    /**
     * Received an Publish Packet
     * @return {undefined}
     * @ignore
     */
    _onPublish(packet: any, port: any, address: any, _data: any): undefined;
    /**
     * Received an Stream Packet
     * @return {undefined}
     * @ignore
     */
    _onStream(packet: any, port: any, address: any, _data: any): undefined;
    /**
     * Received any packet on the probe port to determine the firewall:
     * are you port restricted, host restricted, or unrestricted.
     * @return {undefined}
     * @ignore
     */
    _onProbeMessage(data: any, { port, address }: {
        port: any;
        address: any;
    }): undefined;
    /**
     * When a packet is received it is decoded, the packet contains the type
     * of the message. Based on the message type it is routed to a function.
     * like WebSockets, don't answer queries unless we know its another SRP peer.
     *
     * @param {Buffer|Uint8Array} data
     * @param {{ port: number, address: string }} info
     */
    _onMessage(data: Buffer | Uint8Array, { port, address }: {
        port: number;
        address: string;
    }): Promise<undefined>;
}
export default Peer;
import { Packet } from './packets.js';
import { sha256 } from './packets.js';
import { Cache } from './cache.js';
import { Encryption } from './encryption.js';
import * as NAT from './nat.js';
import { Buffer } from '../buffer.js';
import { PacketPing } from './packets.js';
import { PacketPublish } from './packets.js';
export { Packet, sha256, Cache, Encryption, NAT };
