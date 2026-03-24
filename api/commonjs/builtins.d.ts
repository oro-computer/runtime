/**
 * Defines a builtin module by name making a shallow copy of the
 * module exports.
 * @param {string}
 * @param {object} exports
 */
export function defineBuiltin(name: any, exports: object, copy?: boolean): void;
/**
 * Predicate to determine if a given module name is a builtin module.
 * @param {string} name
 * @param {{ builtins?: object }}
 * @return {boolean}
 */
export function isBuiltin(name: string, options?: any): boolean;
/**
 * Gets a builtin module by name.
 * @param {string} name
 * @param {{ builtins?: object }} [options]
 * @return {any}
 */
export function getBuiltin(name: string, options?: {
    builtins?: object;
}): any;
/**
 * A mapping of builtin modules
 * @type {object}
 */
export const builtins: object;
/**
 * Known runtime specific builtin modules.
 * @type {Set<string>}
 */
export const runtimeModules: Set<string>;
export default builtins;
