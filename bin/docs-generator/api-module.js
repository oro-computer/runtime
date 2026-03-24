import * as acorn from 'acorn'
import * as walk from 'acorn-walk'

function escapeTableCell (value) {
  return String(value ?? '')
    .replace(/\|/g, '\\|')
    .replace(/\n/g, '<br>')
}

function getBalancedType (source) {
  const start = source.indexOf('{')
  if (start === -1) return null

  let depth = 0
  let end = -1

  for (let i = start; i < source.length; i++) {
    const char = source[i]
    if (char === '{') {
      depth++
    } else if (char === '}') {
      depth--
      if (depth === 0) {
        end = i
        break
      }
    }
  }

  if (end === -1) return null

  return {
    rawType: source.slice(start + 1, end),
    rest: source.slice(end + 1).trim()
  }
}

function normalizeType (rawType) {
  const withoutOptionalSuffix = rawType.replace(/=$/, '').trim()
  const unwrapped =
    withoutOptionalSuffix.match(/^\((.*)\)$/)?.[1] ?? withoutOptionalSuffix
  return unwrapped.replace(/\s*\|\s*/g, ' \\| ')
}

function parseNameToken (source) {
  if (!source) {
    return { nameToken: '', rest: '' }
  }

  if (source[0] === '[') {
    let depth = 0
    for (let i = 0; i < source.length; i++) {
      const char = source[i]
      if (char === '[') depth++
      if (char === ']') {
        depth--
        if (depth === 0) {
          return {
            nameToken: source.slice(0, i + 1),
            rest: source.slice(i + 1).trim()
          }
        }
      }
    }
  }

  const match = source.match(/^(\S+)([\s\S]*)$/)
  if (!match) {
    return { nameToken: '', rest: '' }
  }

  return {
    nameToken: match[1],
    rest: match[2].trim()
  }
}

function parseDocParam (attr, position = 0) {
  const payload = attr.replace(/^@(param|arg|argument)\s+/, '')
  const parsedType = getBalancedType(payload)
  if (!parsedType) return null

  const { rawType, rest } = parsedType
  const { nameToken, rest: remaining } = parseNameToken(rest)
  const optional = rawType.trim().endsWith('=') || /^\[.*\]$/.test(nameToken)

  const cleanNameToken = nameToken.replace(/^\[|\]$/g, '').trim()
  const assignmentIndex = cleanNameToken.indexOf('=')
  const name =
    assignmentIndex === -1
      ? cleanNameToken
      : cleanNameToken.slice(0, assignmentIndex).trim()
  const defaultValue =
    assignmentIndex === -1
      ? ''
      : cleanNameToken.slice(assignmentIndex + 1).trim()

  const description = remaining.replace(/^-\s*/, '').trim()

  return {
    name: name || `(Position ${position})`,
    type: normalizeType(rawType),
    default: defaultValue,
    optional,
    desc: description
  }
}

function parseDocReturn (attr) {
  const payload = attr.replace(/^@returns?\s+/, '')
  const parsedType = getBalancedType(payload)
  if (!parsedType) return null

  const description = parsedType.rest.replace(/^-\s*/, '').trim()
  const type = normalizeType(parsedType.rawType)

  if (['undefined', 'void'].includes(type)) {
    return null
  }

  return {
    name: 'Not specified',
    type,
    description
  }
}

function escapeRoffText (text) {
  let value = String(text ?? '')
    .replace(/\\/g, '\\\\')
    .replace(/-/g, '\\-')

  if (value.startsWith('.') || value.startsWith("'")) {
    value = `\\&${value}`
  }

  return value
}

function normalizeParagraphs (lines) {
  if (!lines?.length) return []

  const cleaned = lines.join('\n').replace(/```[a-z]*\s*([\s\S]*?)```/gi, '$1')

  return cleaned
    .split(/\n\s*\n/g)
    .map((paragraph) => paragraph.replace(/\s+/g, ' ').trim())
    .filter(Boolean)
}

function normalizeMarkdownText (text) {
  const lines = String(text ?? '').split('\n')
  let inFence = false

  const normalized = lines.map((line) => {
    const trimmedRight = line.trimEnd()
    const trimmed = trimmedRight.trim()

    if (trimmed.startsWith('```')) {
      inFence = !inFence
      return trimmed
    }

    if (inFence) {
      return trimmedRight.replace(/^\s/, '')
    }

    return trimmedRight.replace(/^\s+/, '')
  })

  return normalized
    .join('\n')
    .replace(/\n{3,}/g, '\n\n')
    .trim()
}

function renderRoffParagraphs (lines) {
  return normalizeParagraphs(lines)
    .map((paragraph) => `.PP\n${escapeRoffText(paragraph)}\n`)
    .join('')
}

function stripBackticks (value) {
  return String(value ?? '').replace(/`/g, '')
}

function formatDocTypeLabel (type) {
  return (
    {
      ClassDeclaration: 'class',
      FunctionDeclaration: 'function',
      MethodDefinition: 'method',
      Module: 'module',
      Property: 'property',
      VariableDeclaration: 'variable'
    }[type] || 'symbol'
  )
}

function formatModuleSpecifier (location, explicitModuleSpecifier) {
  if (explicitModuleSpecifier) {
    return explicitModuleSpecifier
  }

  const relative = String(location)
    .replace(/^api\//, '')
    .replace(/\.js$/, '')
    .replace(/\\/g, '/')

  return `oro:${relative}`
}

function formatModuleManpageName (moduleSpecifier) {
  return moduleSpecifier.replace(/^oro:/, 'oro-').replace(/[/:]+/g, '-')
}

function renderRoffParamList (label, entries, formatter) {
  if (!entries?.length) return ''

  let output = `\\fB${escapeRoffText(label)}:\\fR\n`

  entries.forEach((entry) => {
    output += '.br\n'
    output += formatter(entry)
    output += '\n'
  })

  return output
}

function formatDocParam (param) {
  const required = param.optional ? 'optional' : 'required'
  const defaultValue = param.default ? `; default ${param.default}` : ''
  const description = param.desc ? ` ${param.desc}` : ''
  return `\\fI${escapeRoffText(param.name)}\\fR (${escapeRoffText(param.type)}; ${required}${escapeRoffText(defaultValue)})${escapeRoffText(description)}`
}

function formatDocReturn (value) {
  const description = value.description ? ` ${value.description}` : ''
  return `\\fI${escapeRoffText(value.type)}\\fR${escapeRoffText(description)}`
}

function renderDocEntry (doc) {
  let output = '.TP\n'
  output += `\\fB${escapeRoffText(stripBackticks(doc.name))}\\fR\n`

  const paragraphs = normalizeParagraphs(doc.header).map((paragraph) => {
    if (paragraph.includes("it's exported but undocumented")) {
      return `Exported ${formatDocTypeLabel(doc.type)}. Refer to the generated type declarations and neighboring section 3 pages for the supported surface.`
    }

    return paragraph
  })
  if (paragraphs.length > 0) {
    output += `${escapeRoffText(paragraphs[0])}\n`
    paragraphs.slice(1).forEach((paragraph) => {
      output += '.br\n'
      output += `${escapeRoffText(paragraph)}\n`
    })
  } else {
    output += `${escapeRoffText(`Documented ${formatDocTypeLabel(doc.type)} exposed by this module.`)}\n`
  }

  output += renderRoffParamList('Parameters', doc.params, formatDocParam)
  output += renderRoffParamList('Returns', doc.returns, formatDocReturn)
  return output
}

function collectApiModuleDocs ({ src, location, moduleSpecifier }) {
  let accumulateComments = []
  const comments = {}
  const ast = acorn.parse(String(src), {
    tokens: true,
    comment: true,
    ecmaVersion: 'latest',
    sourceType: 'module',
    onToken: (token) => {
      comments[token.start] = accumulateComments
      accumulateComments = []
    },
    onComment: (block, comment) => {
      if (!block) return
      if (comment[0] !== '*') return // not a JSDoc comment

      comment = comment.replace(/^\s*\*/gm, '').trim()
      comment = comment.replace(/^\n/, '')
      const hasTypedef = comment.match(/(^|\n)\s*@typedef\b/)
      const hasRuntimeDoc = comment.match(
        /(^|\n)\s*@(param|arg|argument|returns?|module|see|link|event)\b/
      )

      if (hasTypedef && !hasRuntimeDoc) return
      if (hasTypedef) {
        comment = comment
          .split('\n')
          .filter((line) => !line.trim().startsWith('@typedef'))
          .join('\n')
          .trim()
      }

      accumulateComments.push(comment.trim())
    },
    locations: true
  })

  for (const [key, value] of Object.entries(comments)) {
    if (!value.length) delete comments[key] // release empty items
  }

  const docs = []
  let header = 'Unknown module'
  const shouldDocumentNode = (node, item) => {
    if (item.type === 'Module') return true
    if (item.export) return true
    return node.type.includes('MethodDefinition') || node.type === 'Property'
  }

  const onNode = (node) => {
    const item = {
      sort: node.loc.start.line,
      location: `/${location}#L${node.loc.start.line}`,
      type: node.type,
      name: node.name,
      export: node?.type.includes('Export'),
      header: comments[node.start]
    }

    if (item.header?.join('').match(/@(ignore|private)\b/)) {
      return
    }

    if (item.header?.join('').includes('@module')) {
      item.type = 'Module'
      const name = item.header.join('').match(/@module\s*(.*)/)
      if (name) {
        item.name = name[1]
        header = item.name
      }
    }

    if (item.header?.join('').match(/(^|\n)\s*@link\b/)) {
      const url = item.header.join('').match(/(^|\n)\s*@link\s*(.*)}/)
      if (url) item.url = url[2].trim()
    }

    if (node.type.includes('ExportAllDeclaration')) {
      return
    }

    if (node.type.includes('ExportDefaultDeclaration')) {
      return
    }

    if (node.type.includes('ExportNamedDeclaration')) {
      const firstDeclaration = node.declarations
        ? node.declarations[0]
        : node.declaration
      if (!firstDeclaration) return

      item.type = firstDeclaration.type || item.type

      if (item.type === 'VariableDeclaration') {
        item.name = node.declaration.declarations[0].id.name
      } else {
        item.name = node.declaration.id.name
      }

      if (node.declaration.superClass) {
        item.name = `\`${item.name}\` (extends \`${node.declaration.superClass.name}\`)`
      }

      if (item.type === 'FunctionDeclaration') {
        item.params = [] // node.declaration.params
        item.signature = []
      }
    }

    if (node.type.includes('MethodDefinition')) {
      item.name = node.key?.name
      item.signature = []

      if (node.value.type === 'FunctionExpression') {
        item.generator = node.value.generator
        item.static = node.static
        item.async = node.value.async
        item.params = []
        item.returns = []
      }
    }

    if (node.type === 'Property') {
      item.name = node.key?.name
      item.signature = []

      if (node.value.type === 'FunctionExpression') {
        item.generator = node.value.generator
        item.static = node.static
        item.async = node.value.async
        item.signature = []
        item.params = []
        item.returns = []
      }
    }

    if (!item.name && item.type !== 'Module') {
      return
    }

    if (item.export && !item.header) {
      item.header = [
        `This is an exported \`${item.type}\` named \`${item.name}\`, but it does not yet have a dedicated description in the generated reference.\n`
      ]
    }

    const attrs = item.header?.join('\n').match(/@(.*)[\n$]*/g)

    if (attrs) {
      let position = 0

      for (const attr of attrs) {
        const isParam = attr.match(/^@(param|arg|argument)/)
        const isReturn = attr.match(/^@(returns?)/)

        if (isParam) {
          const propType = 'params'
          item.signature = item.signature || []
          const param = parseDocParam(attr, position++)
          if (!param) continue

          const params = node.declaration?.params || node.value?.params
          if (params) {
            const assign = params.find((o) => o.left?.name === param.name)
            if (assign) param.default = assign.right.raw
          }

          if (!item[propType]) item[propType] = []
          item[propType].push(param)
          if (propType === 'params' && !param.name.includes('.')) {
            item.signature.push(param.name)
          }
        }

        if (isReturn) {
          const propType = 'returns'
          const param = parseDocReturn(attr)
          if (!param) continue
          if (!item[propType]) item[propType] = []
          item[propType].push(param)
        }
      }
    }

    // Ensure signature reflects actual function parameters if available
    if (item.type === 'FunctionDeclaration' && node.declaration?.params) {
      const names = node.declaration.params.map((p) => {
        if (p.type === 'RestElement') return `...${p.argument?.name || 'args'}`
        if (p.type === 'AssignmentPattern') return p.left?.name || 'arg'
        return p.name || 'arg'
      })
      item.signature = names
    }

    if (item.signature && item.type !== 'ClassDeclaration') {
      item.name = `\`${item.name}(${item.signature?.join(', ') || ''})\``
    } else if (item.exports) {
      item.name = `\`${item.name}\``
    }

    if (item.header) {
      if (!shouldDocumentNode(node, item)) {
        return
      }
      item.header = item.header
        .join('\n')
        .split('\n')
        .filter((line) => !line.trim().startsWith('@'))
      docs.push(item)
    }
  }

  walk.full(ast, onNode)
  docs.sort((a, b) => a.sort - b.sort)

  return {
    docs,
    header,
    location,
    moduleSpecifier: formatModuleSpecifier(location, moduleSpecifier)
  }
}

export function generateApiModuleDoc (options) {
  const { docs, header } = collectApiModuleDocs(options)

  const createTableParams = (arr) => {
    if (!arr || !arr.length) return []

    const tableHeader = [
      '| Argument | Type | Default | Optional | Description |',
      '| :---     | :--- | :---:   | :---:    | :---        |'
    ].join('\n')

    let table = `${tableHeader}\n`

    for (const param of arr) {
      table += `| ${escapeTableCell(param.name)} | ${escapeTableCell(param.type)} | ${escapeTableCell(param.default)} | ${escapeTableCell(param.optional)} | ${escapeTableCell(param.desc)} |\n`
    }

    return table + '\n'
  }

  const createTableReturn = (arr) => {
    if (!arr?.length) return []

    const tableHeader = [
      '| Return Value | Type | Description |',
      '| :---         | :--- | :---        |'
    ].join('\n')

    let table = `${tableHeader}\n`

    for (const param of arr) {
      table += `| ${escapeTableCell(param.name)} | ${escapeTableCell(param.type)} | ${escapeTableCell(param.description)} |\n`
    }

    return table + '\n'
  }

  let content = ''

  for (const doc of docs) {
    let h = doc.export ? '##' : '###'
    if (doc.type === 'Module') h = '#'

    const title = `${h} ${doc.name}\n`
    const header = normalizeMarkdownText(doc.header.join('\n'))

    content += title ? `${title}\n` : ''
    content += doc?.url ? `External docs: ${doc.url}\n` : ''
    content += header ? `\n${header}\n` : '\n'
    content += createTableParams(doc?.params)
    content += createTableReturn(doc?.returns)
  }

  return { content, header }
}

export function generateApiModuleManpage (options) {
  const { docs, moduleSpecifier } = collectApiModuleDocs(options)

  const filenameBase = formatModuleManpageName(moduleSpecifier)
  const moduleDoc = docs.find((doc) => doc.type === 'Module')
  const referenceDocs = docs.filter((doc) => doc.type !== 'Module')
  const moduleDescription = moduleDoc?.header?.length
    ? moduleDoc.header
    : [
        `${moduleSpecifier} is a JavaScript API module exposed by the Oro Runtime.`
      ]

  let man = `.TH ${filenameBase.toUpperCase()} 3 "" "Oro Runtime" "Oro Runtime API Manual"\n`
  man += '.SH NAME\n'
  man += `${escapeRoffText(moduleSpecifier)} \\- ${escapeRoffText('JavaScript API module')}\n`
  man += '.SH SYNOPSIS\n'
  man += '.nf\n'
  man += `${escapeRoffText(`import * as module from '${moduleSpecifier}'`)}\n`
  man += '.fi\n'
  man += '.SH DESCRIPTION\n'
  man += renderRoffParagraphs(moduleDescription)

  if (referenceDocs.length > 0) {
    man += '.SH REFERENCE\n'
    referenceDocs.forEach((doc) => {
      man += renderDocEntry(doc)
    })
  } else {
    man += '.SH REFERENCE\n'
    man += '.PP\n'
    man += `${escapeRoffText('This module primarily re-exports symbols from sibling modules. Refer to the generated type declarations and related section 3 pages for the exported surface.')}\n`
  }

  return {
    filename: `${filenameBase}.3`,
    content: man,
    moduleSpecifier
  }
}
