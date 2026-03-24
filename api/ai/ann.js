import ipc from '../ipc.js'

/**
 * @module ai/ann
 *
 * High-level interface for interacting with the native artificial neural
 * network (ANN) runtime backed by the embedded Cranium library. Provides
 * helpers to create, train, evaluate, and persist feed-forward neural
 * networks directly from JavaScript.
 */

const ROUTES = {
  create: 'ai.ann.model.create',
  load: 'ai.ann.model.load',
  save: 'ai.ann.model.save',
  remove: 'ai.ann.model.remove',
  list: 'ai.ann.model.list',
  train: 'ai.ann.model.train',
  predict: 'ai.ann.model.predict',
  accuracy: 'ai.ann.model.accuracy'
}

/**
 * Supported loss function identifiers.
 * @enum {string}
 */
export const LossFunction = Object.freeze({
  CrossEntropy: 'crossEntropy',
  MeanSquaredError: 'meanSquaredError'
})

const STATE = new WeakMap()

function createDefaultState () {
  return {
    id: null,
    name: '',
    inputSize: 0,
    outputSize: 0,
    outputActivation: 'linear',
    hiddenLayers: []
  }
}

function getState (network) {
  const state = STATE.get(network)
  if (!state) {
    throw new Error('ANN network state is not initialised')
  }
  return state
}

function ensureReady (network) {
  const state = getState(network)
  if (!Number.isInteger(state.id) || state.id <= 0) {
    throw new Error('ANN network is not registered in the runtime yet')
  }
  return state
}

function unwrapData (result) {
  if (!result || typeof result !== 'object') {
    return result || null
  }
  const first = Object.prototype.hasOwnProperty.call(result, 'data')
    ? result.data
    : result
  if (!first || typeof first !== 'object') {
    return first != null ? first : null
  }
  if (Object.prototype.hasOwnProperty.call(first, 'data')) {
    return first.data != null ? first.data : null
  }
  return first
}

function unwrapOk (result) {
  const payload = unwrapData(result)
  if (
    payload &&
    typeof payload === 'object' &&
    Object.prototype.hasOwnProperty.call(payload, 'ok')
  ) {
    return payload.ok
  }
  return payload
}

function toArrayBuffer (view) {
  if (view.byteOffset === 0 && view.byteLength === view.buffer.byteLength) {
    return view.buffer
  }
  return view.buffer.slice(view.byteOffset, view.byteOffset + view.byteLength)
}

function isMatrixDescriptor (value) {
  return (
    value &&
    typeof value === 'object' &&
    ArrayBuffer.isView(value.data) &&
    Number.isFinite(value.rows) &&
    Number.isFinite(value.columns)
  )
}

function normalizeMatrix (name, input, options = {}) {
  if (isMatrixDescriptor(input)) {
    return normalizeMatrix(name, input.data, {
      rows: input.rows,
      columns: input.columns
    })
  }

  let rows = Number.isFinite(options.rows) ? Number(options.rows) : null
  let columns = Number.isFinite(options.columns)
    ? Number(options.columns)
    : null

  const ensureShape = () => {
    if (!Number.isInteger(rows) || rows <= 0) {
      throw new TypeError(`${name}: rows must be a positive integer`)
    }
    if (!Number.isInteger(columns) || columns <= 0) {
      throw new TypeError(`${name}: columns must be a positive integer`)
    }
  }

  const assignVectorShape = (vectorLength) => {
    if (columns == null && rows == null) {
      throw new TypeError(
        `${name}: when providing a flat array you must specify either rows or columns`
      )
    }
    if (columns == null) {
      columns = vectorLength / rows
    } else if (rows == null) {
      rows = vectorLength / columns
    }
    if (!Number.isInteger(rows) || !Number.isInteger(columns)) {
      throw new TypeError(
        `${name}: rows * columns must equal the flattened length`
      )
    }
    ensureShape()
  }

  if (ArrayBuffer.isView(input)) {
    const typed =
      input instanceof Float32Array ? input : new Float32Array(input)
    assignVectorShape(typed.length)
    return { rows, columns, data: typed }
  }

  if (Array.isArray(input)) {
    if (input.length === 0) {
      throw new TypeError(`${name}: expected a non-empty array`)
    }

    if (Array.isArray(input[0])) {
      if (rows == null) rows = input.length
      if (columns == null) columns = input[0].length
      ensureShape()
      const result = new Float32Array(rows * columns)
      for (let r = 0; r < rows; r++) {
        const row = input[r]
        if (!Array.isArray(row) || row.length !== columns) {
          throw new TypeError(
            `${name}: row ${r} does not match expected column count (${columns})`
          )
        }
        result.set(row.map(Number), r * columns)
      }
      return { rows, columns, data: result }
    }

    const vector = new Float32Array(input)
    assignVectorShape(vector.length)
    return { rows, columns, data: vector }
  }

  throw new TypeError(
    `${name}: expected a Float32Array, ArrayBuffer view, or array-like`
  )
}

function encodeHiddenLayers (layers) {
  if (!Array.isArray(layers) || layers.length === 0) {
    return undefined
  }
  return layers.map((layer) => {
    if (!layer || typeof layer !== 'object') {
      throw new TypeError('hiddenLayers entries must be objects')
    }
    const size = Number(layer.size)
    if (!Number.isInteger(size) || size <= 0) {
      throw new TypeError(
        'hiddenLayers entries must include a positive integer size'
      )
    }
    const activation = layer.activation ? String(layer.activation) : ''
    return { size, activation }
  })
}

function applyModelMetadata (target, metadata) {
  if (!metadata || typeof metadata !== 'object') {
    return
  }
  const state = STATE.get(target)
  if (!state) {
    return
  }
  if (metadata.id != null) {
    const id = Number(metadata.id)
    state.id = Number.isNaN(id) ? null : id
  }
  if (typeof metadata.name === 'string') {
    state.name = metadata.name
  }
  if (metadata.inputSize != null) {
    state.inputSize = Number(metadata.inputSize)
  }
  if (metadata.outputSize != null) {
    state.outputSize = Number(metadata.outputSize)
  }
  if (typeof metadata.outputActivation === 'string') {
    state.outputActivation = metadata.outputActivation
  }
  if (Array.isArray(metadata.hiddenLayers)) {
    state.hiddenLayers = metadata.hiddenLayers.map((layer) => ({
      size: Number(layer.size),
      activation: layer.activation || ''
    }))
  }
}

/**
 * Represents a managed ANN model within the runtime.
 */
export class Network {
  constructor (metadata = null) {
    STATE.set(this, createDefaultState())
    if (metadata) {
      applyModelMetadata(this, metadata)
    }
  }

  /** @returns {number|null} Unique model identifier assigned by the runtime. */
  get id () {
    return getState(this).id
  }

  /** @returns {string} Model name assigned during creation (optional). */
  get name () {
    return getState(this).name
  }

  /** @returns {number} Number of input features per example. */
  get inputSize () {
    return getState(this).inputSize
  }

  /** @returns {number} Number of output units per example. */
  get outputSize () {
    return getState(this).outputSize
  }

  /** @returns {string} Activation function applied on the output layer. */
  get outputActivation () {
    return getState(this).outputActivation
  }

  /** @returns {Array<{size:number, activation:string}>} Hidden layer definitions. */
  get hiddenLayers () {
    return getState(this).hiddenLayers.slice()
  }

  /**
   * Persist the model state to disk.
   * @param {string} path Absolute path where the model should be stored.
   * @returns {Promise<boolean>}
   */
  async save (path) {
    const state = ensureReady(this)
    if (typeof path !== 'string' || path.length === 0) {
      throw new TypeError('save: path must be a non-empty string')
    }

    const result = await ipc.request(ROUTES.save, {
      id: state.id,
      path
    })

    if (result.err) {
      throw result.err
    }

    return Boolean(unwrapOk(result))
  }

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
  async train (features, labels, options = {}) {
    const state = ensureReady(this)

    const featureColumns =
      options.featureColumns != null
        ? options.featureColumns
        : state.inputSize || undefined
    const labelColumns =
      options.labelColumns != null
        ? options.labelColumns
        : state.outputSize || undefined

    const featureMatrix = normalizeMatrix('features', features, {
      rows: options.featureRows,
      columns: featureColumns
    })

    const labelMatrix = normalizeMatrix('labels', labels, {
      rows: options.labelRows != null ? options.labelRows : featureMatrix.rows,
      columns: labelColumns
    })

    if (featureMatrix.rows !== labelMatrix.rows) {
      throw new Error(
        `train: feature rows (${featureMatrix.rows}) must match label rows (${labelMatrix.rows})`
      )
    }

    const merged = new Float32Array(
      featureMatrix.data.length + labelMatrix.data.length
    )
    merged.set(featureMatrix.data, 0)
    merged.set(labelMatrix.data, featureMatrix.data.length)

    const payload = {
      id: state.id,
      rows: featureMatrix.rows,
      featureCols: featureMatrix.columns,
      labelCols: labelMatrix.columns,
      loss: options.loss || '',
      batchSize: options.batchSize != null ? options.batchSize : '',
      learningRate: options.learningRate != null ? options.learningRate : '',
      searchTime: options.searchTime != null ? options.searchTime : '',
      regularizationStrength:
        options.regularizationStrength != null
          ? options.regularizationStrength
          : '',
      momentumFactor:
        options.momentumFactor != null ? options.momentumFactor : '',
      maxEpochs: options.maxEpochs != null ? options.maxEpochs : '',
      shuffle: options.shuffle ? 'true' : '',
      verbose: options.verbose ? 'true' : ''
    }

    const result = await ipc.send(ROUTES.train, payload, {
      bytes: toArrayBuffer(merged)
    })

    if (result.err) {
      throw result.err
    }

    const data = unwrapData(result) || {}
    if (
      data &&
      typeof data === 'object' &&
      Object.prototype.hasOwnProperty.call(data, 'model')
    ) {
      applyModelMetadata(this, data.model)
    }

    const report =
      data &&
      typeof data === 'object' &&
      data.report &&
      typeof data.report === 'object'
        ? data.report
        : null

    const lossValue =
      report && report.loss != null
        ? report.loss
        : data && data.loss != null
          ? data.loss
          : 0

    const accuracyValue =
      report && report.accuracy != null
        ? report.accuracy
        : data && data.accuracy != null
          ? data.accuracy
          : 0

    const epochsValue =
      report && report.epochs != null
        ? report.epochs
        : data && data.epochs != null
          ? data.epochs
          : 0

    const durationValue =
      report && report.durationMs != null
        ? report.durationMs
        : data && data.durationMs != null
          ? data.durationMs
          : 0

    return {
      loss: Number(lossValue),
      accuracy: Number(accuracyValue),
      epochs: Number(epochsValue),
      durationMs: Number(durationValue)
    }
  }

  /**
   * Run inference on the network.
   * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} input
   * @param {{rows?:number, columns?:number}} [options]
   * @returns {Promise<{rows:number,columns:number,logits:Float32Array,classes:Int32Array}>}
   */
  async predict (input, options = {}) {
    const state = ensureReady(this)

    const predictColumns =
      options.columns != null ? options.columns : state.inputSize || undefined

    const matrix = normalizeMatrix('input', input, {
      rows: options.rows,
      columns: predictColumns
    })

    const result = await ipc.send(
      ROUTES.predict,
      {
        id: state.id,
        rows: matrix.rows,
        featureCols: matrix.columns
      },
      {
        bytes: toArrayBuffer(matrix.data)
      }
    )

    if (result.err) {
      throw result.err
    }

    const payload = unwrapData(result) || {}
    const inference =
      payload &&
      typeof payload === 'object' &&
      Object.prototype.hasOwnProperty.call(payload, 'result')
        ? payload.result
        : payload
    const inferenceObject =
      inference && typeof inference === 'object' ? inference : {}
    const rows = Number(inferenceObject.rows != null ? inferenceObject.rows : 0)
    const colsValue =
      inferenceObject.cols != null
        ? inferenceObject.cols
        : inferenceObject.columns != null
          ? inferenceObject.columns
          : 0
    const cols = Number(colsValue)

    const logits = new Float32Array(rows * cols)
    const source = Array.isArray(inferenceObject.logits)
      ? inferenceObject.logits
      : []
    for (let r = 0; r < rows; r++) {
      const row = Array.isArray(source[r]) ? source[r] : []
      for (let c = 0; c < cols; c++) {
        const value = row[c]
        logits[r * cols + c] = Number(value != null ? value : 0)
      }
    }

    const classesArray = Array.isArray(inferenceObject.classes)
      ? inferenceObject.classes
      : []
    const classes = new Int32Array(rows)
    for (let i = 0; i < rows; i++) {
      const value = classesArray[i]
      classes[i] = Number(value != null ? value : 0)
    }

    return { rows, columns: cols, logits, classes }
  }

  /**
   * Compute classification accuracy for labeled samples.
   * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} features
   * @param {ArrayLike<number>|Array<ArrayLike<number>>|Float32Array|{data:Float32Array,rows:number,columns:number}} labels
   * @param {{featureColumns?:number,featureRows?:number,labelColumns?:number,labelRows?:number}} [options]
   * @returns {Promise<number>}
   */
  async accuracy (features, labels, options = {}) {
    const state = ensureReady(this)

    const accFeatureColumns =
      options.featureColumns != null
        ? options.featureColumns
        : state.inputSize || undefined
    const accLabelColumns =
      options.labelColumns != null
        ? options.labelColumns
        : state.outputSize || undefined

    const featureMatrix = normalizeMatrix('features', features, {
      rows: options.featureRows,
      columns: accFeatureColumns
    })

    const labelMatrix = normalizeMatrix('labels', labels, {
      rows: options.labelRows != null ? options.labelRows : featureMatrix.rows,
      columns: accLabelColumns
    })

    if (featureMatrix.rows !== labelMatrix.rows) {
      throw new Error(
        `accuracy: feature rows (${featureMatrix.rows}) must match label rows (${labelMatrix.rows})`
      )
    }

    const merged = new Float32Array(
      featureMatrix.data.length + labelMatrix.data.length
    )
    merged.set(featureMatrix.data, 0)
    merged.set(labelMatrix.data, featureMatrix.data.length)

    const result = await ipc.send(
      ROUTES.accuracy,
      {
        id: state.id,
        rows: featureMatrix.rows,
        featureCols: featureMatrix.columns,
        labelCols: labelMatrix.columns
      },
      {
        bytes: toArrayBuffer(merged)
      }
    )

    if (result.err) {
      throw result.err
    }

    const data = unwrapData(result) || {}
    const accuracyValue = data && data.accuracy != null ? data.accuracy : 0
    return Number(accuracyValue)
  }

  /**
   * Destroy the network within the runtime.
   * @returns {Promise<boolean>}
   */
  async remove () {
    const state = ensureReady(this)
    const ok = await remove(state.id)
    if (ok) {
      state.id = null
    }
    return ok
  }

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
  static async create (options) {
    if (!options || typeof options !== 'object') {
      throw new TypeError('create: options object is required')
    }
    if (!Number.isFinite(options.inputSize) || options.inputSize <= 0) {
      throw new TypeError('create: inputSize must be a positive number')
    }
    if (!Number.isFinite(options.outputSize) || options.outputSize <= 0) {
      throw new TypeError('create: outputSize must be a positive number')
    }

    const payload = {
      name: options.name != null ? options.name : '',
      inputSize: options.inputSize,
      outputSize: options.outputSize
    }

    if (options.outputActivation) {
      payload.outputActivation = options.outputActivation
    }

    const layers = encodeHiddenLayers(options.hiddenLayers)
    if (layers) {
      payload.hiddenLayers = layers
    }

    const result = await ipc.request(ROUTES.create, payload)
    if (result.err) {
      throw result.err
    }
    return networkFromResult(result)
  }

  /**
   * Load an ANN model from disk and register it with the runtime.
   * @param {string} path Absolute path to a serialized model file.
   * @param {{name?:string}} [options]
   * @returns {Promise<Network>}
   */
  static async load (path, options = {}) {
    if (typeof path !== 'string' || path.length === 0) {
      throw new TypeError('load: path must be a non-empty string')
    }
    const result = await ipc.request(ROUTES.load, {
      path,
      name: options.name != null ? options.name : ''
    })
    if (result.err) {
      throw result.err
    }
    return networkFromResult(result)
  }

  /**
   * List registered ANN models.
   * @returns {Promise<Array<object>>}
   */
  static async list () {
    const result = await ipc.request(ROUTES.list)
    if (result.err) {
      throw result.err
    }
    const payload = unwrapData(result)
    return Array.isArray(payload) ? payload : []
  }
}

/**
 * Create a new ANN model.
 * @param {ConstructorParameters<typeof Network>[0]} options
 * @returns {Promise<Network>}
 */
export async function create (options) {
  return Network.create(options)
}

/**
 * Load a model from disk.
 * @param {string} path
 * @param {{name?:string}} [options]
 * @returns {Promise<Network>}
 */
export async function load (path, options = {}) {
  return Network.load(path, options)
}

/**
 * Retrieve metadata for registered ANN models.
 * @returns {Promise<Array<object>>}
 */
export async function list () {
  return Network.list()
}

/**
 * Remove a model by instance, id, or name.
 * @param {Network|number|string} target
 * @returns {Promise<boolean>}
 */
export async function remove (target) {
  let id = 0
  let name = ''

  if (target instanceof Network) {
    id = target.id != null ? target.id : 0
    name = target.name || ''
  } else if (typeof target === 'number') {
    id = Number(target)
  } else if (typeof target === 'string') {
    name = target
  } else {
    throw new TypeError('remove: expected a Network instance, id, or name')
  }

  if (!Number.isInteger(id) && !name) {
    throw new TypeError('remove: expected a valid id or name')
  }

  const result = await ipc.request(ROUTES.remove, {
    id: id || '',
    name
  })

  if (result.err) {
    throw result.err
  }

  return Boolean(unwrapOk(result))
}

function networkFromResult (result) {
  const metadata = unwrapData(result)
  return new Network(metadata)
}

export default {
  Network,
  LossFunction,
  create,
  load,
  list,
  remove
}
