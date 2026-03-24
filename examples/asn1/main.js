import { parse } from 'oro:asn1'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleGrid,
  ExamplePanel,
  ExampleSection,
  ExampleStack,
  ExampleProse,
  StatusBanner,
  Button,
  Input,
  Textarea,
  Select,
  Option,
  Checkbox,
  FormField,
  Badge,
  Code,
  InlineCode,
  Tabs,
  TabsList,
  TabsTrigger,
  TabsContent,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  ScrollArea,
  EmptyState
} from '../ui/index.js'

import { ASN1_SAMPLES } from './samples.js'

const { createElement: h, useState, useMemo, useCallback, useRef } = React

const DEFAULT_STATUS = {
  variant: 'info',
  title: 'Ready to parse',
  message: 'Choose a sample or paste ASN.1 text, then run the parser.'
}

const DEFAULT_MAX_DEPTH = '64'

function formatError (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error instanceof Error) {
    return error.message || error.name || 'Unexpected error'
  }
  if (typeof error === 'object') {
    const parts = []
    if (typeof error.type === 'string') parts.push(error.type)
    if (typeof error.name === 'string' && parts.length === 0) {
      parts.push(error.name)
    }
    if (typeof error.code !== 'undefined') parts.push(`[${error.code}]`)
    if (typeof error.message === 'string') parts.push(error.message)
    if (parts.length) return parts.join(' ')
    try {
      return JSON.stringify(error)
    } catch {
      return String(error)
    }
  }
  return String(error)
}

function formatDuration (ms) {
  if (!Number.isFinite(ms) || ms < 0) return '0 ms'
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`
  }
  return `${ms.toFixed(2)} ms`
}

function toPrettyJson (value) {
  try {
    return JSON.stringify(value, null, 2)
  } catch {
    return String(value)
  }
}

function normalizeConstraints (value) {
  if (value == null) return []
  if (Array.isArray(value)) {
    return value.filter(Boolean)
  }
  return [value].filter(Boolean)
}

function formatConstraintRange (range) {
  if (!range) return ''
  const start = range.start ? formatValue(range.start) : 'min'
  const stop = range.stop ? formatValue(range.stop) : 'max'
  return `${start}..${stop}`
}

function formatConstraintSummary (constraint) {
  if (!constraint) return ''
  const parts = []
  if (constraint.type) parts.push(constraint.type)
  if (constraint.presence && constraint.presence !== 'default') {
    parts.push(`presence:${constraint.presence}`)
  }
  if (constraint.range) {
    const range = formatConstraintRange(constraint.range)
    if (range) parts.push(`range:${range}`)
  }
  if (constraint.value) {
    parts.push(`value:${formatValue(constraint.value)}`)
  }
  if (constraint.elements && constraint.elements.length) {
    parts.push(`elements:${constraint.elements.length}`)
  }
  if (constraint.truncated) parts.push('truncated')
  return parts.join(' · ')
}

function formatValue (value) {
  if (!value) return ''

  const parts = []
  if (value.type && value.type !== 'none') {
    parts.push(value.type)
  }

  if (value.reference) {
    parts.push(`→ ${value.reference}`)
  } else if (value.integer !== undefined && value.integer !== null) {
    parts.push(String(value.integer))
  } else if (value.real !== undefined && value.real !== null) {
    parts.push(String(value.real))
  } else if (value.string !== undefined && value.string !== null) {
    parts.push(JSON.stringify(value.string))
  } else if (value.bytes && value.bytes.length) {
    const hex = value.bytes
      .slice(0, 16)
      .map((byte) => byte.toString(16).padStart(2, '0'))
      .join('')
    parts.push(`0x${hex}${value.bytes.length > 16 ? '…' : ''}`)
  } else if (value.repr) {
    parts.push(value.repr)
  }

  if (value.sizeInBits !== undefined && value.sizeInBits !== null) {
    parts.push(`${value.sizeInBits} bits`)
  }

  const valueSet = normalizeConstraints(value.valueSet)
  if (valueSet.length) {
    parts.push(`valueSet(${valueSet.length})`)
  }

  if (!parts.length) {
    return value.type || 'value'
  }

  return parts.join(' ')
}

function formatTag (tag) {
  if (!tag) return ''
  const parts = []
  if (tag.class) parts.push(tag.class)
  if (tag.mode && tag.mode !== 'default') parts.push(tag.mode)
  if (tag.value) parts.push(`#${tag.value}`)
  if (tag.description) parts.push(tag.description)
  return parts.join(' ')
}

function describeExpression (expr) {
  if (!expr) return 'expression'

  const head = []
  if (expr.metaType) head.push(expr.metaType)
  if (expr.exprType) head.push(expr.exprType)

  let description = head.length ? head.join(' · ') : 'expression'

  if (expr.identifier) {
    description += ` ${expr.identifier}`
  }

  const details = []

  if (expr.line) {
    details.push(`line ${expr.line}`)
  }

  if (expr.reference) {
    details.push(`ref:${expr.reference}`)
  }

  if (expr.markers && expr.markers.length) {
    details.push(`markers:${expr.markers.join(', ')}`)
  }

  if (expr.tag) {
    const tagSummary = formatTag(expr.tag)
    if (tagSummary) {
      details.push(`tag:${tagSummary}`)
    }
  }

  if (expr.value) {
    details.push(`value:${formatValue(expr.value)}`)
  }

  if (expr.defaultValue) {
    details.push(`default:${formatValue(expr.defaultValue)}`)
  }

  const constraints = normalizeConstraints(expr.constraints)
  if (constraints.length) {
    details.push(
      `constraints:${constraints.map(formatConstraintSummary).join(', ')}`
    )
  }

  if (expr.unique) {
    details.push('unique')
  }

  if (expr.truncated) {
    details.push('children truncated')
  }

  if (details.length) {
    description += ` (${details.join(' | ')})`
  }

  return description
}

function formatExpressionTree (expressions) {
  if (!expressions || expressions.length === 0) {
    return 'No members declared.'
  }

  const lines = []

  const visit = (nodes, depth) => {
    nodes.forEach((expr) => {
      lines.push(`${'  '.repeat(depth)}- ${describeExpression(expr)}`)
      if (expr.members && expr.members.length && !expr.truncated) {
        visit(expr.members, depth + 1)
      }
    })
  }

  visit(expressions, 0)
  return lines.join('\n')
}

function computeModuleStats (module) {
  const stats = {
    memberCount: Array.isArray(module?.members) ? module.members.length : 0,
    importsCount: Array.isArray(module?.imports) ? module.imports.length : 0,
    exportsCount: Array.isArray(module?.exports) ? module.exports.length : 0,
    flags: Object.entries(module?.flags || {})
      .filter(([, enabled]) => Boolean(enabled))
      .map(([name]) => name),
    metaTypeCounts: {},
    totalExpressions: 0,
    constraintCount: 0,
    valueCount: 0,
    truncated: false
  }

  const metaCounts = new Map()

  const visitConstraint = (constraint) => {
    if (!constraint) return
    stats.constraintCount += 1
    if (constraint.value) {
      stats.valueCount += 1
    }
    if (constraint.truncated) {
      stats.truncated = true
    }
    if (constraint.elements && constraint.elements.length) {
      constraint.elements.forEach((child) => visitConstraint(child))
    }
  }

  const visitExpression = (expr) => {
    if (!expr) return
    stats.totalExpressions += 1
    metaCounts.set(
      expr.metaType || 'unknown',
      (metaCounts.get(expr.metaType || 'unknown') || 0) + 1
    )

    if (expr.value) {
      stats.valueCount += 1
    }

    const constraints = normalizeConstraints(expr.constraints)
    constraints.forEach((constraint) => visitConstraint(constraint))

    if (expr.truncated) {
      stats.truncated = true
    }

    if (expr.members && expr.members.length) {
      expr.members.forEach((child) => visitExpression(child))
    }
  }

  if (Array.isArray(module?.members)) {
    module.members.forEach((expr) => visitExpression(expr))
  }

  stats.metaTypeCounts = Object.fromEntries(metaCounts)

  return stats
}

function Asn1ExampleApp () {
  const [activeSampleId, setActiveSampleId] = useState(
    ASN1_SAMPLES[0]?.id ?? null
  )
  const [source, setSource] = useState(ASN1_SAMPLES[0]?.source ?? '')
  const [status, setStatus] = useState(DEFAULT_STATUS)
  const [includeSourceText, setIncludeSourceText] = useState(true)
  const [lexerDebug, setLexerDebug] = useState(false)
  const [maxDepthInput, setMaxDepthInput] = useState(DEFAULT_MAX_DEPTH)
  const [parseResult, setParseResult] = useState(null)
  const [isParsing, setIsParsing] = useState(false)
  const [lastDuration, setLastDuration] = useState(null)
  const [lastOptions, setLastOptions] = useState(null)
  const [tabsKey, setTabsKey] = useState(0)
  const fileInputRef = useRef(null)

  const activeSample = useMemo(() => {
    return ASN1_SAMPLES.find((entry) => entry.id === activeSampleId) ?? null
  }, [activeSampleId])

  const maxDepth = useMemo(() => {
    const parsed = Number(maxDepthInput)
    if (!Number.isFinite(parsed) || parsed <= 0) {
      return 64
    }
    return Math.min(Math.floor(parsed), 1024)
  }, [maxDepthInput])

  const modules = parseResult?.modules ?? []

  const moduleSummaries = useMemo(() => {
    return modules.map((module, index) => {
      const stats = computeModuleStats(module)
      return {
        id: `module-${index}`,
        module,
        stats,
        tree: formatExpressionTree(module?.members ?? []),
        displayName: module?.name || `Module ${index + 1}`
      }
    })
  }, [modules])

  const handleSelectSample = useCallback((event) => {
    const sampleId = event.target.value
    setActiveSampleId(sampleId)
    const sample = ASN1_SAMPLES.find((entry) => entry.id === sampleId)
    if (sample) {
      setSource(sample.source)
      setStatus({
        variant: 'info',
        title: 'Sample loaded',
        message: sample.description
      })
    }
  }, [])

  const handleResetSample = useCallback(() => {
    if (!activeSample) return
    setSource(activeSample.source)
    setStatus({
      variant: 'info',
      title: 'Sample restored',
      message: activeSample.description
    })
  }, [activeSample])

  const handleFileSelection = useCallback(async (event) => {
    const file = event.target.files && event.target.files[0]
    if (!file) return
    try {
      const contents = await file.text()
      setSource(contents)
      setStatus({
        variant: 'info',
        title: 'File loaded',
        message: `Loaded ${file.name} (${contents.length} characters).`
      })
    } catch (error) {
      setStatus({
        variant: 'danger',
        title: 'Failed to read file',
        message: formatError(error)
      })
    } finally {
      event.target.value = ''
    }
  }, [])

  const runParse = useCallback(async () => {
    const trimmed = source.trim()
    if (!trimmed) {
      setParseResult(null)
      setStatus({
        variant: 'warning',
        title: 'Source required',
        message: 'Paste or load an ASN.1 module before parsing.'
      })
      return
    }

    setIsParsing(true)
    setStatus({
      variant: 'info',
      title: 'Parsing...',
      message: 'Dispatching ASN.1 document to the runtime.'
    })

    const startedAt =
      typeof performance !== 'undefined' && performance.now
        ? performance.now()
        : Date.now()

    try {
      const options = {
        includeSourceText,
        lexerDebug,
        maxDepth
      }
      const result = await parse(source, options)
      const endedAt =
        typeof performance !== 'undefined' && performance.now
          ? performance.now()
          : Date.now()
      const duration = endedAt - startedAt

      setParseResult(result)
      setLastDuration(duration)
      setLastOptions(options)
      setTabsKey((key) => key + 1)

      const truncatedModules =
        result.modules?.reduce((count, mod) => {
          const truncated = Array.isArray(mod?.members)
            ? mod.members.some((expr) => expr?.truncated)
            : false
          return truncated ? count + 1 : count
        }, 0) ?? 0

      setStatus({
        variant: 'success',
        title: `Parsed ${result.modulesCount ?? modules.length} module${(result.modulesCount ?? modules.length) === 1 ? '' : 's'}`,
        message: `Completed in ${formatDuration(duration)}${truncatedModules ? ` · ${truncatedModules} module${truncatedModules === 1 ? '' : 's'} hit the depth limit` : ''}.`
      })
    } catch (error) {
      setParseResult(null)
      setLastDuration(null)
      setLastOptions(null)
      setStatus({
        variant: 'danger',
        title: 'Parse failed',
        message: formatError(error)
      })
    } finally {
      setIsParsing(false)
    }
  }, [source, includeSourceText, lexerDebug, maxDepth, modules.length])

  const statusBanner = useMemo(() => {
    return h(StatusBanner, {
      variant: status.variant,
      title: status.title,
      message: status.message
    })
  }, [status])

  const overviewContent = useMemo(() => {
    if (!parseResult) {
      return h(EmptyState, {
        title: 'No parse result yet',
        description: 'Run the parser to inspect the generated module tree.'
      })
    }

    const metaItems = [
      `Modules: ${parseResult.modulesCount ?? modules.length}`,
      `Max depth: ${lastOptions?.maxDepth ?? maxDepth}`,
      `Lexer debug: ${lastOptions?.lexerDebug ? 'enabled' : 'disabled'}`,
      `Source text included: ${lastOptions?.includeSourceText ? 'yes' : 'no'}`
    ]

    if (parseResult.sourceText) {
      metaItems.push(
        `Source length: ${parseResult.sourceText.length} characters`
      )
    }

    if (Number.isFinite(lastDuration)) {
      metaItems.push(`Last duration: ${formatDuration(lastDuration)}`)
    }

    const tableRows = moduleSummaries.map(
      ({ id, module, stats, displayName }) => {
        const metaTypes = ['type', 'value', 'object', 'objectClass', 'valueSet']
        return h(
          TableRow,
          { key: id },
          h(
            TableCell,
            null,
            displayName,
            module.oid
              ? h(
                Badge,
                { variant: 'info', style: { marginLeft: '0.5rem' } },
                module.oid
              )
              : null,
            module.sourceFile
              ? h(
                Badge,
                { variant: 'neutral', style: { marginLeft: '0.5rem' } },
                module.sourceFile
              )
              : null
          ),
          h(TableCell, null, stats.memberCount),
          h(TableCell, null, stats.totalExpressions),
          h(
            TableCell,
            null,
            metaTypes
              .map((kind) => {
                const count = stats.metaTypeCounts[kind] || 0
                return count ? `${kind}:${count}` : null
              })
              .filter(Boolean)
              .join(', ') || '—'
          ),
          h(
            TableCell,
            null,
            [
              `constraints:${stats.constraintCount}`,
              `values:${stats.valueCount}`
            ].join(' · ')
          ),
          h(
            TableCell,
            null,
            stats.flags.length
              ? stats.flags.map((flag) =>
                h(
                  Badge,
                  {
                    key: flag,
                    variant: 'success',
                    style: { marginRight: '0.4rem' }
                  },
                  flag
                )
              )
              : '—'
          ),
          h(
            TableCell,
            null,
            stats.truncated
              ? h(Badge, { variant: 'warning' }, 'Depth limited')
              : h(Badge, { variant: 'success' }, 'Complete')
          ),
          h(
            TableCell,
            null,
            [
              `imports:${stats.importsCount}`,
              `exports:${stats.exportsCount}`
            ].join(' · ')
          )
        )
      }
    )

    return h(
      ExampleStack,
      { gap: 'lg' },
      h(
        ExampleSection,
        { title: 'Run metadata' },
        h(
          ExampleProse,
          null,
          h(
            'ul',
            null,
            metaItems.map((item, index) => h('li', { key: index }, item))
          )
        )
      ),
      h(
        ExampleSection,
        {
          title: 'Module summary',
          description: 'Counts are limited by the selected maxDepth.'
        },
        moduleSummaries.length
          ? h(
            ScrollArea,
            { style: { maxHeight: '320px' } },
            h(
              Table,
              null,
              h(
                TableHead,
                null,
                h(
                  TableRow,
                  null,
                  h(TableHeader, null, 'Module'),
                  h(TableHeader, null, 'Top-level'),
                  h(TableHeader, null, 'Expressions'),
                  h(TableHeader, null, 'Meta types'),
                  h(TableHeader, null, 'Values & constraints'),
                  h(TableHeader, null, 'Flags'),
                  h(TableHeader, null, 'Coverage'),
                  h(TableHeader, null, 'Imports/Exports')
                )
              ),
              h(TableBody, null, tableRows)
            )
          )
          : h(EmptyState, {
            description: 'No modules parsed yet.'
          })
      )
    )
  }, [
    parseResult,
    modules.length,
    lastOptions,
    maxDepth,
    lastDuration,
    moduleSummaries
  ])

  const modulesContent = useMemo(() => {
    if (!moduleSummaries.length) {
      return h(EmptyState, {
        title: 'No modules available',
        description: 'Parse a document to explore its module members.'
      })
    }

    const innerTabs = h(
      Tabs,
      {
        defaultValue: moduleSummaries[0]?.id,
        key: `${tabsKey}-modules`
      },
      h(
        TabsList,
        null,
        moduleSummaries.map(({ id, displayName, stats }) =>
          h(
            TabsTrigger,
            {
              key: id,
              value: id
            },
            `${displayName} (${stats.memberCount})`
          )
        )
      ),
      moduleSummaries.map(({ id, module, stats, tree }) => {
        const metaEntries = Object.entries(stats.metaTypeCounts)
          .filter(([, value]) => value)
          .sort((a, b) => b[1] - a[1])

        const overviewItems = [
          h(
            'li',
            { key: 'members' },
            `Top-level members: ${stats.memberCount}`
          ),
          h(
            'li',
            { key: 'expressions' },
            `Expressions visited: ${stats.totalExpressions}`
          ),
          h(
            'li',
            { key: 'constraints' },
            `Constraint nodes: ${stats.constraintCount}`
          ),
          h('li', { key: 'values' }, `Value nodes: ${stats.valueCount}`),
          module.oid
            ? h('li', { key: 'oid' }, `Module OID: ${module.oid}`)
            : null,
          module.sourceFile
            ? h(
              'li',
              { key: 'source' },
                `Source file hint: ${module.sourceFile}`
            )
            : null,
          stats.flags.length
            ? h(
              'li',
              { key: 'flags' },
              'Flags: ',
              stats.flags.map((flag) =>
                h(
                  Badge,
                  {
                    key: flag,
                    variant: 'neutral',
                    style: { marginRight: '0.4rem' }
                  },
                  flag
                )
              )
            )
            : null,
          metaEntries.length
            ? h(
              'li',
              { key: 'meta' },
              'Meta types: ',
              metaEntries.map(([kind, value]) =>
                h(
                  Badge,
                  {
                    key: kind,
                    variant: 'info',
                    style: { marginRight: '0.4rem' }
                  },
                    `${kind}:${value}`
                )
              )
            )
            : null
        ].filter(Boolean)

        return h(
          TabsContent,
          { key: id, value: id },
          h(
            ExampleStack,
            { gap: 'lg' },
            h(
              ExampleSection,
              { title: 'Overview' },
              h(ExampleProse, null, h('ul', null, overviewItems))
            ),
            h(
              ExampleSection,
              {
                title: 'Member tree',
                description:
                  'Indentation reflects the traversal depth returned by the parser.'
              },
              h(
                ScrollArea,
                { style: { maxHeight: '360px' } },
                h(Code, null, tree)
              )
            ),
            h(
              ExampleSection,
              { title: 'Raw JSON' },
              h(
                ScrollArea,
                { style: { maxHeight: '360px' } },
                h(Code, null, toPrettyJson(module))
              )
            ),
            module.imports && module.imports.length
              ? h(
                ExampleSection,
                { title: 'Imports' },
                h(
                  ExampleProse,
                  null,
                  h(
                    'ul',
                    null,
                    module.imports.map((entry, index) => {
                      const symbolList = (entry.symbols || [])
                        .map(
                          (symbol) =>
                            symbol.identifier ||
                              `${symbol.metaType}:${symbol.exprType}`
                        )
                        .join(', ')
                      return h(
                        'li',
                        { key: index },
                        h(InlineCode, null, entry.module || 'anonymous'),
                        symbolList ? ` ← ${symbolList}` : '',
                        entry.kind ? ` (${entry.kind})` : ''
                      )
                    })
                  )
                )
              )
              : null,
            module.exports && module.exports.length
              ? h(
                ExampleSection,
                { title: 'Exports' },
                h(
                  ExampleProse,
                  null,
                  h(
                    'ul',
                    null,
                    module.exports.map((entry, index) =>
                      h(
                        'li',
                        { key: index },
                        entry.identifier
                          ? h(InlineCode, null, entry.identifier)
                          : h(
                            InlineCode,
                            null,
                                `${entry.metaType}:${entry.exprType}`
                          ),
                        entry.identifier
                          ? ` (${entry.metaType}:${entry.exprType})`
                          : ''
                      )
                    )
                  )
                )
              )
              : null
          )
        )
      })
    )

    return innerTabs
  }, [moduleSummaries, tabsKey])

  const documentJsonContent = useMemo(() => {
    if (!parseResult) {
      return h(EmptyState, {
        description: 'No document to display.'
      })
    }
    return h(
      ScrollArea,
      { style: { maxHeight: '480px' } },
      h(Code, null, toPrettyJson(parseResult))
    )
  }, [parseResult])

  const sourceContent = useMemo(() => {
    if (!parseResult?.sourceText) {
      return h(EmptyState, {
        title: 'Source text not requested',
        description:
          'Enable “Include source text” before parsing to view the captured module.'
      })
    }

    return h(
      ScrollArea,
      { style: { maxHeight: '480px' } },
      h(Code, null, parseResult.sourceText)
    )
  }, [parseResult])

  const tabs = h(
    Tabs,
    {
      key: tabsKey,
      defaultValue: 'overview'
    },
    h(
      TabsList,
      null,
      h(TabsTrigger, { value: 'overview' }, 'Overview'),
      h(
        TabsTrigger,
        { value: 'modules' },
        `Modules (${moduleSummaries.length})`
      ),
      h(TabsTrigger, { value: 'document' }, 'Document JSON'),
      parseResult?.sourceText
        ? h(TabsTrigger, { value: 'source' }, 'Source text')
        : null
    ),
    h(TabsContent, { value: 'overview' }, overviewContent),
    h(TabsContent, { value: 'modules' }, modulesContent),
    h(TabsContent, { value: 'document' }, documentJsonContent),
    parseResult?.sourceText
      ? h(TabsContent, { value: 'source' }, sourceContent)
      : null
  )

  return h(
    ExampleLayout,
    {
      title: 'ASN.1 Playground',
      description: h(
        ExampleProse,
        null,
        'Parse ASN.1 modules via ',
        h(InlineCode, null, 'oro:asn1'),
        ' and inspect the generated document tree.'
      )
    },
    h(
      ExampleGrid,
      { columns: 2 },
      h(
        ExamplePanel,
        {
          title: 'ASN.1 input',
          description:
            'Load a sample definition or provide your own schema, then configure parser limits.'
        },
        h(
          ExampleStack,
          { gap: 'lg' },
          h(
            ExampleSection,
            { title: 'Samples' },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                Select,
                {
                  value: activeSampleId ?? '',
                  onChange: handleSelectSample
                },
                ASN1_SAMPLES.map((sample) =>
                  h(Option, { key: sample.id, value: sample.id }, sample.title)
                )
              ),
              activeSample?.description
                ? h(ExampleProse, null, activeSample.description)
                : null,
              h(
                ExampleStack,
                { gap: 'sm' },
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'ghost',
                    size: 'sm',
                    onClick: handleResetSample,
                    disabled: !activeSample
                  },
                  'Reset to sample'
                ),
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'ghost',
                    size: 'sm',
                    onClick: () => {
                      if (fileInputRef.current) {
                        fileInputRef.current.click()
                      }
                    }
                  },
                  'Import .asn1 file'
                ),
                h('input', {
                  ref: fileInputRef,
                  type: 'file',
                  accept: '.asn1,.txt',
                  style: { display: 'none' },
                  onChange: handleFileSelection
                })
              )
            )
          ),
          h(
            ExampleSection,
            { title: 'Source' },
            h(Textarea, {
              value: source,
              onChange: (event) => setSource(event.target.value),
              rows: 18,
              spellCheck: 'false'
            })
          ),
          h(
            ExampleSection,
            { title: 'Options' },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                Checkbox,
                {
                  checked: includeSourceText,
                  onChange: (event) =>
                    setIncludeSourceText(event.target.checked)
                },
                'Include source text in response'
              ),
              h(
                Checkbox,
                {
                  checked: lexerDebug,
                  onChange: (event) => setLexerDebug(event.target.checked)
                },
                'Enable lexer debug traces'
              ),
              h(
                FormField,
                {
                  label: 'Traversal depth limit',
                  htmlFor: 'asn1-max-depth',
                  description:
                    'Maximum nested member depth retained in the JSON tree (1-1024).'
                },
                h(Input, {
                  id: 'asn1-max-depth',
                  type: 'number',
                  min: 1,
                  max: 1024,
                  step: 1,
                  value: maxDepthInput,
                  onChange: (event) => setMaxDepthInput(event.target.value)
                })
              )
            )
          ),
          h(
            ExampleSection,
            { title: 'Actions' },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                Button,
                {
                  type: 'button',
                  onClick: () => runParse().catch(() => {}),
                  disabled: isParsing
                },
                isParsing ? 'Parsing…' : 'Parse ASN.1'
              )
            )
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Parse results',
          description:
            'Inspect the generated module metadata, expression tree, and raw JSON payload.'
        },
        h(ExampleStack, { gap: 'lg' }, statusBanner, tabs)
      )
    )
  )
}

mountExample(Asn1ExampleApp)
