/**
 * The magic bytes prefixing every packet. They are the
 * 2nd, 3rd, 5th, and 7th, prime numbers.
 * @type {number[]}
 */
export const MAGIC_BYTES_PREFIX: number[];
/**
 * The version of the protocol.
 */
export const VERSION: 6;
/**
 * The size in bytes of the prefix magic bytes.
 */
export const MAGIC_BYTES: 4;
/**
 * The maximum size of the user message.
 */
export const MESSAGE_BYTES: 1024;
/**
 * The cache TTL in milliseconds.
 */
export const CACHE_TTL: number;
export namespace PACKET_SPEC {
    namespace type {
        let bytes: number;
        let encoding: string;
    }
    namespace version {
        let bytes_1: number;
        export { bytes_1 as bytes };
        let encoding_1: string;
        export { encoding_1 as encoding };
        export { VERSION as default };
    }
    namespace clock {
        let bytes_2: number;
        export { bytes_2 as bytes };
        let encoding_2: string;
        export { encoding_2 as encoding };
        let _default: number;
        export { _default as default };
    }
    namespace hops {
        let bytes_3: number;
        export { bytes_3 as bytes };
        let encoding_3: string;
        export { encoding_3 as encoding };
        let _default_1: number;
        export { _default_1 as default };
    }
    namespace index {
        let bytes_4: number;
        export { bytes_4 as bytes };
        let encoding_4: string;
        export { encoding_4 as encoding };
        let _default_2: number;
        export { _default_2 as default };
        export let signed: boolean;
    }
    namespace ttl {
        let bytes_5: number;
        export { bytes_5 as bytes };
        let encoding_5: string;
        export { encoding_5 as encoding };
        export { CACHE_TTL as default };
    }
    namespace clusterId {
        let bytes_6: number;
        export { bytes_6 as bytes };
        let encoding_6: string;
        export { encoding_6 as encoding };
        let _default_3: number[];
        export { _default_3 as default };
    }
    namespace subclusterId {
        let bytes_7: number;
        export { bytes_7 as bytes };
        let encoding_7: string;
        export { encoding_7 as encoding };
        let _default_4: number[];
        export { _default_4 as default };
    }
    namespace previousId {
        let bytes_8: number;
        export { bytes_8 as bytes };
        let encoding_8: string;
        export { encoding_8 as encoding };
        let _default_5: number[];
        export { _default_5 as default };
    }
    namespace packetId {
        let bytes_9: number;
        export { bytes_9 as bytes };
        let encoding_9: string;
        export { encoding_9 as encoding };
        let _default_6: number[];
        export { _default_6 as default };
    }
    namespace nextId {
        let bytes_10: number;
        export { bytes_10 as bytes };
        let encoding_10: string;
        export { encoding_10 as encoding };
        let _default_7: number[];
        export { _default_7 as default };
    }
    namespace usr1 {
        let bytes_11: number;
        export { bytes_11 as bytes };
        let _default_8: number[];
        export { _default_8 as default };
    }
    namespace usr2 {
        let bytes_12: number;
        export { bytes_12 as bytes };
        let _default_9: number[];
        export { _default_9 as default };
    }
    namespace usr3 {
        let bytes_13: number;
        export { bytes_13 as bytes };
        let _default_10: number[];
        export { _default_10 as default };
    }
    namespace usr4 {
        let bytes_14: number;
        export { bytes_14 as bytes };
        let _default_11: number[];
        export { _default_11 as default };
    }
    namespace message {
        let bytes_15: number;
        export { bytes_15 as bytes };
        let _default_12: number[];
        export { _default_12 as default };
    }
    namespace sig {
        let bytes_16: number;
        export { bytes_16 as bytes };
        let _default_13: number[];
        export { _default_13 as default };
    }
}
/**
 * The size in bytes of the total packet frame and message.
 */
export const PACKET_BYTES: number;
/**
 * The maximum distance that a packet can be replicated.
 */
export const MAX_HOPS: 16;
export function validateMessage(o: object, constraints: {
    [key: string]: constraint;
}): void;
/**
 * Computes a SHA-256 hash of input returning a hex encoded string.
 * @type {function(string|Buffer|Uint8Array): Promise<string>}
 */
export const sha256: (arg0: string | Buffer | Uint8Array) => Promise<string>;
export function decode(buf: Buffer): Packet;
export function getTypeFromBytes(buf: any): any;
export class Packet {
    static ttl: number;
    static maxLength: number;
    /**
     * Returns an empty `Packet` instance.
     * @return {Packet}
     */
    static empty(): Packet;
    /**
     * @param {Packet|object} packet
     * @return {Packet}
     */
    static from(packet: Packet | object): Packet;
    /**
     * Determines if input is a packet.
     * @param {Buffer|Uint8Array|number[]|object|Packet} packet
     * @return {boolean}
     */
    static isPacket(packet: Buffer | Uint8Array | number[] | object | Packet): boolean;
    /**
     */
    static encode(p: any): Promise<Uint8Array<any>>;
    static decode(buf: any): Packet;
    /**
     * `Packet` class constructor.
     * @param {Packet|object?} options
     */
    constructor(options?: Packet | (object | null));
    /**
     * @param {Packet} packet
     * @return {Packet}
     */
    copy(): Packet;
    timestamp: any;
    isComposed: any;
    isReconciled: any;
    meta: any;
}
export class PacketPing extends Packet {
    static type: number;
}
export class PacketPong extends Packet {
    static type: number;
}
export class PacketIntro extends Packet {
    static type: number;
}
export class PacketJoin extends Packet {
    static type: number;
}
export class PacketPublish extends Packet {
    static type: number;
}
export class PacketStream extends Packet {
    static type: number;
}
export class PacketSync extends Packet {
    static type: number;
}
export class PacketQuery extends Packet {
    static type: number;
}
export default Packet;
export type constraint = {
    type: string;
    required?: boolean;
    /**
     * optional validator fn returning boolean
     */
    assert?: Function;
};
import { Buffer } from '../buffer.js';
