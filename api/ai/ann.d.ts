/**
 * Create a new ANN model.
 * @param {ConstructorParameters<typeof Network>[0]} options
 * @returns {Promise<Network>}
 */
export function create(options: ConstructorParameters<typeof Network>[0]): Promise<Network>;
/**
 * Load a model from disk.
 * @param {string} path
 * @param {{name?:string}} [options]
 * @returns {Promise<Network>}
 */
export function load(path: string, options?: {
    name?: string;
}): Promise<Network>;
/**
 * Retrieve metadata for registered ANN models.
 * @returns {Promise<Array<object>>}
 */
export function list(): Promise<Array<object>>;
/**
 * Remove a model by instance, id, or name.
 * @param {Network|number|string} target
 * @returns {Promise<boolean>}
 */
export function remove(target: Network | number | string): Promise<boolean>;
/**
 * Supported loss function identifiers.
 */
export type LossFunction = string;
/**
 * Supported loss function identifiers.
 * @enum {string}
 */
export const LossFunction: Readonly<{
    CrossEntropy: "crossEntropy";
    MeanSquaredError: "meanSquaredError";
}>;
/**
 * Represents a managed ANN model within the runtime.
 */
export class Network {
    /**
     * Create a new ANN model inside the runtime.
     * @param {{
     *   name?: string,
     *   inputSize: number,
     *   outputSize: number,
     *   outputActivation?: string,
     *   hiddenLayers?: Array<{size:number, activation?:string}>
     * }} options
     * @returns {Promise<Network>}
     */
    static create(options: {
        name?: string;
        inputSize: number;
        outputSize: number;
        outputActivation?: string;
        hiddenLayers?: Array<{
            size: number;
            activation?: string;
        }>;
    }): Promise<Network>;
    /**
     * Load an ANN model from disk and register it with the runtime.
     * @param {string} path Absolute path to a serialized model file.
     * @param {{name?:string}} [options]
     * @returns {Promise<Network>}
     */
    static load(path: string, options?: {
        name?: string;
    }): Promise<Network>;
    /**
     * List registered ANN models.
     * @returns {Promise<Array<object>>}
     */
    static list(): Promise<Array<object>>;
    constructor(metadata?: any);
    /** @returns {number|null} Unique model identifier assigned by the runtime. */
    get id(): number | null;
    /** @returns {string} Model name assigned during creation (optional). */
    get name(): string;
    /** @returns {number} Number of input features per example. */
    get inputSize(): number;
    /** @returns {number} Number of output units per example. */
    get outputSize(): number;
    /** @returns {string} Activation function applied on the output layer. */
    get outputActivation(): string;
    /** @returns {Array<{size:number, activation:string}>} Hidden layer definitions. */
    get hiddenLayers(): Array<{
        size: number;
        activation: string;
    }>;
    /**
     * Persist the model state to disk.
     * @param {string} path Absolute path where the model should be stored.
     * @returns {Promise<boolean>}
     */
    save(path: string): Promise<boolean>;
    /**
     * Train the network with labeled data.
     * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} features
     * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} labels
     * @param {{
     *   loss?: string,
     *   batchSize?: number,
     *   learningRate?: number,
     *   searchTime?: number,
     *   regularizationStrength?: number,
     *   momentumFactor?: number,
     *   maxEpochs?: number,
     *   shuffle?: boolean,
     *   verbose?: boolean,
     *   featureColumns?: number,
     *   featureRows?: number,
     *   labelColumns?: number,
     *   labelRows?: number
     * }} [options]
     * @returns {Promise<{loss:number,accuracy:number,epochs:number,durationMs:number}>}
     */
    train(features: ArrayLike<number> | Array<ArrayLike<number>> | Float32Array | {
        data: Float32Array;
        rows: number;
        columns: number;
    }, labels: ArrayLike<number> | Array<ArrayLike<number>> | Float32Array | {
        data: Float32Array;
        rows: number;
        columns: number;
    }, options?: {
        loss?: string;
        batchSize?: number;
        learningRate?: number;
        searchTime?: number;
        regularizationStrength?: number;
        momentumFactor?: number;
        maxEpochs?: number;
        shuffle?: boolean;
        verbose?: boolean;
        featureColumns?: number;
        featureRows?: number;
        labelColumns?: number;
        labelRows?: number;
    }): Promise<{
        loss: number;
        accuracy: number;
        epochs: number;
        durationMs: number;
    }>;
    /**
     * Run inference on the network.
     * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} input
     * @param {{rows?:number, columns?:number}} [options]
     * @returns {Promise<{rows:number,columns:number,logits:Float32Array,classes:Int32Array}>}
     */
    predict(input: ArrayLike<number> | Array<ArrayLike<number>> | Float32Array | {
        data: Float32Array;
        rows: number;
        columns: number;
    }, options?: {
        rows?: number;
        columns?: number;
    }): Promise<{
        rows: number;
        columns: number;
        logits: Float32Array;
        classes: Int32Array;
    }>;
    /**
     * Compute classification accuracy for labeled samples.
     * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} features
     * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} labels
     * @param {{featureColumns?:number,featureRows?:number,labelColumns?:number,labelRows?:number}} [options]
     * @returns {Promise<number>}
     */
    accuracy(features: ArrayLike<number> | Array<ArrayLike<number>> | Float32Array | {
        data: Float32Array;
        rows: number;
        columns: number;
    }, labels: ArrayLike<number> | Array<ArrayLike<number>> | Float32Array | {
        data: Float32Array;
        rows: number;
        columns: number;
    }, options?: {
        featureColumns?: number;
        featureRows?: number;
        labelColumns?: number;
        labelRows?: number;
    }): Promise<number>;
    /**
     * Destroy the network within the runtime.
     * @returns {Promise<boolean>}
     */
    remove(): Promise<boolean>;
}
declare namespace _default {
    export { Network };
    export { LossFunction };
    export { create };
    export { load };
    export { list };
    export { remove };
}
export default _default;
