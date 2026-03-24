export function createServer(options: any, connectionListener: any): TLSServer;
/**
 * Establish a TLS client connection.
 *
 * @param {number|TlsConnectOptions} options
 * @param {(err?: Error) => void} [cb] Called once on success or failure.
 * @returns {TLSSocket}
 */
export function connect(options: number | TlsConnectOptions, cb?: (err?: Error) => void): TLSSocket;
/**
 * Set or extend runtime TLS certificate pins at runtime.
 *
 * Pins use the same format as the static `tls_pins` config:
 *
 *   <host> sha256/<base64>
 *
 * Invalid entries throw a `TypeError`.
 *
 * @param {string|string[]} pins - a string or array of pin lines
 * @param {TlsPinsOptions} [options]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function setTlsPins(pins: string | string[], options?: TlsPinsOptions): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Get the current runtime TLS pins configuration.
 *
 * @returns {Promise<{ value: string }>}
 */
export function getTlsPins(): Promise<{
    value: string;
}>;
/**
 * Get the active runtime TLS provider name.
 *
 * @returns {Promise<{ provider: string }>}
 */
export function getTlsProvider(): Promise<{
    provider: string;
}>;
/**
 * Clear all runtime TLS pins.
 *
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function clearTlsPins(): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Create a `sha256/<base64>` pin from a leaf certificate DER payload.
 *
 * @param {Buffer|TypedArray|DataView|ArrayBuffer} der
 * @returns {Promise<string>}
 */
export function createTlsPinFromCertificateDer(der: Buffer | any | DataView | ArrayBuffer): Promise<string>;
/**
 * Create a `sha256/<base64>` pin from a PEM-encoded certificate.
 *
 * @param {string} pem
 * @returns {Promise<string>}
 */
export function createTlsPinFromCertificatePem(pem: string): Promise<string>;
/**
 * Get configured runtime TLS pins for a host.
 *
 * @param {string} host
 * @returns {Promise<{ host: string, configured: boolean, pins: string[] }>}
 */
export function getTlsPinsForHost(host: string): Promise<{
    host: string;
    configured: boolean;
    pins: string[];
}>;
/**
 * Replace runtime TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function setTlsPinsForHost(host: string, pins: string | string[]): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Add one or more runtime TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function addTlsPinsForHost(host: string, pins: string | string[]): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Remove one or more runtime TLS pins for a single host.
 * When `pins` is omitted, removes the host entry entirely.
 *
 * @param {string} host
 * @param {string|string[]} [pins]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function removeTlsPinsForHost(host: string, pins?: string | string[]): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * @typedef {'append'|'replace'} WebViewTlsPinsMode
 *
 * @typedef {Object} WebViewTlsPinsOptions
 * @property {WebViewTlsPinsMode} [mode='append']
 */
/**
 * Configure WebView TLS certificate pins at runtime.
 *
 * This updates the process-wide `webview_tls_pins` configuration and refreshes
 * all active window/bridge configs so platform WebViews immediately see the
 * new pins. Pins use the same format as the static `webview_tls_pins` config:
 *
 *   <host> sha256/<base64>
 *
 * Invalid entries throw a `TypeError`.
 *
 * @param {string|string[]} pins - a string or array of pin lines
 * @param {WebViewTlsPinsOptions} [options]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function setWebViewTlsPins(pins: string | string[], options?: WebViewTlsPinsOptions): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Get the current WebView TLS pins configuration.
 *
 * @returns {Promise<{ value: string }>}
 */
export function getWebViewTlsPins(): Promise<{
    value: string;
}>;
/**
 * Clear all WebView TLS pins.
 *
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function clearWebViewTlsPins(): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Get configured WebView TLS pins for a host.
 *
 * @param {string} host
 * @returns {Promise<{ host: string, configured: boolean, pins: string[] }>}
 */
export function getWebViewTlsPinsForHost(host: string): Promise<{
    host: string;
    configured: boolean;
    pins: string[];
}>;
/**
 * Replace WebView TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function setWebViewTlsPinsForHost(host: string, pins: string | string[]): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Add one or more WebView TLS pins for a single host.
 *
 * @param {string} host
 * @param {string|string[]} pins
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function addWebViewTlsPinsForHost(host: string, pins: string | string[]): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * Remove one or more WebView TLS pins for a single host.
 * When `pins` is omitted, removes the host entry entirely.
 *
 * @param {string} host
 * @param {string|string[]} [pins]
 * @returns {Promise<{ ok: boolean, value: string }>}
 */
export function removeWebViewTlsPinsForHost(host: string, pins?: string | string[]): Promise<{
    ok: boolean;
    value: string;
}>;
/**
 * @typedef {Object} TlsHandshakeInfo
 * @property {string} [id]
 * @property {string} [clientId]
 * @property {string} [hostname]
 * @property {string} [protocol]
 * @property {string} [cipher]
 * @property {string} [alpn]
 * @property {string} [alpnProtocol]
 * @property {string} [provider]
 * @property {string} [subject]
 * @property {string[]} [sans]
 * @property {string} [peerPin]
 */
export class TLSSocket extends EventEmitter {
    /**
     * @param {string|number|bigint} [id]
     */
    constructor(id?: string | number | bigint);
    /** @type {string} */
    id: string;
    /** @type {TlsHandshakeInfo|null} */
    handshake: TlsHandshakeInfo | null;
    /** @type {string|undefined} */
    hostname: string | undefined;
    /** @type {string|undefined} */
    provider: string | undefined;
    /** @type {string|undefined} */
    protocol: string | undefined;
    /** @type {string|undefined} */
    cipher: string | undefined;
    /** @type {string|undefined} */
    alpn: string | undefined;
    /** @type {string|undefined} */
    alpnProtocol: string | undefined;
    /** @type {string|undefined} */
    subject: string | undefined;
    /** @type {string[]|undefined} */
    sans: string[] | undefined;
    /** @type {string|undefined} */
    peerPin: string | undefined;
    /**
     * @param {TlsHandshakeInfo} info
     */
    _setHandshake(info: TlsHandshakeInfo): void;
    /**
     * @param {Buffer|Uint8Array|ArrayBuffer|DataView|string} chunk
     * @param {(err?: Error) => void} [cb]
     * @returns {boolean}
     */
    write(chunk: Buffer | Uint8Array | ArrayBuffer | DataView | string, cb?: (err?: Error) => void): boolean;
    /**
     * @param {Buffer|Uint8Array|ArrayBuffer|DataView|string|(() => void)} [chunk]
     * @param {() => void} [cb]
     */
    end(chunk?: Buffer | Uint8Array | ArrayBuffer | DataView | string | (() => void), cb?: () => void): void;
    /**
     * @returns {void}
     */
    destroy(): void;
}
export class TLSServer extends EventEmitter {
    constructor(options: {}, connectionListener: any);
    /** @type {string} */
    id: string;
    /** @type {boolean} */
    _listening: boolean;
    /** @type {Set<TLSSocket>} */
    _clients: Set<TLSSocket>;
    /** @type {Map<string, TLSSocket>} */
    _clientById: Map<string, TLSSocket>;
    /** @type {any} */
    _options: any;
    listen(port: any, host: string, backlog: number, cb: any): this;
    _ondata: (ev: any) => void;
    _onsecure: (ev: any) => void;
    close(cb: any): Promise<void>;
}
declare namespace _default {
    export { TLSSocket };
    export { TLSServer };
    export { createServer };
    export { connect };
    export { setTlsPins };
    export { getTlsPins };
    export { getTlsProvider };
    export { clearTlsPins };
    export { getTlsPinsForHost };
    export { setTlsPinsForHost };
    export { addTlsPinsForHost };
    export { removeTlsPinsForHost };
    export { createTlsPinFromCertificateDer };
    export { createTlsPinFromCertificatePem };
    export { setWebViewTlsPins };
    export { getWebViewTlsPins };
    export { clearWebViewTlsPins };
    export { getWebViewTlsPinsForHost };
    export { setWebViewTlsPinsForHost };
    export { addWebViewTlsPinsForHost };
    export { removeWebViewTlsPinsForHost };
}
export default _default;
export type WebViewTlsPinsMode = "append" | "replace";
export type WebViewTlsPinsOptions = {
    mode?: WebViewTlsPinsMode;
};
export type TlsHandshakeInfo = {
    id?: string;
    clientId?: string;
    hostname?: string;
    protocol?: string;
    cipher?: string;
    alpn?: string;
    alpnProtocol?: string;
    provider?: string;
    subject?: string;
    sans?: string[];
    peerPin?: string;
};
export type TlsPinsMode = "append" | "replace";
export type TlsPinsOptions = {
    mode?: TlsPinsMode;
};
export type TlsConnectOptions = {
    host: string;
    port: number;
    id?: string | number | bigint;
    /**
     * TLS SNI server name (defaults to `host`)
     */
    servername?: string;
    /**
     * Alias for `servername`
     */
    serverName?: string;
    rejectUnauthorized?: boolean;
    /**
     * PEM-encoded CA bundle
     */
    ca?: string;
    /**
     * PEM-encoded client certificate
     */
    cert?: string;
    /**
     * PEM-encoded client private key
     */
    key?: string;
    /**
     * Passphrase for `key` (if encrypted)
     */
    keyPassphrase?: string;
    /**
     * Alias for `keyPassphrase`
     */
    passphrase?: string;
    alpnProtocols?: string[];
    minVersion?: string;
    maxVersion?: string;
    ciphers?: string[];
    /**
     * TLS pin lines or pin tokens
     */
    pins?: string | string[];
    /**
     * Pin merge mode
     */
    pinsMode?: TlsPinsMode;
};
import { Buffer } from './buffer.js';
import { EventEmitter } from './events.js';
