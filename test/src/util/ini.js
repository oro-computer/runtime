// Shared INI parsing helpers for tests. Mirrors runtime/native behaviour.

function isWhitespace (char) {
  return /\s/.test(char)
}

function isHexDigit (char) {
  return /[0-9a-fA-F]/.test(char)
}

function hexValue (char) {
  if (!isHexDigit(char)) return 0
  if (char >= '0' && char <= '9') return char.charCodeAt(0) - 48
  return 10 + (char.toLowerCase().charCodeAt(0) - 97)
}

function stripBom (value) {
  if (value.startsWith('\ufeff')) {
    return value.slice(1)
  }

  // UTF-16 BOMs
  if (value.length >= 2) {
    const first = value.charCodeAt(0)
    const second = value.charCodeAt(1)
    if (
      (first === 0xfe && second === 0xff) ||
      (first === 0xff && second === 0xfe)
    ) {
      return value.slice(2)
    }
  }

  return value
}

function ltrim (value) {
  let index = 0
  while (index < value.length && isWhitespace(value[index])) {
    index += 1
  }
  return value.slice(index)
}

function rtrim (value) {
  let index = value.length - 1
  while (index >= 0 && isWhitespace(value[index])) {
    index -= 1
  }
  return value.slice(0, index + 1)
}

function isEscaped (value, index) {
  if (index <= 0 || index >= value.length) return false

  let backslashCount = 0
  let cursor = index - 1

  while (cursor >= 0 && value[cursor] === '\\') {
    backslashCount += 1
    cursor -= 1
  }

  return backslashCount % 2 === 1
}

function hasTerminatingQuote (value, quote) {
  for (let i = 1; i < value.length; i++) {
    if (value[i] === quote && !isEscaped(value, i)) {
      return true
    }
  }
  return false
}

function findKeyValueSeparator (line) {
  let insideQuotes = false
  let quote = null

  for (let i = 0; i < line.length; i++) {
    const char = line[i]

    if ((char === '"' || char === "'") && !isEscaped(line, i)) {
      if (insideQuotes && char === quote) {
        insideQuotes = false
        quote = null
      } else if (!insideQuotes) {
        insideQuotes = true
        quote = char
      }
      continue
    }

    if (!insideQuotes && (char === '=' || char === ':')) {
      return i
    }
  }

  return -1
}

function removeInlineComment (source) {
  let insideSingle = false
  let insideDouble = false

  for (let i = 0; i < source.length; i++) {
    const char = source[i]

    if (char === '\\' && (insideSingle || insideDouble)) {
      i += 1
      continue
    }

    if (char === "'" && !insideDouble) {
      insideSingle = !insideSingle
      continue
    }

    if (char === '"' && !insideSingle) {
      insideDouble = !insideDouble
      continue
    }

    if (!insideSingle && !insideDouble && (char === ';' || char === '#')) {
      return rtrim(source.slice(0, i))
    }
  }

  return rtrim(source)
}

function decodeSingleQuotedValue (body) {
  let result = ''
  let escape = false

  for (let i = 0; i < body.length; i++) {
    const char = body[i]

    if (escape) {
      escape = false
      if (char === '\\' || char === "'") {
        result += char
      } else {
        result += '\\' + char
      }
      continue
    }

    if (char === '\\') {
      escape = true
      continue
    }

    result += char
  }

  if (escape) {
    result += '\\'
  }

  return result
}

function decodeDoubleQuotedValue (body) {
  let result = ''
  let escape = false

  for (let i = 0; i < body.length; i++) {
    const char = body[i]

    if (escape) {
      escape = false
      switch (char) {
        case '\\':
          result += '\\'
          break
        case '"':
          result += '"'
          break
        case 'n':
          result += '\n'
          break
        case 'r':
          result += '\r'
          break
        case 't':
          result += '\t'
          break
        case '0':
          result += '\0'
          break
        case 'x': {
          if (
            i + 2 < body.length &&
            isHexDigit(body[i + 1]) &&
            isHexDigit(body[i + 2])
          ) {
            const upper = hexValue(body[i + 1])
            const lower = hexValue(body[i + 2])
            result += String.fromCharCode((upper << 4) | lower)
            i += 2
          } else {
            result += 'x'
          }
          break
        }
        default:
          result += char
      }
      continue
    }

    if (char === '\\') {
      escape = true
      continue
    }

    result += char
  }

  if (escape) {
    result += '\\'
  }

  return result
}

function decodeQuotedValue (value) {
  if (!value) return value
  const quote = value[0]
  const body = value.slice(1, -1)
  return quote === "'"
    ? decodeSingleQuotedValue(body)
    : decodeDoubleQuotedValue(body)
}

class Value {
  constructor () {
    this.type = 'scalar'
    this.scalar = ''
    this.list = []
  }

  isScalar () {
    return this.type === 'scalar'
  }

  asString () {
    if (!this.isScalar()) throw new Error('INI value is not scalar')
    return this.scalar
  }

  asList () {
    if (this.isScalar()) throw new Error('INI value is not a list')
    return this.list
  }

  setScalar (value) {
    this.type = 'scalar'
    this.scalar = value
    this.list = []
  }

  setList (values) {
    this.type = 'list'
    this.list = values.slice()
    this.scalar = ''
  }

  append (value) {
    if (this.isScalar()) {
      this.setList([this.scalar])
    }
    this.list.push(value)
  }
}

class Section {
  constructor (name = '') {
    this.name = name
    this.values = new Map()
    this.order = []
    this.explicitLists = new Set()
  }

  has (key) {
    return this.values.has(key)
  }

  set (key, value, append, forceList = false) {
    let entry = this.values.get(key)

    if (!entry) {
      entry = new Value()
      if (append || forceList) {
        entry.setList([value])
      } else {
        entry.setScalar(value)
      }
      this.values.set(key, entry)
      this.order.push(key)
      if (forceList) {
        this.explicitLists.add(key)
      }
      return entry
    }

    if (!append) {
      if (forceList) {
        entry.setList([value])
        this.explicitLists.add(key)
      } else {
        entry.setScalar(value)
        this.explicitLists.delete(key)
      }
      return entry
    }

    if (forceList) {
      this.explicitLists.add(key)
    }

    entry.append(value)
    return entry
  }

  isExplicitList (key) {
    return this.explicitLists.has(key)
  }
}

class Document {
  constructor () {
    this.sections = new Map()
    this.sectionOrder = []
    this.section('')
  }

  section (name) {
    if (this.sections.has(name)) {
      return this.sections.get(name)
    }

    const section = new Section(name)
    this.sections.set(name, section)
    this.sectionOrder.push(name)
    return section
  }

  flatten (separator = '_') {
    const flattened = {}

    for (const sectionName of this.sectionOrder) {
      const section = this.sections.get(sectionName)
      const prefix = sectionName
        ? sectionName.replace(/\./g, separator) + separator
        : ''

      for (const key of section.order) {
        const entry = section.values.get(key)
        const composedKey = prefix ? prefix + key : key

        if (entry.type === 'list') {
          const useNewlines = key.endsWith('_headers')
          const joined = useNewlines
            ? entry.list.join('\n')
            : entry.list.join(' ')
          flattened[composedKey] = joined
        } else {
          flattened[composedKey] = entry.scalar
        }
      }
    }

    return flattened
  }
}

function parseDocument (source) {
  const document = new Document()
  const lines = source.split(/\n/)

  let currentSection = document.section('')
  let currentSectionName = ''

  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    let rawLine = lines[lineIndex]

    if (lineIndex === 0) {
      rawLine = stripBom(rawLine)
    }

    if (rawLine.endsWith('\r')) {
      rawLine = rawLine.slice(0, -1)
    }

    const trimmed = rawLine.trim()
    if (!trimmed) {
      continue
    }

    const lead = trimmed[0]

    if (lead === ';' || lead === '#') {
      continue
    }

    if (lead === '[') {
      const opening = rawLine.indexOf('[')
      let closing = -1
      for (let i = opening + 1; i < rawLine.length; i++) {
        if (rawLine[i] === ']' && !isEscaped(rawLine, i)) {
          closing = i
          break
        }
      }

      if (closing === -1) {
        throw new Error('Unterminated section header')
      }

      let headerContent = rawLine.slice(opening + 1, closing)
      headerContent = removeInlineComment(headerContent)
      const cleaned = headerContent.trim()
      const relative = cleaned.startsWith('.')
      let decoded = cleaned.replace(/\\([;#[\]=:'"\\])/g, '$1')

      if (relative) {
        decoded = decoded.slice(1)
        if (!currentSectionName) {
          throw new Error('Relative section declared without an active section')
        }
        if (decoded) {
          currentSectionName = `${currentSectionName}.${decoded}`
        }
      } else {
        currentSectionName = decoded
      }

      currentSection = document.section(currentSectionName)
      continue
    }

    const separatorIndex = findKeyValueSeparator(rawLine)
    let key
    let valuePortion

    if (separatorIndex === -1) {
      key = rawLine.trim()
      valuePortion = ''
    } else {
      key = rawLine.slice(0, separatorIndex).trim()
      valuePortion = rawLine.slice(separatorIndex + 1)
    }

    if (!key) {
      throw new Error('Missing key in assignment')
    }

    let forceList = false
    if (key.endsWith('[]')) {
      forceList = true
      key = key.slice(0, -2).trim()
    }

    let valueSource = ltrim(valuePortion)
    let quoted = false
    let quote = null

    if (valueSource.startsWith('"') || valueSource.startsWith("'")) {
      quoted = true
      quote = valueSource[0]
      while (!hasTerminatingQuote(valueSource, quote)) {
        lineIndex += 1
        if (lineIndex >= lines.length) {
          throw new Error('Unterminated quoted value')
        }
        let continuation = lines[lineIndex]
        if (continuation.endsWith('\r')) {
          continuation = continuation.slice(0, -1)
        }
        valueSource += '\n' + continuation
      }
    }

    valueSource = removeInlineComment(valueSource)

    let value
    if (quoted) {
      if (
        valueSource.length < 2 ||
        valueSource[0] !== quote ||
        valueSource[valueSource.length - 1] !== quote ||
        isEscaped(valueSource, valueSource.length - 1)
      ) {
        throw new Error('Invalid quoted value')
      }
      value = decodeQuotedValue(valueSource)
    } else {
      value = valueSource.trim()
    }

    if (forceList || currentSection.has(key)) {
      currentSection.set(key, value, true, forceList)
    } else {
      currentSection.set(key, value, false, forceList)
    }
  }

  return document
}

function flattenIni (source, separator = '_') {
  return parseDocument(source).flatten(separator)
}

export { parseDocument, flattenIni }
