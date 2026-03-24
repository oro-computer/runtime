import { Network, LossFunction } from 'oro:ai/ann'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleGrid,
  ExamplePanel,
  ExampleSection,
  ExampleStack,
  FormField,
  Input,
  Select,
  Option,
  Switch,
  Button,
  StatusBanner,
  Badge,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  LogViewer,
  EmptyState,
  Code
} from '../ui/index.js'

const {
  createElement: h,
  useState,
  useMemo,
  useCallback,
  useEffect,
  useRef
} = React

const CLASS_NAMES = ['Spiral A', 'Spiral B', 'Spiral C']
const FEATURE_COUNT = 2

const DEFAULT_CONFIG = Object.freeze({
  name: 'spiral-classifier',
  hiddenLayers: '64:relu,32:relu',
  learningRate: '0.05',
  batchSize: '32',
  maxEpochs: '160',
  momentumFactor: '0.8',
  regularizationStrength: '0.0001',
  loss: LossFunction.CrossEntropy,
  shuffle: true,
  verbose: false
})

function createSpiralDataset ({
  classes = CLASS_NAMES.length,
  featureCount = FEATURE_COUNT,
  trainPerClass = 90,
  testPerClass = 30,
  turns = 3.6,
  jitterScale = 0.05
} = {}) {
  const labelCount = classes
  const trainRows = trainPerClass * classes
  const testRows = testPerClass * classes

  const trainFeatures = new Float32Array(trainRows * featureCount)
  const trainLabels = new Float32Array(trainRows * labelCount)
  const trainTargets = new Int32Array(trainRows)
  const testFeatures = new Float32Array(testRows * featureCount)
  const testLabels = new Float32Array(testRows * labelCount)
  const testTargets = new Int32Array(testRows)

  for (let classIndex = 0; classIndex < classes; classIndex++) {
    const angleOffset = classIndex * ((Math.PI * 2) / classes)

    for (let i = 0; i < trainPerClass; i++) {
      const row = classIndex * trainPerClass + i
      const radius = (i + 1) / (trainPerClass + 1)
      const twist = radius * turns * Math.PI
      const wobble = Math.sin((i + 1) * 0.85 + classIndex) * jitterScale
      const angle = angleOffset + twist + wobble
      const x = Math.cos(angle) * radius
      const y = Math.sin(angle) * radius
      writeSample(
        trainFeatures,
        trainLabels,
        trainTargets,
        row,
        [x, y],
        classIndex,
        featureCount,
        labelCount
      )
    }

    for (let i = 0; i < testPerClass; i++) {
      const row = classIndex * testPerClass + i
      const radius = (i + 0.5) / (testPerClass + 1)
      const twist = radius * turns * Math.PI
      const wobble = Math.cos((i + 0.5) * 1.05 + classIndex) * jitterScale
      const angle = angleOffset + twist + wobble
      const x = Math.cos(angle) * radius
      const y = Math.sin(angle) * radius
      writeSample(
        testFeatures,
        testLabels,
        testTargets,
        row,
        [x, y],
        classIndex,
        featureCount,
        labelCount
      )
    }
  }

  const normalization = computeNormalization(trainFeatures, featureCount)
  applyNormalization(trainFeatures, featureCount, normalization)
  applyNormalization(testFeatures, featureCount, normalization)

  const bounds = computeBounds(featureCount, trainFeatures, testFeatures)

  return {
    featureCount,
    labelCount,
    classNames: CLASS_NAMES.slice(0, classes),
    normalization,
    bounds,
    train: {
      rows: trainRows,
      features: trainFeatures,
      labels: trainLabels,
      targets: trainTargets
    },
    test: {
      rows: testRows,
      features: testFeatures,
      labels: testLabels,
      targets: testTargets
    }
  }
}

function writeSample (
  features,
  labels,
  targets,
  row,
  coords,
  classIndex,
  featureCount,
  labelCount
) {
  const featureBase = row * featureCount
  for (let col = 0; col < featureCount; col++) {
    const value = coords[col]
    features[featureBase + col] = value != null ? value : 0
  }

  const labelBase = row * labelCount
  for (let col = 0; col < labelCount; col++) {
    labels[labelBase + col] = col === classIndex ? 1 : 0
  }

  targets[row] = classIndex
}

function computeNormalization (features, featureCount) {
  const rows = features.length / featureCount
  const mean = new Float32Array(featureCount)
  const variance = new Float32Array(featureCount)

  for (let row = 0; row < rows; row++) {
    for (let col = 0; col < featureCount; col++) {
      mean[col] += features[row * featureCount + col]
    }
  }

  for (let col = 0; col < featureCount; col++) {
    mean[col] /= rows
  }

  for (let row = 0; row < rows; row++) {
    for (let col = 0; col < featureCount; col++) {
      const value = features[row * featureCount + col]
      const diff = value - mean[col]
      variance[col] += diff * diff
    }
  }

  const std = new Float32Array(featureCount)
  for (let col = 0; col < featureCount; col++) {
    const scale = variance[col] / rows
    std[col] = scale > 1e-9 ? Math.sqrt(scale) : 1
  }

  return { mean, std }
}

function applyNormalization (features, featureCount, stats) {
  const rows = features.length / featureCount
  for (let row = 0; row < rows; row++) {
    for (let col = 0; col < featureCount; col++) {
      const index = row * featureCount + col
      const value = features[index]
      features[index] = (value - stats.mean[col]) / stats.std[col]
    }
  }
}

function computeBounds (featureCount, ...arrays) {
  const bounds = Array.from({ length: featureCount }, () => ({
    min: Infinity,
    max: -Infinity
  }))

  for (const array of arrays) {
    const rows = array.length / featureCount
    for (let row = 0; row < rows; row++) {
      for (let col = 0; col < featureCount; col++) {
        const index = row * featureCount + col
        const value = array[index]
        const target = bounds[col]
        if (value < target.min) target.min = value
        if (value > target.max) target.max = value
      }
    }
  }

  return bounds.map((bound) => ({
    min: Number.isFinite(bound.min) ? bound.min : 0,
    max: Number.isFinite(bound.max) ? bound.max : 0
  }))
}

function parseHiddenLayers (raw) {
  if (!raw || typeof raw !== 'string') return []
  const layers = []
  const entries = raw
    .split(',')
    .map((part) => part.trim())
    .filter(Boolean)
  for (const entry of entries) {
    const [sizePart, activationPart] = entry
      .split(':')
      .map((part) => part.trim())
    const size = Number.parseInt(sizePart, 10)
    if (!Number.isInteger(size) || size <= 0) {
      throw new TypeError(
        `Hidden layer size must be a positive integer (received "${entry}")`
      )
    }
    const activation = activationPart ? activationPart.toLowerCase() : 'relu'
    layers.push({ size, activation })
  }
  return layers
}

function formatNumber (value, digits = 4) {
  if (!Number.isFinite(value)) return '—'
  return Number(value).toFixed(digits)
}

function formatPercent (value, digits = 2) {
  if (!Number.isFinite(value)) return '—'
  return `${(value * 100).toFixed(digits)}%`
}

function formatDuration (ms) {
  if (!Number.isFinite(ms)) return '—'
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`
  }
  return `${ms.toFixed(2)} ms`
}

function now () {
  if (
    typeof performance !== 'undefined' &&
    typeof performance.now === 'function'
  ) {
    return performance.now()
  }
  return Date.now()
}

function formatHiddenLayers (layers) {
  if (!Array.isArray(layers) || layers.length === 0) return 'None'
  return layers
    .map((layer) => {
      const activation = layer.activation ? ` · ${layer.activation}` : ''
      return `${layer.size}${activation}`
    })
    .join(' → ')
}

function formatError (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error && typeof error === 'object' && typeof error.message === 'string') {
    return error.message
  }
  return String(error)
}

function asPositiveInt (value) {
  const parsed = Number.parseInt(value, 10)
  return Number.isInteger(parsed) && parsed > 0 ? parsed : undefined
}

function asFiniteNumber (value) {
  const parsed = Number.parseFloat(value)
  return Number.isFinite(parsed) ? parsed : undefined
}

function getFeatureRow (features, featureCount, row) {
  const values = []
  for (let col = 0; col < featureCount; col++) {
    values.push(features[row * featureCount + col])
  }
  return values
}

function softmax (logits, start, count) {
  let max = -Infinity
  for (let i = 0; i < count; i++) {
    const value = logits[start + i]
    if (value > max) max = value
  }
  let sum = 0
  const exps = Array.from({ length: count })
  for (let i = 0; i < count; i++) {
    const value = Math.exp(logits[start + i] - max)
    exps[i] = value
    sum += value
  }
  if (sum === 0) return exps.fill(1 / count)
  return exps.map((value) => value / sum)
}

function buildPredictionPreview (inference, dataset, limit = 12) {
  if (!inference) return []
  const { rows, columns, logits, classes } = inference
  const count = Math.min(rows, limit)
  const items = []
  for (let row = 0; row < count; row++) {
    const predictedIndex = classes && classes[row] != null ? classes[row] : 0
    const probabilities = softmax(logits, row * columns, columns)
    items.push({
      id: row,
      sample: row + 1,
      predictedIndex,
      actualIndex: dataset.test.targets[row],
      probabilities,
      confidence:
        probabilities[predictedIndex] != null
          ? probabilities[predictedIndex]
          : 0,
      features: getFeatureRow(dataset.test.features, dataset.featureCount, row)
    })
  }
  return items
}

function AnnPlayground () {
  const dataset = useMemo(() => createSpiralDataset(), [])
  const [config, setConfig] = useState(() => ({ ...DEFAULT_CONFIG }))
  const [model, setModel] = useState(null)
  const [banner, setBanner] = useState(null)
  const [trainReport, setTrainReport] = useState(null)
  const [testAccuracy, setTestAccuracy] = useState(null)
  const [predictions, setPredictions] = useState([])
  const [registry, setRegistry] = useState([])
  const [busyPhase, setBusyPhase] = useState(null)
  const [logs, setLogs] = useState([])
  const logPointer = useRef(0)

  const appendLog = useCallback((kind, message) => {
    const timestamp = new Date().toLocaleTimeString()
    logPointer.current += 1
    setLogs((previous) => {
      const next = previous.concat({
        id: logPointer.current,
        kind,
        message,
        timestamp
      })
      return next.length > 160 ? next.slice(next.length - 160) : next
    })
  }, [])

  const refreshRegistry = useCallback(async () => {
    try {
      const models = await Network.list()
      if (Array.isArray(models)) {
        setRegistry(models)
      } else {
        setRegistry([])
      }
    } catch (error) {
      appendLog(
        'error',
        `Unable to list registered models: ${formatError(error)}`
      )
    }
  }, [appendLog])

  useEffect(() => {
    refreshRegistry()
  }, [refreshRegistry])

  const handleInput = useCallback(
    (key) => (event) => {
      const value = event && event.target ? event.target.value : ''
      setConfig((previous) => ({ ...previous, [key]: value }))
    },
    []
  )

  const handleToggle = useCallback(
    (key) => (checked) => {
      setConfig((previous) => ({ ...previous, [key]: Boolean(checked) }))
    },
    []
  )

  const createModel = useCallback(async () => {
    if (busyPhase) return
    setBusyPhase('create')
    setBanner(null)
    setTrainReport(null)
    setTestAccuracy(null)
    setPredictions([])

    try {
      const hiddenLayers = parseHiddenLayers(config.hiddenLayers)
      if (model) {
        appendLog(
          'info',
          'Detaching previous model instance before creating a new one.'
        )
        try {
          await model.remove()
        } catch (error) {
          appendLog(
            'warning',
            `Failed to remove previous model: ${formatError(error)}`
          )
        }
        setModel(null)
      }

      appendLog('info', 'Creating ANN model…')
      const instance = await Network.create({
        name: config.name || '',
        inputSize: dataset.featureCount,
        outputSize: dataset.labelCount,
        outputActivation: 'softmax',
        hiddenLayers
      })

      setModel(instance)
      appendLog(
        'success',
        `Model #${instance.id != null ? instance.id : '—'} ready (${instance.inputSize} → ${instance.outputSize})`
      )
      setBanner({
        variant: 'success',
        title: 'Model initialised',
        message: `Network configured with ${formatHiddenLayers(instance.hiddenLayers)}`
      })
      await refreshRegistry()
    } catch (error) {
      appendLog('error', formatError(error))
      setBanner({
        variant: 'error',
        title: 'Model creation failed',
        message: formatError(error)
      })
    } finally {
      setBusyPhase(null)
    }
  }, [busyPhase, config, dataset, model, appendLog, refreshRegistry])

  const trainModel = useCallback(async () => {
    if (!model || busyPhase) {
      if (!model) {
        setBanner({
          variant: 'warning',
          title: 'Model not available',
          message: 'Create a model before triggering training.'
        })
      }
      return
    }

    setBusyPhase('train')
    setBanner(null)
    setTestAccuracy(null)

    try {
      appendLog(
        'info',
        `Training on ${dataset.train.rows} samples (${dataset.train.rows * dataset.featureCount} feature values).`
      )

      const options = {
        loss: config.loss,
        batchSize: asPositiveInt(config.batchSize),
        learningRate: asFiniteNumber(config.learningRate),
        maxEpochs: asPositiveInt(config.maxEpochs),
        momentumFactor: asFiniteNumber(config.momentumFactor),
        regularizationStrength: asFiniteNumber(config.regularizationStrength),
        shuffle: config.shuffle,
        verbose: config.verbose,
        featureRows: dataset.train.rows,
        featureColumns: dataset.featureCount,
        labelRows: dataset.train.rows,
        labelColumns: dataset.labelCount
      }

      const startedAt = now()
      const report = await model.train(
        dataset.train.features,
        dataset.train.labels,
        options
      )
      const elapsed = now() - startedAt

      setTrainReport({ ...report, wallTimeMs: elapsed })
      appendLog(
        'success',
        `Training complete. Loss ${formatNumber(report.loss, 4)}, accuracy ${formatPercent(report.accuracy, 2)}.`
      )
      setBanner({
        variant: 'success',
        title: 'Training finished',
        message: `Loss ${formatNumber(report.loss, 4)} · Accuracy ${formatPercent(report.accuracy, 2)}`
      })
      await refreshRegistry()
    } catch (error) {
      appendLog('error', formatError(error))
      setBanner({
        variant: 'error',
        title: 'Training failed',
        message: formatError(error)
      })
    } finally {
      setBusyPhase(null)
    }
  }, [model, busyPhase, dataset, config, appendLog, refreshRegistry])

  const evaluateModel = useCallback(async () => {
    if (!model || busyPhase) {
      if (!model) {
        setBanner({
          variant: 'warning',
          title: 'Model not available',
          message: 'Create and train a model before running evaluation.'
        })
      }
      return
    }

    setBusyPhase('evaluate')
    setBanner(null)

    try {
      appendLog(
        'info',
        `Computing accuracy over ${dataset.test.rows} held-out samples.`
      )
      const accuracy = await model.accuracy(
        dataset.test.features,
        dataset.test.labels,
        {
          featureRows: dataset.test.rows,
          featureColumns: dataset.featureCount,
          labelRows: dataset.test.rows,
          labelColumns: dataset.labelCount
        }
      )

      setTestAccuracy(accuracy)
      appendLog('success', `Test accuracy ${formatPercent(accuracy, 2)}.`)

      const inference = await model.predict(dataset.test.features, {
        rows: dataset.test.rows,
        columns: dataset.featureCount
      })

      setPredictions(buildPredictionPreview(inference, dataset, 12))
      setBanner({
        variant: 'info',
        title: 'Evaluation ready',
        message: `Held-out accuracy ${formatPercent(accuracy, 2)}`
      })
    } catch (error) {
      appendLog('error', formatError(error))
      setBanner({
        variant: 'error',
        title: 'Evaluation failed',
        message: formatError(error)
      })
    } finally {
      setBusyPhase(null)
    }
  }, [model, busyPhase, dataset, appendLog])

  const removeModel = useCallback(async () => {
    if (!model || busyPhase) return
    setBusyPhase('remove')

    try {
      appendLog(
        'info',
        `Removing model ${model.name || model.id || ''} from runtime.`
      )
      await model.remove()
      appendLog('success', 'Model removed from runtime registry.')
      setModel(null)
      setTrainReport(null)
      setTestAccuracy(null)
      setPredictions([])
      setBanner({
        variant: 'info',
        title: 'Model detached',
        message:
          'Create a model to continue experimenting with new hyperparameters.'
      })
      await refreshRegistry()
    } catch (error) {
      appendLog('error', formatError(error))
      setBanner({
        variant: 'error',
        title: 'Removal failed',
        message: formatError(error)
      })
    } finally {
      setBusyPhase(null)
    }
  }, [model, busyPhase, appendLog, refreshRegistry])

  const resetView = useCallback(() => {
    if (busyPhase) return
    setConfig(() => ({ ...DEFAULT_CONFIG }))
    setBanner(null)
    setTrainReport(null)
    setTestAccuracy(null)
    setPredictions([])
    setLogs([])
  }, [busyPhase])

  const isBusy = Boolean(busyPhase)
  const hiddenLayerPreview = useMemo(() => {
    try {
      return formatHiddenLayers(parseHiddenLayers(config.hiddenLayers))
    } catch {
      return 'Invalid definition'
    }
  }, [config.hiddenLayers])

  const metrics = useMemo(() => {
    let durationSource = null
    if (trainReport && typeof trainReport === 'object') {
      if (
        typeof trainReport.durationMs === 'number' &&
        trainReport.durationMs > 0
      ) {
        durationSource = trainReport.durationMs
      } else if (typeof trainReport.wallTimeMs === 'number') {
        durationSource = trainReport.wallTimeMs
      }
    }
    const durationValue = durationSource != null ? durationSource : NaN
    return [
      {
        label: 'Model status',
        value: model
          ? `#${model.id != null ? model.id : '—'} · ${model.inputSize} → ${model.outputSize}`
          : 'No model'
      },
      {
        label: 'Hidden layers',
        value: model
          ? formatHiddenLayers(model.hiddenLayers)
          : hiddenLayerPreview
      },
      {
        label: 'Train loss',
        value: trainReport ? formatNumber(trainReport.loss, 4) : '—'
      },
      {
        label: 'Train accuracy',
        value: trainReport ? formatPercent(trainReport.accuracy, 2) : '—'
      },
      {
        label: 'Epochs',
        value: trainReport ? trainReport.epochs : '—'
      },
      {
        label: 'Train duration',
        value: trainReport ? formatDuration(durationValue) : '—'
      },
      {
        label: 'Test accuracy',
        value: Number.isFinite(testAccuracy)
          ? formatPercent(testAccuracy, 2)
          : '—'
      }
    ]
  }, [model, trainReport, testAccuracy, hiddenLayerPreview])

  const registryTable = useMemo(() => {
    if (!registry || registry.length === 0) {
      return h(EmptyState, {
        title: 'No registered models',
        description: 'Create one from this playground and it will appear here.'
      })
    }

    return h(
      Table,
      { className: 'ann-app__registry-table' },
      h(
        TableHead,
        null,
        h(
          TableRow,
          null,
          h(TableHeader, null, 'ID'),
          h(TableHeader, null, 'Name'),
          h(TableHeader, null, 'Input → Output'),
          h(TableHeader, null, 'Layers')
        )
      ),
      h(
        TableBody,
        null,
        registry.map((entry, index) => {
          const hidden = Array.isArray(entry.hiddenLayers)
            ? entry.hiddenLayers
              .map(
                (layer) =>
                    `${layer.size}${layer.activation ? ` ${layer.activation}` : ''}`
              )
              .join(', ')
            : '—'
          const key = entry.id != null ? entry.id : entry.name || `row-${index}`
          const entryId = entry.id != null ? entry.id : '—'
          const inputLabel = entry.inputSize != null ? entry.inputSize : '—'
          const outputLabel = entry.outputSize != null ? entry.outputSize : '—'
          return h(
            TableRow,
            { key },
            h(TableCell, null, entryId),
            h(TableCell, null, entry.name || '—'),
            h(TableCell, null, `${inputLabel} → ${outputLabel}`),
            h(TableCell, null, hidden || '—')
          )
        })
      )
    )
  }, [registry])

  const predictionTable = useMemo(() => {
    if (!predictions || predictions.length === 0) {
      return h(EmptyState, {
        title: 'No predictions yet',
        description:
          'Evaluate the model to generate predictions over the test split.'
      })
    }

    return h(
      Table,
      { className: 'ann-app__prediction-table' },
      h(
        TableHead,
        null,
        h(
          TableRow,
          null,
          h(TableHeader, null, 'Sample'),
          h(TableHeader, null, 'Feature 1'),
          h(TableHeader, null, 'Feature 2'),
          h(TableHeader, null, 'Ground truth'),
          h(TableHeader, null, 'Predicted'),
          h(TableHeader, null, 'Confidence')
        )
      ),
      h(
        TableBody,
        null,
        predictions.map((item) => {
          const actual =
            dataset.classNames[item.actualIndex] ||
            `Class ${item.actualIndex + 1}`
          const predicted =
            dataset.classNames[item.predictedIndex] ||
            `Class ${item.predictedIndex + 1}`
          return h(
            TableRow,
            { key: item.id },
            h(TableCell, null, `#${item.sample}`),
            h(TableCell, null, formatNumber(item.features[0], 3)),
            h(TableCell, null, formatNumber(item.features[1], 3)),
            h(
              TableCell,
              null,
              actual,
              item.actualIndex === item.predictedIndex
                ? h(
                  Badge,
                  { className: 'ann-app__label-badge', variant: 'success' },
                  'Match'
                )
                : null
            ),
            h(TableCell, null, predicted),
            h(TableCell, null, formatPercent(item.confidence, 1))
          )
        })
      )
    )
  }, [predictions, dataset.classNames])

  const datasetSummary = useMemo(
    () => [
      { label: 'Training samples', value: dataset.train.rows },
      { label: 'Test samples', value: dataset.test.rows },
      { label: 'Features', value: dataset.featureCount },
      { label: 'Classes', value: dataset.labelCount }
    ],
    [dataset]
  )

  const normalizationSummary = useMemo(
    () =>
      dataset.bounds.map((bound, index) => {
        return {
          label: `Feature ${index + 1}`,
          value: `${formatNumber(bound.min, 3)} → ${formatNumber(bound.max, 3)}`
        }
      }),
    [dataset.bounds]
  )

  return h(
    ExampleLayout,
    {
      title: 'ANN Playground',
      description:
        'Train and evaluate a compact feed-forward network using oro:ai/ann.',
      badge: h(Badge, { variant: 'neutral' }, 'AI runtime'),
      aside: h(
        ExampleStack,
        { gap: 'lg' },
        h(
          ExamplePanel,
          {
            title: 'Dataset overview',
            description:
              'Deterministic three-arm spiral with standardised features.'
          },
          h(
            'div',
            { className: 'ann-app__summary' },
            datasetSummary.map(({ label, value }) =>
              h(
                'div',
                { key: label, className: 'ann-app__summary-item' },
                h('span', { className: 'ann-app__summary-label' }, label),
                h('span', { className: 'ann-app__summary-value' }, value)
              )
            )
          ),
          h(
            ExampleSection,
            {
              title: 'Normalised feature range',
              description:
                'Ranges after applying per-feature mean/variance scaling.'
            },
            h(
              'div',
              { className: 'ann-app__summary' },
              normalizationSummary.map(({ label, value }) =>
                h(
                  'div',
                  { key: label, className: 'ann-app__summary-item' },
                  h('span', { className: 'ann-app__summary-label' }, label),
                  h('span', { className: 'ann-app__summary-value' }, value)
                )
              )
            )
          ),
          h(
            ExampleSection,
            {
              title: 'Class labels',
              description:
                'Model predicts the dominant spiral arm for each point.'
            },
            h(
              'ul',
              { className: 'ann-app__class-list' },
              dataset.classNames.map((name, index) =>
                h(
                  'li',
                  { key: name },
                  h(Badge, { variant: 'info' }, `Class ${index + 1}`),
                  h('span', null, name)
                )
              )
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Runtime registry',
            description: 'Models currently registered with the ANN service.'
          },
          h(
            'div',
            { className: 'ann-app__registry-actions' },
            h(
              Button,
              {
                variant: 'ghost',
                size: 'sm',
                onClick: () => refreshRegistry(),
                disabled: isBusy
              },
              'Refresh'
            )
          ),
          registryTable
        )
      )
    },
    h(
      ExampleStack,
      { gap: 'lg' },
      banner &&
        h(StatusBanner, {
          variant: banner.variant,
          title: banner.title,
          message: banner.message
        }),
      h(
        ExampleGrid,
        { columns: 2, className: 'ann-app__grid' },
        h(
          ExamplePanel,
          {
            title: 'Model configuration',
            description:
              'Define the network structure and optimisation parameters.'
          },
          h(
            ExampleStack,
            { gap: 'lg' },
            h(
              'div',
              { className: 'ann-app__form-grid' },
              h(
                FormField,
                {
                  label: 'Model name',
                  htmlFor: 'ann-name',
                  hint: 'Stored alongside the runtime model entry.'
                },
                h(Input, {
                  id: 'ann-name',
                  placeholder: 'spiral-classifier',
                  value: config.name,
                  onInput: handleInput('name')
                })
              ),
              h(
                FormField,
                {
                  label: 'Hidden layers',
                  htmlFor: 'ann-hidden',
                  hint: `Comma separated sizes (optionally size:activation). Preview: ${hiddenLayerPreview}`
                },
                h(Input, {
                  id: 'ann-hidden',
                  placeholder: '64:relu,32:relu',
                  value: config.hiddenLayers,
                  onInput: handleInput('hiddenLayers')
                })
              ),
              h(
                FormField,
                {
                  label: 'Learning rate',
                  htmlFor: 'ann-learning-rate'
                },
                h(Input, {
                  id: 'ann-learning-rate',
                  type: 'number',
                  step: '0.01',
                  value: config.learningRate,
                  onInput: handleInput('learningRate')
                })
              ),
              h(
                FormField,
                {
                  label: 'Batch size',
                  htmlFor: 'ann-batch-size'
                },
                h(Input, {
                  id: 'ann-batch-size',
                  type: 'number',
                  min: '1',
                  value: config.batchSize,
                  onInput: handleInput('batchSize')
                })
              ),
              h(
                FormField,
                {
                  label: 'Max epochs',
                  htmlFor: 'ann-epochs'
                },
                h(Input, {
                  id: 'ann-epochs',
                  type: 'number',
                  min: '1',
                  value: config.maxEpochs,
                  onInput: handleInput('maxEpochs')
                })
              ),
              h(
                FormField,
                {
                  label: 'Momentum factor',
                  htmlFor: 'ann-momentum',
                  hint: 'Set to 0 to disable momentum.'
                },
                h(Input, {
                  id: 'ann-momentum',
                  type: 'number',
                  step: '0.05',
                  value: config.momentumFactor,
                  onInput: handleInput('momentumFactor')
                })
              ),
              h(
                FormField,
                {
                  label: 'Regularisation strength',
                  htmlFor: 'ann-regularisation'
                },
                h(Input, {
                  id: 'ann-regularisation',
                  type: 'number',
                  step: '0.0001',
                  value: config.regularizationStrength,
                  onInput: handleInput('regularizationStrength')
                })
              ),
              h(
                FormField,
                {
                  label: 'Loss function',
                  htmlFor: 'ann-loss'
                },
                h(
                  Select,
                  {
                    id: 'ann-loss',
                    value: config.loss,
                    onChange: handleInput('loss')
                  },
                  h(
                    Option,
                    { value: LossFunction.CrossEntropy },
                    'Cross entropy'
                  ),
                  h(
                    Option,
                    { value: LossFunction.MeanSquaredError },
                    'Mean squared error'
                  )
                )
              )
            ),
            h(
              'div',
              { className: 'ann-app__toggles' },
              h(
                FormField,
                {
                  label: 'Shuffle batches',
                  hint: 'Randomise training rows between epochs.',
                  className: 'ann-app__toggle-field'
                },
                h(Switch, {
                  checked: config.shuffle,
                  onCheckedChange: handleToggle('shuffle')
                })
              ),
              h(
                FormField,
                {
                  label: 'Verbose training',
                  hint: 'Requests detailed runtime logs (if supported).',
                  className: 'ann-app__toggle-field'
                },
                h(Switch, {
                  checked: config.verbose,
                  onCheckedChange: handleToggle('verbose')
                })
              )
            ),
            h(
              'div',
              { className: 'ann-app__actions' },
              h(
                Button,
                {
                  variant: 'primary',
                  onClick: createModel,
                  disabled: isBusy
                },
                model ? 'Recreate model' : 'Create model'
              ),
              h(
                Button,
                {
                  onClick: trainModel,
                  disabled: isBusy || !model
                },
                'Train on dataset'
              ),
              h(
                Button,
                {
                  variant: 'outline',
                  onClick: evaluateModel,
                  disabled: isBusy || !model
                },
                'Evaluate test split'
              ),
              h(
                Button,
                {
                  variant: 'ghost',
                  onClick: removeModel,
                  disabled: isBusy || !model
                },
                'Remove model'
              ),
              h(
                Button,
                {
                  variant: 'ghost',
                  onClick: resetView,
                  disabled: isBusy
                },
                'Reset view'
              )
            ),
            h(
              Code,
              null,
              `await Network.create({
  name: '${config.name || 'model'}',
  inputSize: ${dataset.featureCount},
  outputSize: ${dataset.labelCount},
  hiddenLayers: [${hiddenLayerPreview}],
  outputActivation: 'softmax'
})`
            )
          )
        ),
        h(
          ExamplePanel,
          {
            title: 'Training & evaluation',
            description:
              'Monitor training reports and inspect held-out predictions.'
          },
          h(
            ExampleStack,
            { gap: 'lg' },
            h(
              'div',
              { className: 'ann-app__metrics' },
              metrics.map((metric) =>
                h(
                  'div',
                  { key: metric.label, className: 'ann-app__metric' },
                  h(
                    'span',
                    { className: 'ann-app__metric-label' },
                    metric.label
                  ),
                  h(
                    'span',
                    { className: 'ann-app__metric-value' },
                    metric.value
                  )
                )
              )
            ),
            h(
              ExampleSection,
              {
                title: 'Test predictions',
                description:
                  'Preview of the first 12 samples from the held-out split.'
              },
              predictionTable
            )
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Runtime activity',
          description:
            'High-level log of operations dispatched to the ANN service.'
        },
        h(LogViewer, {
          entries: logs,
          renderEntry: (entry) =>
            h(
              'div',
              { className: 'ann-app__log-entry' },
              h(
                'span',
                {
                  className: `ann-app__log-kind ann-app__log-kind--${entry.kind}`
                },
                entry.kind
              ),
              h(
                'span',
                { className: 'ann-app__log-timestamp' },
                entry.timestamp
              ),
              h('span', { className: 'ann-app__log-message' }, entry.message)
            ),
          emptyState: () => 'No activity yet — create a model to begin.'
        })
      )
    )
  )
}

mountExample(AnnPlayground)
