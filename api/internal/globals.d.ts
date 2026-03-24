/**
 * Gets a runtime global value by name.
 * @ignore
 * @param {string} name
 * @return {any|null}
 */
export function get(name: string): any | null;
/**
 * Symbolic global registry
 * @ignore
 */
export class GlobalsRegistry {
    get global(): any;
    symbol(name: any): symbol;
    register(name: any, value: any): any;
    get(name: any): any;
}
export default registry;
declare const registry: any;
