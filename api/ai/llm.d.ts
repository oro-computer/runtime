/**
 * @typedef {{ name: string, }} ModelOptions
 * @typedef {{ directory?: string, gpuLayerCount?: number }} ModelLoadOptions
 */
export class Model {
    /**
     * @param {ModelOptions} options
     */
    constructor(options: ModelOptions);
    /**
     * @type {string}
     */
    get id(): string;
    /**
     * @type {string}
     */
    get name(): string;
    /**
     * @type {Promise}
     */
    get ready(): Promise<any>;
    /**
     * `true` if the model is loaded, otherwise `false`.
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * Loads the model it not already loaded.
     * @param {ModelLoadOptions=} [options]
     */
    load(options?: ModelLoadOptions | undefined): Promise<any>;
    toJSON(): {
        name: string;
    };
    #private;
}
/**
 * @typedef {{ name: string, }} LoRAOptions
 * @typedef {{ directory?: string, id?: string|number }} LoRALoadOptions
 * @typedef {{ scale?: number }} LoraAttachOptions
 */
export class LoRA {
    /**
     * @param {Model} model
     * @param {LoRAOptions} options
     */
    constructor(model: Model, options: LoRAOptions);
    /**
     * @type {string}
     */
    get id(): string;
    /**
     * @type {string}
     */
    get name(): string;
    /**
     * @type {Promise}
     */
    get ready(): Promise<any>;
    /**
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * @type {Model}
     */
    get model(): Model;
    /**
     * Load this adapter. Pass `options.id` to reference an already-loaded LoRA
     * without providing `name`/`model` metadata.
     * @param {LoRALoadOptions=} [options]
     */
    load(options?: LoRALoadOptions | undefined): Promise<any>;
    /**
     * Attach a LoRA to a context.
     * @param {Context} context
     * @param {LoraAttachOptions=} [options]
     * @return {Promise}
     */
    attach(context: Context, options?: LoraAttachOptions | undefined): Promise<any>;
    /**
     * @param {Context} context
     * @return {Promise}
     */
    detach(context: Context): Promise<any>;
    toJSON(): {
        name: string;
        model: {
            name: string;
        };
    };
    #private;
}
/**
 * @typedef {
 *   context: Context,
 *   model: Model,
 *   lora: LoRA
 * {}} LoRAAttachmentOptions
 */
export class LoRAAttachment {
    /**
     * @param {LoRAAttachmentOptions} options
     */
    constructor(options: LoRAAttachmentOptions);
    /**
     * @type {Context}
     */
    get context(): Context;
    /**
     * @type {Model}
     */
    get model(): Model;
    /**
     * @type {LoRA}
     */
    get lora(): LoRA;
    toJSON(): {
        context: any;
        model: any;
        lora: any;
    };
    #private;
}
/**
 * @typedef {{
 *   size?: number,
 *   minP?: number,
 *   temp?: number,
 *   topK?: number,
 *   topP?: number,
 *   id?: string
 * }} ContextOptions
 *
 * @typedef {{
 *   id: string,
 *   size: number,
 *   used: number
 * }} ContextStats
 */
export class Context {
    /**
     * @param {ContextOptions=} [options]
     */
    constructor(options?: ContextOptions | undefined);
    /**
     * @type {string}
     */
    get id(): string;
    /**
     * @type {number}
     */
    get size(): number;
    /**
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * @type {Model}
     */
    get model(): Model;
    /**
     * @type {Promise}
     */
    get ready(): Promise<any>;
    /**
     * @type {ContextOptions}
     */
    get options(): ContextOptions;
    /**
     * @type {LoRAAttachment[]}
     */
    get attachments(): LoRAAttachment[];
    /**
     * @type {LoRA[]}
     */
    get adapters(): LoRA[];
    /**
     * @param {Model} model
     * @param {ContextOptions=} [options]
     * @return {Promise}
     */
    load(model: Model, options?: ContextOptions | undefined): Promise<any>;
    /**
     * @return {Promise<ContextStats>}
     */
    stats(): Promise<ContextStats>;
    toJSON(): {
        id: string;
        size: number;
        model: {
            name: string;
        };
    };
    #private;
}
declare namespace _default {
    export { Model };
    export { LoRA };
    export { LoRAAttachment };
    export { Context };
}
export default _default;
export type ModelOptions = {
    name: string;
};
export type ModelLoadOptions = {
    directory?: string;
    gpuLayerCount?: number;
};
export type LoRAOptions = {
    name: string;
};
export type LoRALoadOptions = {
    directory?: string;
    id?: string | number;
};
export type LoraAttachOptions = {
    scale?: number;
};
/**
 * : Context,
 *   model: Model,
 *   lora: LoRA
 * {}} LoRAAttachmentOptions
 */
export type context = any;
export type ContextOptions = {
    size?: number;
    minP?: number;
    temp?: number;
    topK?: number;
    topP?: number;
    id?: string;
};
export type ContextStats = {
    id: string;
    size: number;
    used: number;
};
