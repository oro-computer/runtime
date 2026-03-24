/**
 * @typedef {import('./llm.js').ModelOptions} ModelOptions
 * @typedef {import('./llm.js').ModelLoadOptions} ModelLoadOptions
 * @typedef {import('./llm.js').ContextOptions} ContextOptions
 */
/**
 * @typedef {{
 *   prompt?: string,
 *   antiprompts?: (string|Set<string>)[]
 * }} GenerateOptions
 */
export class ChatMessageEvent {
    /**
     * @param {string} type
     * @param {MessageEventInit & { finished?: boolean }} options
     */
    constructor(type: string, options: MessageEventInit & {
        finished?: boolean;
    });
    get finished(): boolean;
    #private;
}
/**
 * @typedef {{
 *   id: string,
 *   role: string,
 *   content: string
 * }} MessageOptions
 */
export class Message {
    constructor(options: any);
    /**
     * @type {string}
     */
    get id(): string;
    /**
     * @type {string}
     */
    get role(): string;
    /**
     * @type {string}
     */
    get content(): string;
    #private;
}
/**
 * @typedef {{
 *   id?: string,
 *   prompt?: string,
 *   antiprompts?: Set<string>|string[]
 * }} SessionOptions
 */
export class Session extends EventTarget {
    [x: number]: (options: any) => {
        args: any[];
        handle(id: any, conduit: any): Promise<void>;
    };
    /**
     * @param {Context} context
     * @param {SessionOptions=} [options]
     */
    constructor(context: Context, options?: SessionOptions | undefined);
    /**
     * @type {string}
     */
    get id(): string;
    /**
     * @type {string}
     */
    get prompt(): string;
    /**
     * @type {Context}
     */
    get context(): Context;
    /**
     * @type {Conduit}
     */
    get conduit(): Conduit;
    /**
     * @type {boolean}
     */
    get started(): boolean;
    /**
     * @type {boolean}
     */
    get loaded(): boolean;
    /**
     * @type {boolean}
     */
    get generating(): boolean;
    /**
     * @type {Message[]}
     */
    get messages(): Message[];
    /**
     * @type {Set<string>}
     */
    get antiprompts(): Set<string>;
    /**
     * @param {Model} model
     * @param {(ModelLoadOptions & ContextOptions)=} [options]
     * @return {Promise}
     */
    load(model: Model, options?: (ModelLoadOptions & ContextOptions) | undefined): Promise<any>;
    /**
     * @return {Promise}
     */
    start(): Promise<any>;
    /**
     * @param {GenerateOptions=} [options]
     * @return {Promise<object>}
     */
    generate(options?: GenerateOptions | undefined): Promise<object>;
    message(options: any): Promise<any>;
    #private;
}
/**
 * @typedef {SessionOptions & {
 *   model: string | (ModelOptions & ModelLoadOptions),
 *   prompt?: string,
 *   context?: ContextOptions
 * }} ChatOptions
 */
export class Chat extends Session {
    /**
     * @param {ChatOptions} options
     */
    constructor(options: ChatOptions);
    /**
     * @type {Model}
     */
    get model(): Model;
    /**
     * @type {Promise}
     */
    get ready(): Promise<any>;
    /**
     * @return {Promise}
     */
    load(): Promise<any>;
    #private;
}
declare namespace _default {
    export { Message };
    export { Session };
    export { Chat };
}
export default _default;
export type ModelOptions = import("./llm.js").ModelOptions;
export type ModelLoadOptions = import("./llm.js").ModelLoadOptions;
export type ContextOptions = import("./llm.js").ContextOptions;
export type GenerateOptions = {
    prompt?: string;
    antiprompts?: (string | Set<string>)[];
};
export type MessageOptions = {
    id: string;
    role: string;
    content: string;
};
export type SessionOptions = {
    id?: string;
    prompt?: string;
    antiprompts?: Set<string> | string[];
};
export type ChatOptions = SessionOptions & {
    model: string | (ModelOptions & ModelLoadOptions);
    prompt?: string;
    context?: ContextOptions;
};
import { Context } from './llm.js';
import { Conduit } from '../conduit.js';
import { Model } from './llm.js';
