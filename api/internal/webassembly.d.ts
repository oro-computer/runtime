/**
 * The `instantiateStreaming()` function compiles and instantiates a WebAssembly
 * module directly from a streamed source.
 * @ignore
 * @param {Response} response
 * @param {=object} [importObject]
 * @return {Promise<WebAssembly.Instance>}
 */
export function instantiateStreaming(response: Response, importObject?: any): Promise<WebAssembly.Instance>;
/**
 * The `compileStreaming()` function compiles and instantiates a WebAssembly
 * module directly from a streamed source.
 * @ignore
 * @param {Response} response
 * @return {Promise<WebAssembly.Module>}
 */
export function compileStreaming(response: Response): Promise<WebAssembly.Module>;
declare namespace _default {
    export { instantiateStreaming };
}
export default _default;
