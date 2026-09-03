function escapeRoffText (text) {
  let value = String(text ?? '')
    .replace(/\\/g, '\\\\')
    .replace(/-/g, '\\-')

  if (value.startsWith('.') || value.startsWith("'")) {
    value = `\\&${value}`
  }

  return value
}

function normalizeWhitespace (text) {
  return String(text ?? '')
    .replace(/\s+/g, ' ')
    .trim()
}

function normalizeComment (text) {
  return String(text ?? '')
    .replace(/^\/\*\*?!?/, '')
    .replace(/\*\/$/, '')
    .split('\n')
    .map((line) => line.replace(/^\s*\*\s?/, '').trimEnd())
    .map((line) => line.replace(/^[@\\]brief\b\s*/, ''))
    .filter((line) => !/^[@\\](param|return|returns?|file)\b/.test(line.trim()))
    .join('\n')
    .trim()
}

function normalizeLineComment (text) {
  return String(text ?? '')
    .split('\n')
    .map((line) => line.replace(/^\s*\/\/\s?/, '').trimEnd())
    .join('\n')
    .trim()
}

function normalizeParagraphs (text) {
  return String(text ?? '')
    .split(/\n\s*\n/g)
    .map((paragraph) => paragraph.replace(/\s+/g, ' ').trim())
    .filter(Boolean)
}

function wrapLiteralText (text, width = 72) {
  const normalized = normalizeWhitespace(text)
  if (normalized.length <= width) {
    return normalized
  }

  const words = normalized.split(' ')
  const lines = []
  let current = ''

  for (const word of words) {
    const candidate = current ? `${current} ${word}` : word
    if (candidate.length <= width || !current) {
      current = candidate
      continue
    }

    lines.push(current)
    current = word
  }

  if (current) {
    lines.push(current)
  }

  return lines.join('\n')
}

function renderParagraphs (text) {
  return normalizeParagraphs(text)
    .map((paragraph) => `.PP\n${escapeRoffText(paragraph)}\n`)
    .join('')
}

function stripIncludePrefix (location) {
  return location.replace(/^include\//, '')
}

const HEADER_MANIFEST = {
  'include/oro/extension.h': {
    pageName: 'oro-extension-c',
    summary: 'public C extension ABI for Oro Runtime',
    description: [
      'The oro/extension.h header defines the native extension ABI used by Oro Runtime loadable modules and embedders.',
      'It includes the extension registration contract, extension context lifecycle helpers, JSON construction macros and types, IPC bridge types, runtime service metadata, and the canonical oapi_* entry points.'
    ],
    allowSymbol (name, kind) {
      if (kind === 'macro') {
        return (
          name.startsWith('ORO_RUNTIME_EXTENSION_ABI_VERSION') ||
          name.startsWith('OAPI_JSON_') ||
          name.startsWith('oapi_json_')
        )
      }

      return /^(__oapi_|oapi_)/.test(name)
    }
  },
  'include/oro/runtime_init.h': {
    pageName: 'oro-runtime-init',
    summary: 'embedder initialization and configuration helpers',
    description: [
      'The oro/runtime_init.h header exposes build-time runtime metadata to host applications that embed Oro Runtime.',
      'Callers can inspect embedded user configuration bytes, debug mode, development host settings, and the encoded configuration format without depending on internal runtime state.'
    ],
    allowSymbol (name, kind) {
      if (kind === 'macro') {
        return false
      }

      return name.startsWith('oro_runtime_')
    }
  },
  'include/oro/platform.h': {
    pageName: 'oro-platform',
    summary: 'compile-time platform and architecture feature macros',
    description: [
      'The oro/platform.h header defines the compile-time platform, operating system, and architecture macros used by the public runtime and extension APIs.',
      'Use these macros to gate source compatibility for desktop, mobile, Apple, Windows, Unix, and cross-compiled targets without duplicating platform detection logic.'
    ],
    allowSymbol (name, kind) {
      if (kind !== 'macro') {
        return false
      }

      return (
        name.startsWith('ORO_RUNTIME_PLATFORM') &&
        name !== 'ORO_RUNTIME_PLATFORM_H'
      )
    }
  },
  'include/oro/dbus.h': {
    pageName: 'oro-dbus-c',
    summary: 'D-Bus feature-detection macros for Oro Runtime builds',
    description: [
      'The oro/dbus.h header centralizes detection of libdbus header availability for builds that integrate D-Bus-backed services.',
      'It keeps the public contract intentionally small: include the header, inspect ORO_RUNTIME_HAVE_DBUS, and compile optional integrations accordingly.'
    ],
    allowSymbol (name) {
      return name === 'ORO_RUNTIME_HAVE_DBUS'
    }
  },
  'include/iroh/oro_iroh.h': {
    pageName: 'oro-iroh-c',
    summary: 'experimental UniFFI-backed iroh bridge for native runtime code',
    description: [
      'The iroh/oro_iroh.h header declares the experimental C ABI that bridges Oro Runtime to the Rust-based iroh implementation.',
      'The header is intentionally opt-in while the UniFFI migration is in progress. Callers must define ORO_IROH_HEADER_ALLOW_INCOMPLETE before including it and should treat the surface as evolving until the migration is complete.'
    ],
    allowSymbol (name, kind) {
      if (kind === 'macro') {
        return name === 'ORO_IROH_ERROR_CODE_TIMEOUT'
      }

      return name.startsWith('oro_iroh_')
    }
  }
}

function collectComment (lines, startIndex) {
  const line = lines[startIndex]
  const trimmed = line.trim()

  if (trimmed.startsWith('/**') || trimmed.startsWith('/*!')) {
    const comment = [line]
    let index = startIndex

    while (
      !comment[comment.length - 1].includes('*/') &&
      index < lines.length - 1
    ) {
      index++
      comment.push(lines[index])
    }

    return {
      comment: normalizeComment(comment.join('\n')),
      nextIndex: index + 1
    }
  }

  if (trimmed.startsWith('//')) {
    const comment = [line]
    let index = startIndex

    while (
      index + 1 < lines.length &&
      lines[index + 1].trim().startsWith('//')
    ) {
      index++
      comment.push(lines[index])
    }

    return {
      comment: normalizeLineComment(comment.join('\n')),
      nextIndex: index + 1
    }
  }

  return null
}

function collectMacro (lines, startIndex) {
  const declaration = [lines[startIndex]]
  let index = startIndex

  while (
    declaration[declaration.length - 1].trim().endsWith('\\') &&
    index < lines.length - 1
  ) {
    index++
    declaration.push(lines[index])
  }

  return {
    declaration: declaration.join('\n'),
    nextIndex: index + 1
  }
}

function collectDeclaration (lines, startIndex) {
  const declaration = []
  let index = startIndex
  let braceDepth = 0

  while (index < lines.length) {
    const line = lines[index]
    declaration.push(line)

    for (const char of line) {
      if (char === '{') {
        braceDepth++
      } else if (char === '}') {
        braceDepth = Math.max(0, braceDepth - 1)
      }
    }

    if (braceDepth === 0 && line.includes(';')) {
      break
    }

    index++
  }

  return {
    declaration: declaration.join('\n'),
    nextIndex: index + 1
  }
}

function classifyDeclaration (declaration) {
  const normalized = normalizeWhitespace(declaration)

  if (!normalized) return null
  if (normalized.startsWith('#define ')) return 'macro'
  if (normalized.startsWith('typedef ') || normalized.startsWith('enum ')) {
    return 'type'
  }
  if (normalized.endsWith(';') && normalized.includes('(')) return 'function'
  return null
}

function extractSymbolName (kind, declaration) {
  const normalized = normalizeWhitespace(declaration)

  if (kind === 'macro') {
    return normalized.match(/^#define\s+([A-Za-z_]\w*)/)?.[1] ?? null
  }

  if (kind === 'type') {
    const functionPointer = normalized.match(
      /\(\s*\*\s*([A-Za-z_]\w*)\s*\)/
    )?.[1]
    if (functionPointer) {
      return functionPointer
    }

    if (normalized.startsWith('enum ')) {
      return normalized.match(/^enum\s+([A-Za-z_]\w*)/)?.[1] ?? null
    }

    return normalized.match(/([A-Za-z_]\w*)\s*;$/)?.[1] ?? null
  }

  if (kind === 'function') {
    const beforeParen = normalized.slice(0, normalized.indexOf('(')).trim()
    return beforeParen.split(/\s+/).pop()?.replace(/^\*+/, '') ?? null
  }

  return null
}

function formatDeclarationSnippet (symbol) {
  const normalized = normalizeWhitespace(
    symbol.declaration.replace(/\\\n/g, ' ').replace(/\s*\\\s*/g, ' ')
  )

  if (symbol.kind === 'type' && normalized.includes('{')) {
    const prefix = normalized.slice(0, normalized.indexOf('{')).trim()
    const suffix = normalized.slice(normalized.lastIndexOf('}') + 1).trim()
    return wrapLiteralText(`${prefix} { ... } ${suffix}`.trim())
  }

  return wrapLiteralText(normalized)
}

function fallbackDescription (symbol, location) {
  const label =
    symbol.kind === 'macro'
      ? 'Public macro'
      : symbol.kind === 'type'
        ? 'Public type'
        : 'Public function'

  return `${label} declared in ${stripIncludePrefix(location)}.`
}

function extractHeaderSymbols (source, location) {
  const manifest = HEADER_MANIFEST[location]
  const lines = String(source).split('\n')
  const symbols = []
  let pendingComment = ''

  for (let index = 0; index < lines.length;) {
    const line = lines[index]
    const trimmed = line.trim()

    if (!trimmed) {
      index++
      continue
    }

    const comment = collectComment(lines, index)
    if (comment) {
      pendingComment = comment.comment
      index = comment.nextIndex
      continue
    }

    let collected = null

    if (trimmed.startsWith('#define ')) {
      collected = collectMacro(lines, index)
    } else if (
      trimmed === 'ORO_RUNTIME_EXTENSION_EXPORT' ||
      trimmed.startsWith('typedef ') ||
      trimmed.startsWith('enum ') ||
      /^(?:const|bool|void|int|unsigned|char|size_t|uint\d+_t)\b/.test(trimmed)
    ) {
      collected = collectDeclaration(lines, index)
    }

    if (!collected) {
      index++
      continue
    }

    const kind = classifyDeclaration(collected.declaration)
    const name = kind ? extractSymbolName(kind, collected.declaration) : null

    if (kind && name && manifest.allowSymbol(name, kind)) {
      symbols.push({
        kind,
        name,
        declaration: collected.declaration,
        description: pendingComment.trim()
      })
    }

    pendingComment = ''
    index = collected.nextIndex
  }

  return symbols
}

function renderSymbolSection (title, symbols, location) {
  if (!symbols.length) return ''

  let output = `.SH ${title}\n`

  symbols.forEach((symbol) => {
    output += '.TP\n'
    output += `\\fB${escapeRoffText(symbol.name)}\\fR\n`
    output += '.nf\n'
    output += `${escapeRoffText(formatDeclarationSnippet(symbol))}\n`
    output += '.fi\n'
    output += renderParagraphs(
      symbol.description || fallbackDescription(symbol, location)
    )
  })

  return output
}

export function generateCApiManpage ({ source, location }) {
  const manifest = HEADER_MANIFEST[location]
  if (!manifest) {
    throw new Error(`Unsupported C API header: ${location}`)
  }

  const symbols = extractHeaderSymbols(source, location)
  const macros = symbols.filter((symbol) => symbol.kind === 'macro')
  const types = symbols.filter((symbol) => symbol.kind === 'type')
  const functions = symbols.filter((symbol) => symbol.kind === 'function')

  let man = `.TH ${manifest.pageName.toUpperCase()} 3 "" "Oro Runtime" "Oro Runtime API Manual"\n`
  man += '.SH NAME\n'
  man += `${escapeRoffText(manifest.pageName)} \\- ${escapeRoffText(manifest.summary)}\n`
  man += '.SH SYNOPSIS\n'
  man += '.nf\n'
  man += `${escapeRoffText(`#include <${stripIncludePrefix(location)}>`)}\n`
  man += '.fi\n'
  man += '.SH DESCRIPTION\n'
  man += renderParagraphs(manifest.description.join('\n\n'))
  man += renderSymbolSection('MACROS', macros, location)
  man += renderSymbolSection('TYPES', types, location)
  man += renderSymbolSection('FUNCTIONS', functions, location)
  man += '.SH HEADER\n'
  man += `${escapeRoffText(location)}\n`

  return {
    filename: `${manifest.pageName}.3`,
    content: man
  }
}

export const PUBLIC_C_API_HEADERS = Object.keys(HEADER_MANIFEST).sort()
