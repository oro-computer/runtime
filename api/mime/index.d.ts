/**
 * Look up a MIME type in various MIME databases.
 * @param {string} query
 * @return {Promise<DatabaseQueryResult[]>}
 */
export function lookup(query: string): Promise<DatabaseQueryResult[]>;
/**
 * Look up a MIME type in various MIME databases synchronously.
 * @param {string} query
 * @return {DatabaseQueryResult[]}
 */
export function lookupSync(query: string): DatabaseQueryResult[];
/**
 * A container for a database lookup query.
 */
export class DatabaseQueryResult {
    /**
     * `DatabaseQueryResult` class constructor.
     * @ignore
     * @param {Database|null} database
     * @param {string} name
     * @param {string} mime
     */
    constructor(database: Database | null, name: string, mime: string);
    /**
     * @type {string}
     */
    name: string;
    /**
     * @type {string}
     */
    mime: string;
    /**
     * @type {Database?}
     */
    database: Database | null;
}
/**
 * A container for MIME types by class (audio, video, text, etc)
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml}
 */
export class Database {
    /**
     * `Database` class constructor.
     * @param {string} name
     */
    constructor(name: string);
    /**
     * The name of the MIME database.
     * @type {string}
     */
    name: string;
    /**
     * The URL of the MIME database.
     * @type {URL}
     */
    url: URL;
    /**
     * The mapping of MIME name to the MIME "content type"
     * @type {Map}
     */
    map: Map<any, any>;
    /**
     * An index of MIME "content type" to the MIME name.
     * @type {Map}
     */
    index: Map<any, any>;
    /**
     * An enumeration of all database entries.
     * @return {Array<Array<string>>}
     */
    entries(): Array<Array<string>>;
    /**
     * Loads database MIME entries into internal map.
     * @return {Promise}
     */
    load(): Promise<any>;
    /**
     * Loads database MIME entries synchronously into internal map.
     */
    loadSync(): void;
    /**
     * Lookup MIME type by name or content type
     * @param {string} query
     * @return {Promise<DatabaseQueryResult[]>}
     */
    lookup(query: string): Promise<DatabaseQueryResult[]>;
    /**
     * Lookup MIME type by name or content type synchronously.
     * @param {string} query
     * @return {Promise<DatabaseQueryResult[]>}
     */
    lookupSync(query: string): Promise<DatabaseQueryResult[]>;
    /**
     * Queries database map and returns an array of results
     * @param {string} query
     * @return {DatabaseQueryResult[]}
     */
    query(query: string): DatabaseQueryResult[];
}
/**
 * A database of MIME types for 'application/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#application}
 */
export const application: Database;
/**
 * A database of MIME types for 'audio/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#audio}
 */
export const audio: Database;
/**
 * A database of MIME types for 'font/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#font}
 */
export const font: Database;
/**
 * A database of MIME types for 'image/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#image}
 */
export const image: Database;
/**
 * A database of MIME types for 'model/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#model}
 */
export const model: Database;
/**
 * A database of MIME types for 'multipart/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#multipart}
 */
export const multipart: Database;
/**
 * A database of MIME types for 'text/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#text}
 */
export const text: Database;
/**
 * A database of MIME types for 'video/' content types
 * @type {Database}
 * @see {@link https://www.iana.org/assignments/media-types/media-types.xhtml#video}
 */
export const video: Database;
/**
 * An array of known MIME databases. Custom databases can be added to this
 * array in userspace for lookup with `mime.lookup()`
 * @type {Database[]}
 */
export const databases: Database[];
declare namespace _default {
    export { Database };
    export { databases };
    export { lookup };
    export { lookupSync };
    export { MIMEParams };
    export { MIMEType };
    export { application };
    export { audio };
    export { font };
    export { image };
    export { model };
    export { multipart };
    export { text };
    export { video };
}
export default _default;
import { MIMEParams } from './params.js';
import { MIMEType } from './type.js';
