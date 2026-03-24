/**
 * @module enumeration
 * This module provides a data structure for enumerated unique values.
 */
/**
 * A container for enumerated values.
 */
export class Enumeration extends Set<any> {
    /**
     * Creates an `Enumeration` instance from arguments.
     * @param {...any} values
     * @return {Enumeration}
     */
    static from(...values: any[]): Enumeration;
    /**
     * `Enumeration` class constructor.
     * @param {any[]} values
     * @param {object=} [options = {}]
     * @param {number=} [options.start = 0]
     */
    constructor(values: any[], options?: object | undefined);
    /**
     * @type {number}
     */
    get length(): number;
    /**
     * Returns `true` if enumeration contains `value`. An alias
     * for `Set.prototype.has`.
     * @return {boolean}
     */
    contains(value: any): boolean;
    /**
     * @ignore
     */
    add(): void;
    /**
     * @ignore
     */
    delete(): void;
    /**
     * JSON represenation of a `Enumeration` instance.
     * @ignore
     * @return {string[]}
     */
    toJSON(): string[];
    /**
     * Internal inspect function.
     * @ignore
     * @return {LanguageQueryResult}
     */
    inspect(): LanguageQueryResult;
}
export default Enumeration;
