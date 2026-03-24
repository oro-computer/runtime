/**
 * Look up a language name or code by query.
 * @param {string} query
 * @param {object=} [options]
 * @param {boolean=} [options.strict = false]
 * @return {?LanguageQueryResult[]}
 */
export function lookup(query: string, options?: object | undefined, ...args: any[]): LanguageQueryResult[] | null;
/**
 * Describe a language by tag
 * @param {string} query
 * @param {object=} [options]
 * @param {boolean=} [options.strict = true]
 * @return {?LanguageDescription[]}
 */
export function describe(query: string, options?: object | undefined): LanguageDescription[] | null;
/**
 * A list of ISO 639-1 language names.
 * @type {string[]}
 */
export const names: string[];
/**
 * A list of ISO 639-1 language codes.
 * @type {string[]}
 */
export const codes: string[];
/**
 * A list of RFC 5646 language tag identifiers.
 * @see {@link http://tools.ietf.org/html/rfc5646}
 */
export const tags: Enumeration;
/**
 * A list of RFC 5646 language tag titles corresponding
 * to language tags.
 * @see {@link http://tools.ietf.org/html/rfc5646}
 */
export const descriptions: Enumeration;
/**
 * A container for a language query response containing an ISO language
 * name and code.
 * @see {@link https://www.sitepoint.com/iso-2-letter-language-codes}
 */
export class LanguageQueryResult {
    /**
     * `LanguageQueryResult` class constructor.
     * @param {string} code
     * @param {string} name
     * @param {string[]} [tags]
     */
    constructor(code: string, name: string, tags?: string[]);
    /**
     * The language code corresponding to the query.
     * @type {string}
     */
    get code(): string;
    /**
     * The language name corresponding to the query.
     * @type {string}
     */
    get name(): string;
    /**
     * The language tags corresponding to the query.
     * @type {string[]}
     */
    get tags(): string[];
    /**
     * JSON represenation of a `LanguageQueryResult` instance.
     * @return {{
     *   code: string,
     *   name: string,
     *   tags: string[]
     * }}
     */
    toJSON(): {
        code: string;
        name: string;
        tags: string[];
    };
    /**
     * Internal inspect function.
     * @ignore
     * @return {LanguageQueryResult}
     */
    inspect(): LanguageQueryResult;
    #private;
}
/**
 * A container for a language code, tag, and description.
 */
export class LanguageDescription {
    /**
     * `LanguageDescription` class constructor.
     * @param {string} code
     * @param {string} tag
     * @param {string} description
     */
    constructor(code: string, tag: string, description: string);
    /**
     * The language code corresponding to the language
     * @type {string}
     */
    get code(): string;
    /**
     * The language tag corresponding to the language.
     * @type {string}
     */
    get tag(): string;
    /**
     * The language description corresponding to the language.
     * @type {string}
     */
    get description(): string;
    /**
     * JSON represenation of a `LanguageDescription` instance.
     * @return {{
     *   code: string,
     *   tag: string,
     *   description: string
     * }}
     */
    toJSON(): {
        code: string;
        tag: string;
        description: string;
    };
    /**
     * Internal inspect function.
     * @ignore
     * @return {LanguageDescription}
     */
    inspect(): LanguageDescription;
    #private;
}
declare namespace _default {
    export { codes };
    export { describe };
    export { lookup };
    export { names };
    export { tags };
}
export default _default;
import Enumeration from './enumeration.js';
