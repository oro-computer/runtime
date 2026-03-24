/**
 * Get messages for `locale` pattern. This function could return many results
 * for various locales given a `locale` pattern. such as `fr`, which could
 * return results for `fr`, `fr-FR`, `fr-BE`, etc.
 * @ignore
 * @param {string} locale
 * @return {object[]}
 */
export function getMessagesForLocale(locale: string): object[];
/**
 * Returns user preferred ISO 639 language codes or RFC 5646 language tags.
 * @return {string[]}
 */
export function getAcceptLanguages(): string[];
/**
 * Returns the current user ISO 639 language code or RFC 5646 language tag.
 * @return {?string}
 */
export function getUILanguage(): string | null;
/**
 * Gets a localized message string for the specified message name.
 * @param {string} messageName
 * @param {object|string[]=} [substitutions = []]
 * @param {object=} [options]
 * @param {string=} [options.locale = null]
 * @see {@link https://developer.chrome.com/docs/extensions/reference/i18n/#type-LanguageCode}
 * @see {@link https://www.ibm.com/docs/en/rbd/9.5.1?topic=syslib-getmessage}
 * @return {?string}
 */
export function getMessage(messageName: string, substitutions?: (object | string[]) | undefined, options?: object | undefined): string | null;
/**
 * Gets a localized message description string for the specified message name.
 * @param {string} messageName
 * @param {object=} [options]
 * @param {string=} [options.locale = null]
 * @return {?string}
 */
export function getMessageDescription(messageName: string, options?: object | undefined): string | null;
/**
 * A cache of loaded locale messages.
 * @type {Map}
 */
export const cache: Map<any, any>;
/**
 * Default location of i18n locale messages
 * @type {string}
 */
export const DEFAULT_LOCALES_LOCATION: string;
/**
 * An enumeration of supported ISO 639 language codes or RFC 5646 language tags.
 * @type {Enumeration}
 * @see {@link https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/i18n/LanguageCode}
 * @see {@link https://developer.chrome.com/docs/extensions/reference/i18n/#type-LanguageCode}
 */
export const LanguageCode: Enumeration;
declare namespace _default {
    export { LanguageCode };
    export { getAcceptLanguages };
    export { getMessage };
    export { getUILanguage };
}
export default _default;
import Enumeration from './enumeration.js';
