/* eslint-disable max-depth */
// @ts-check
/**
 * @module toml
 *
 * A compliant TOML 1.0 parser implemented in pure JavaScript.
 *
 * ```js
 * import { parse } from 'oro:toml'
 * const config = parse(await fs.readFile('config.toml', 'utf8'))
 * ```
 */

const SPACE = 0x20
const TAB = 0x09
const NEWLINE = 0x0a
const CARRIAGE_RETURN = 0x0d

const VALUE_TERMINATORS = new Set([
  SPACE,
  TAB,
  NEWLINE,
  0x2c /* , */,
  0x5d /* ] */,
  0x7d /* } */,
  0x23 /* # */
])
const BARE_KEY_RE = /^[A-Za-z0-9_-]+$/

const OFFSET_DATE_TIME_RE =
  /^(\d{4})-(\d{2})-(\d{2})[Tt\s](\d{2}):(\d{2}):(\d{2})(\.\d{1,9})?(Z|z|[+-]\d{2}:\d{2})$/
const LOCAL_DATE_TIME_RE =
  /^(\d{4})-(\d{2})-(\d{2})[Tt\s](\d{2}):(\d{2}):(\d{2})(\.\d{1,9})?$/
const LOCAL_DATE_RE = /^(\d{4})-(\d{2})-(\d{2})$/
const LOCAL_TIME_RE = /^(\d{2}):(\d{2}):(\d{2})(\.\d{1,9})?$/

const INTEGER_RE = /^[+-]?(?:0|[1-9](?:_?[0-9])*)$/
const INT_PART = '(?:0|[1-9](?:_?[0-9])*)'
const FRACTION_PART = '(?:\\.(?:[0-9](?:_?[0-9])*))'
const EXP_PART = '(?:[eE][+-]?[0-9](?:_?[0-9])*)'
const FLOAT_RE = new RegExp(
  `^[+-]?(?:${INT_PART}${FRACTION_PART}${EXP_PART}?|${INT_PART}${EXP_PART})$`
)
const HEX_RE = /^[+-]?0x[0-9A-Fa-f](_?[0-9A-Fa-f])*$/
const OCTAL_RE = /^[+-]?0o[0-7](_?[0-7])*$/
const BINARY_RE = /^[+-]?0b[01](_?[01])*$/
const MAX_INT64 = 9223372036854775807n
const MIN_INT64 = -9223372036854775808n

const INLINE_TABLE_TRAILING_COMMA_MSG =
  'Trailing comma in inline table is not permitted'

const ESCAPE_SEQUENCES = new Map([
  ['b', '\b'],
  ['t', '\t'],
  ['n', '\n'],
  ['f', '\f'],
  ['r', '\r'],
  ['"', '"'],
  ['\\', '\\']
])

/**
 * @typedef {{ declared: boolean, closed: boolean }} TableMeta
 */

const META = new WeakMap()

/**
 * Represents a TOML local date.
 */
export class TomlLocalDate {
  /**
   * @param {number} year
   * @param {number} month
   * @param {number} day
   */
  constructor (year, month, day) {
    validateDateComponents(year, month, day)
    this.year = year
    this.month = month
    this.day = day
    Object.freeze(this)
  }

  toString () {
    return `${pad(this.year, 4)}-${pad(this.month, 2)}-${pad(this.day, 2)}`
  }

  toJSON () {
    return this.toString()
  }
}

/**
 * Represents a TOML local time.
 */
export class TomlLocalTime {
  /**
   * @param {number} hour
   * @param {number} minute
   * @param {number} second
   * @param {number} nanosecond
   */
  constructor (hour, minute, second, nanosecond) {
    validateTimeComponents(hour, minute, second, nanosecond)
    this.hour = hour
    this.minute = minute
    this.second = second
    this.nanosecond = nanosecond
    Object.freeze(this)
  }

  toString () {
    let suffix = ''
    if (this.nanosecond > 0) {
      let fraction = String(this.nanosecond).padStart(9, '0')
      fraction = fraction.replace(/0+$/, '')
      suffix = `.${fraction}`
    }
    return `${pad(this.hour, 2)}:${pad(this.minute, 2)}:${pad(this.second, 2)}${suffix}`
  }

  toJSON () {
    return this.toString()
  }
}

/**
 * Represents a TOML local date-time.
 */
export class TomlLocalDateTime {
  /**
   * @param {TomlLocalDate} date
   * @param {TomlLocalTime} time
   */
  constructor (date, time) {
    if (!(date instanceof TomlLocalDate) || !(time instanceof TomlLocalTime)) {
      throw new TypeError('Invalid local date-time components')
    }
    this.date = date
    this.time = time
    Object.freeze(this)
  }

  toString () {
    return `${this.date.toString()}T${this.time.toString()}`
  }

  toJSON () {
    return this.toString()
  }
}

class Scanner {
  /**
   * @param {string} source
   */
  constructor (source) {
    let text = String(source ?? '')
    if (text.charCodeAt(0) === 0xfeff) {
      text = text.slice(1)
    }
    text = text.replace(/\r\n?/g, '\n')
    this.source = text
    this.length = text.length
    this.position = 0
    this.line = 1
    this.column = 1
  }

  eof () {
    return this.position >= this.length
  }

  peek (offset = 0) {
    const index = this.position + offset
    if (index >= this.length) return ''
    return this.source.charAt(index)
  }

  peekCode (offset = 0) {
    const index = this.position + offset
    if (index >= this.length) return -1
    return this.source.charCodeAt(index)
  }

  next () {
    if (this.eof()) return ''
    const code = this.source.charCodeAt(this.position++)
    if (code === NEWLINE) {
      this.line++
      this.column = 1
    } else {
      this.column++
    }
    return String.fromCharCode(code)
  }

  consume (ch) {
    if (this.peek() !== ch) {
      this.error(`Expected "${ch}" but found "${this.peek() || 'EOF'}"`)
    }
    return this.next()
  }

  location () {
    return { line: this.line, column: this.column }
  }

  error (message) {
    const { line, column } = this.location()
    const err = new SyntaxError(`${message} (line ${line}, column ${column})`)
    throw err
  }
}

function pad (value, length) {
  let str = String(value)
  const sign = str.startsWith('-') ? '-' : ''
  if (sign) str = str.slice(1)
  while (str.length < length) str = `0${str}`
  return `${sign}${str}`
}

function validateDateComponents (year, month, day) {
  if (
    !Number.isInteger(year) ||
    !Number.isInteger(month) ||
    !Number.isInteger(day)
  ) {
    throw new SyntaxError('Invalid local date components')
  }
  if (month < 1 || month > 12) {
    throw new SyntaxError('Month must be between 1 and 12')
  }
  const daysInMonth = new Date(year, month, 0).getDate()
  if (day < 1 || day > daysInMonth) {
    throw new SyntaxError('Day is out of range for month')
  }
}

function validateTimeComponents (hour, minute, second, nanosecond) {
  if (![hour, minute, second, nanosecond].every(Number.isInteger)) {
    throw new SyntaxError('Invalid local time components')
  }
  if (hour < 0 || hour > 23) {
    throw new SyntaxError('Hour must be between 0 and 23')
  }
  if (minute < 0 || minute > 59) {
    throw new SyntaxError('Minute must be between 0 and 59')
  }
  if (second < 0 || second > 60) {
    throw new SyntaxError('Second must be between 0 and 60')
  }
  if (nanosecond < 0 || nanosecond > 999999999) {
    throw new SyntaxError('Nanosecond must be between 0 and 999999999')
  }
}

function isWhitespace (code) {
  return code === SPACE || code === TAB
}

function isBareKeyChar (code) {
  return (
    (code >= 0x30 && code <= 0x39) || // 0-9
    (code >= 0x41 && code <= 0x5a) || // A-Z
    (code >= 0x61 && code <= 0x7a) || // a-z
    code === 0x2d || // -
    code === 0x5f // _
  )
}

function isDigit (code) {
  return code >= 0x30 && code <= 0x39
}

function isControlCharacter (code) {
  return code >= 0x00 && code <= 0x1f && code !== TAB
}

function isPlainObject (value) {
  return (
    value !== null &&
    typeof value === 'object' &&
    !Array.isArray(value) &&
    !(value instanceof Date) &&
    !(value instanceof TomlLocalDate) &&
    !(value instanceof TomlLocalTime) &&
    !(value instanceof TomlLocalDateTime)
  )
}

class Parser {
  /**
   * @param {string} source
   * @param {(key: string, value: any) => any} [reviver]
   */
  constructor (source, reviver) {
    this.scanner = new Scanner(source)
    this.reviver = typeof reviver === 'function' ? reviver : null
    this.root = this.createTable({ declared: true })
    this.currentTable = this.root
    this.currentPath = []
  }

  parse () {
    this.skipBlankLines()
    while (!this.scanner.eof()) {
      const code = this.scanner.peekCode()
      if (code === 0x5b /* [ */) {
        this.parseTableHeader()
      } else if (code === 0x23 /* # */) {
        this.skipComment()
        this.consumeLineEnding()
      } else {
        this.parseKeyValue()
      }
      this.skipBlankLines()
    }

    if (this.reviver) {
      return this.applyReviver({ '': this.root }, '')
    }
    return this.root
  }

  applyReviver (holder, key) {
    const value = holder[key]
    if (Array.isArray(value)) {
      for (let i = 0; i < value.length; i++) {
        value[i] = this.applyReviver(value, String(i))
      }
    } else if (isPlainObject(value)) {
      for (const prop of Object.keys(value)) {
        value[prop] = this.applyReviver(value, prop)
      }
    }
    return this.reviver(key, value)
  }

  createTable (options = {}) {
    const table = {}
    META.set(table, {
      declared: Boolean(options.declared),
      closed: Boolean(options.closed)
    })
    return table
  }

  ensureTableOpen (table, path) {
    const meta = META.get(table)
    if (meta?.closed) {
      this.error(`Cannot modify inline table at "${path.join('.')}"`)
    }
  }

  skipInlineWhitespace () {
    while (isWhitespace(this.scanner.peekCode())) {
      this.scanner.next()
    }
  }

  skipBlankLines () {
    while (true) {
      const startPos = this.scanner.position
      this.skipInlineWhitespace()
      if (this.scanner.peek() === '#') {
        this.skipComment()
      }
      if (this.scanner.peekCode() === NEWLINE) {
        this.scanner.next()
        continue
      }
      if (startPos === this.scanner.position) break
    }
  }

  skipComment () {
    if (this.scanner.peek() !== '#') return
    while (!this.scanner.eof()) {
      const code = this.scanner.peekCode()
      if (code === NEWLINE) break
      this.scanner.next()
    }
  }

  consumeLineEnding () {
    this.skipInlineWhitespace()
    if (this.scanner.peek() === '#') {
      this.skipComment()
    }
    if (!this.scanner.eof()) {
      const code = this.scanner.peekCode()
      if (code === NEWLINE) {
        this.scanner.next()
      } else {
        this.scanner.error('Expected end of line')
      }
    }
  }

  parseTableHeader () {
    this.scanner.consume('[')
    let isArray = false
    if (this.scanner.peek() === '[') {
      isArray = true
      this.scanner.next()
    }
    this.skipInlineWhitespace()
    const path = this.parseDottedKey()
    if (path.length === 0) this.error('Empty table header')
    this.skipInlineWhitespace()
    if (isArray) {
      this.scanner.consume(']')
    }
    this.scanner.consume(']')
    this.skipInlineWhitespace()
    if (this.scanner.peek() === '#') {
      this.skipComment()
    }
    if (!this.scanner.eof()) {
      const code = this.scanner.peekCode()
      if (code === NEWLINE) {
        this.scanner.next()
      } else {
        this.scanner.error('Expected end of table declaration')
      }
    }

    const table = this.enterTable(path, isArray)
    this.currentTable = table
    this.currentPath = path.slice()
  }

  enterTable (path, isArray) {
    let table = this.root
    const fullPath = []

    for (let i = 0; i < path.length; i++) {
      const key = path[i]
      fullPath.push(key)
      const isLast = i === path.length - 1

      this.ensureTableOpen(table, fullPath.slice(0, -1))

      if (!Object.prototype.hasOwnProperty.call(table, key)) {
        if (isLast && isArray) {
          const element = this.createTable({ declared: true })
          table[key] = [element]
          table = element
        } else {
          const child = this.createTable({ declared: isLast })
          table[key] = child
          table = child
        }
        continue
      }

      const existing = table[key]

      if (isLast && !isArray) {
        this.error(`Table "${fullPath.join('.')}" is already defined`)
      }

      if (isLast && isArray) {
        if (!Array.isArray(existing)) {
          this.error(
            `Cannot redefine table "${fullPath.join('.')}" as array-of-tables`
          )
        }
        const element = this.createTable({ declared: true })
        existing.push(element)
        table = element
        continue
      }

      if (Array.isArray(existing)) {
        if (existing.length === 0) {
          this.error(
            `Array for "${fullPath.join('.')}" has no tables to inherit`
          )
        }
        const element = existing[existing.length - 1]
        if (!isPlainObject(element)) {
          this.error(
            `Array for "${fullPath.join('.')}" does not contain tables`
          )
        }
        table = element
      } else if (isPlainObject(existing)) {
        this.ensureTableOpen(existing, fullPath)
        table = existing
      } else {
        this.error(
          `Key "${fullPath.join('.')}" was previously defined and is not a table`
        )
      }
    }

    return table
  }

  parseKeyValue () {
    const keyPath = this.parseDottedKey()
    if (keyPath.length === 0) this.error('Invalid key')
    this.skipInlineWhitespace()
    this.scanner.consume('=')
    this.skipInlineWhitespace()
    const value = this.parseValue()
    this.assignValue(
      this.currentTable,
      this.currentPath.slice(),
      keyPath,
      value
    )
    this.consumeLineEnding()
  }

  parseDottedKey () {
    const parts = []
    while (true) {
      const part = this.parseKeyPart()
      if (!part.length) this.error('Empty key part')
      parts.push(part)
      this.skipInlineWhitespace()
      if (this.scanner.peek() === '.') {
        this.scanner.next()
        this.skipInlineWhitespace()
        continue
      }
      break
    }
    return parts
  }

  parseKeyPart () {
    const code = this.scanner.peekCode()
    if (code === 0x22 /* " */) {
      return this.parseBasicString({ multiline: false, asKey: true })
    }
    if (code === 0x27 /* ' */) {
      return this.parseLiteralString({ multiline: false, asKey: true })
    }
    if (!isBareKeyChar(code)) {
      return ''
    }
    let result = ''
    while (isBareKeyChar(this.scanner.peekCode())) {
      result += this.scanner.next()
    }
    return result
  }

  parseValueWithType () {
    const code = this.scanner.peekCode()
    if (code === 0x22 /* " */) {
      const multiline =
        this.scanner.peek(1) === '"' && this.scanner.peek(2) === '"'
      const value = this.parseBasicString({ multiline })
      return { value, type: 'string' }
    }
    if (code === 0x27 /* ' */) {
      const multiline =
        this.scanner.peek(1) === "'" && this.scanner.peek(2) === "'"
      const value = this.parseLiteralString({ multiline })
      return { value, type: 'string' }
    }
    if (code === 0x5b /* [ */) {
      const value = this.parseArray()
      return { value, type: 'array' }
    }
    if (code === 0x7b /* { */) {
      const value = this.parseInlineTable()
      return { value, type: 'table' }
    }
    if (code === 0x74 /* t */ || code === 0x66 /* f */) {
      return { value: this.parseBoolean(), type: 'boolean' }
    }
    if (code === 0x2b /* + */ || code === 0x2d /* - */ || isDigit(code)) {
      return this.parseNumberOrDateTime()
    }
    this.error(`Unexpected value token "${this.scanner.peek()}"`)
  }

  parseValue () {
    return this.parseValueWithType().value
  }

  parseBoolean () {
    const token = this.readBareToken()
    if (token === 'true') return true
    if (token === 'false') return false
    this.error(`Invalid bare value "${token}"`)
  }

  parseNumberOrDateTime () {
    const token = this.readBareToken()
    if (!token.length) this.error('Expected value')
    const lower = token.toLowerCase()
    if (OFFSET_DATE_TIME_RE.test(token)) {
      return {
        value: this.parseOffsetDateTime(token),
        type: 'offset-date-time'
      }
    }
    if (LOCAL_DATE_TIME_RE.test(token)) {
      return { value: this.parseLocalDateTime(token), type: 'local-date-time' }
    }
    if (LOCAL_DATE_RE.test(token)) {
      return { value: this.parseLocalDate(token), type: 'local-date' }
    }
    if (LOCAL_TIME_RE.test(token)) {
      return { value: this.parseLocalTime(token), type: 'local-time' }
    }
    if (lower === 'inf' || lower === '+inf') {
      return { value: Infinity, type: 'float' }
    }
    if (lower === '-inf') return { value: -Infinity, type: 'float' }
    if (lower === 'nan' || lower === '+nan' || lower === '-nan') {
      return { value: NaN, type: 'float' }
    }
    return this.parseNumber(token)
  }

  parseOffsetDateTime (token) {
    const match = token.match(OFFSET_DATE_TIME_RE)
    if (!match) {
      this.error(`Invalid offset date-time "${token}"`)
    }

    const [
      ,
      yearStr,
      monthStr,
      dayStr,
      hourStr,
      minuteStr,
      secondStr,
      fraction,
      zone
    ] = match
    const year = Number(yearStr)
    const month = Number(monthStr)
    const day = Number(dayStr)
    const hour = Number(hourStr)
    const minute = Number(minuteStr)
    const second = Number(secondStr)
    const nanosecond = fraction ? Number(fraction.slice(1).padEnd(9, '0')) : 0

    try {
      validateDateComponents(year, month, day)
      validateTimeComponents(hour, minute, second, nanosecond)
    } catch {
      this.error(`Invalid offset date-time "${token}"`)
    }

    let offsetMinutes = 0
    const zoneUpper = zone.toUpperCase()
    if (zoneUpper !== 'Z') {
      const offsetSign = zone[0] === '-' ? -1 : 1
      const offsetHour = Number(zone.slice(1, 3))
      const offsetMinute = Number(zone.slice(4, 6))
      if (
        !Number.isInteger(offsetHour) ||
        !Number.isInteger(offsetMinute) ||
        offsetHour < 0 ||
        offsetHour > 23 ||
        offsetMinute < 0 ||
        offsetMinute > 59
      ) {
        this.error(`Invalid offset date-time "${token}"`)
      }
      offsetMinutes = offsetSign * (offsetHour * 60 + offsetMinute)
    }

    const milliseconds = Math.floor(nanosecond / 1e6)
    const utc = Date.UTC(
      year,
      month - 1,
      day,
      hour,
      minute,
      second,
      milliseconds
    )
    const adjusted = utc - offsetMinutes * 60 * 1000
    const date = new Date(adjusted)

    if (Number.isNaN(date.getTime())) {
      this.error(`Invalid offset date-time "${token}"`)
    }

    return date
  }

  parseLocalDateTime (token) {
    const [, year, month, day, hour, minute, second, fraction] =
      LOCAL_DATE_TIME_RE.exec(token)
    const date = new TomlLocalDate(Number(year), Number(month), Number(day))
    const time = this.buildLocalTime(hour, minute, second, fraction)
    return new TomlLocalDateTime(date, time)
  }

  parseLocalDate (token) {
    const [, year, month, day] = LOCAL_DATE_RE.exec(token)
    return new TomlLocalDate(Number(year), Number(month), Number(day))
  }

  parseLocalTime (token) {
    const [, hour, minute, second, fraction] = LOCAL_TIME_RE.exec(token)
    return this.buildLocalTime(hour, minute, second, fraction)
  }

  buildLocalTime (hour, minute, second, fraction) {
    const nanosecond = fraction ? Number(fraction.slice(1).padEnd(9, '0')) : 0
    return new TomlLocalTime(
      Number(hour),
      Number(minute),
      Number(second),
      nanosecond
    )
  }

  parseNumber (token) {
    const cleaned = token.replace(/_/g, '')
    const hasSign = cleaned[0] === '+' || cleaned[0] === '-'
    const isNegative = cleaned[0] === '-'
    const magnitude = hasSign ? cleaned.slice(1) : cleaned
    const magnitudeLower = magnitude.toLowerCase()

    const ensureInRange = (big) => {
      if (big < MIN_INT64 || big > MAX_INT64) {
        this.error('Integer literal out of range')
      }
    }

    if (HEX_RE.test(token)) {
      try {
        const magnitudeValue = BigInt(`0x${magnitudeLower.slice(2)}`)
        const big = isNegative ? -magnitudeValue : magnitudeValue
        ensureInRange(big)
        const numeric = Number(big)
        if (numeric === 0 && isNegative) return { value: -0, type: 'integer' }
        return { value: numeric, type: 'integer' }
      } catch {
        this.error(`Invalid numeric value "${token}"`)
      }
    }
    if (OCTAL_RE.test(token)) {
      try {
        const magnitudeValue = BigInt(`0o${magnitudeLower.slice(2)}`)
        const big = isNegative ? -magnitudeValue : magnitudeValue
        ensureInRange(big)
        const numeric = Number(big)
        if (numeric === 0 && isNegative) return { value: -0, type: 'integer' }
        return { value: numeric, type: 'integer' }
      } catch {
        this.error(`Invalid numeric value "${token}"`)
      }
    }
    if (BINARY_RE.test(token)) {
      try {
        const magnitudeValue = BigInt(`0b${magnitudeLower.slice(2)}`)
        const big = isNegative ? -magnitudeValue : magnitudeValue
        ensureInRange(big)
        const numeric = Number(big)
        if (numeric === 0 && isNegative) return { value: -0, type: 'integer' }
        return { value: numeric, type: 'integer' }
      } catch {
        this.error(`Invalid numeric value "${token}"`)
      }
    }
    if (INTEGER_RE.test(token)) {
      try {
        const big = BigInt(cleaned)
        ensureInRange(big)
        const numeric = Number(big)
        if (numeric === 0 && isNegative) return { value: -0, type: 'integer' }
        return { value: numeric, type: 'integer' }
      } catch {
        this.error(`Invalid numeric value "${token}"`)
      }
    }
    if (FLOAT_RE.test(token)) {
      const isFloat =
        token.includes('.') || token.includes('e') || token.includes('E')
      if (!isFloat) this.error(`Invalid numeric value "${token}"`)
      return { value: Number(cleaned), type: 'float' }
    }
    this.error(`Invalid numeric value "${token}"`)
  }

  readBareToken () {
    let result = ''
    while (!this.scanner.eof()) {
      const code = this.scanner.peekCode()
      if (VALUE_TERMINATORS.has(code)) break
      if (code === CARRIAGE_RETURN) break
      result += this.scanner.next()
    }
    return result
  }

  parseArray () {
    this.scanner.consume('[')
    const values = []
    let resolvedType = null
    while (true) {
      this.skipWhitespaceAndCommentsInArray()
      if (this.scanner.peek() === ']') {
        this.scanner.next()
        break
      }
      const { value, type } = this.parseValueWithType()
      if (resolvedType === null) {
        resolvedType = type
      } else if (type !== resolvedType) {
        this.error('TOML arrays must be homogeneous')
      }
      values.push(value)
      this.skipWhitespaceAndCommentsInArray()
      if (this.scanner.peek() === ',') {
        this.scanner.next()
        continue
      }
      if (this.scanner.peek() === ']') {
        this.scanner.next()
        break
      }
      this.scanner.error('Expected comma or closing bracket in array')
    }
    return values
  }

  skipWhitespaceAndCommentsInArray () {
    while (true) {
      this.skipInlineWhitespace()
      const code = this.scanner.peekCode()
      if (code === NEWLINE) {
        this.scanner.next()
        continue
      }
      if (this.scanner.peek() === '#') {
        this.skipComment()
        continue
      }
      break
    }
  }

  parseInlineTable () {
    this.scanner.consume('{')
    const table = this.createTable()
    this.skipInlineWhitespace()
    if (this.scanner.peek() === '}') {
      this.scanner.next()
      this.closeInlineTable(table)
      return table
    }
    while (true) {
      const keyPath = this.parseDottedKey()
      this.skipInlineWhitespace()
      this.scanner.consume('=')
      this.skipInlineWhitespace()
      const value = this.parseValue()
      this.assignValue(table, [], keyPath, value)
      this.skipInlineWhitespace()
      const nextChar = this.scanner.peek()
      if (nextChar === ',') {
        this.scanner.next()
        this.skipInlineWhitespace()
        if (this.scanner.peek() === '}') {
          this.scanner.error(INLINE_TABLE_TRAILING_COMMA_MSG)
        }
        continue
      }
      if (nextChar === '}') {
        this.scanner.next()
        break
      }
      this.scanner.error('Expected comma or closing brace in inline table')
    }
    this.closeInlineTable(table)
    return table
  }

  closeInlineTable (table) {
    const stack = [table]
    const seen = new Set()
    while (stack.length) {
      const current = stack.pop()
      if (seen.has(current)) continue
      seen.add(current)
      const meta = META.get(current)
      if (meta) meta.closed = true
      for (const value of Object.values(current)) {
        if (isPlainObject(value)) {
          stack.push(value)
        } else if (Array.isArray(value)) {
          for (const element of value) {
            if (isPlainObject(element)) {
              stack.push(element)
            }
          }
        }
      }
    }
  }

  parseBasicString ({ multiline = false, asKey = false } = {}) {
    if (multiline) {
      this.scanner.consume('"')
      this.scanner.consume('"')
      this.scanner.consume('"')
      let buffer = ''
      if (this.scanner.peekCode() === NEWLINE) {
        this.scanner.next()
      }
      while (true) {
        if (this.scanner.eof()) {
          this.scanner.error('Unterminated multiline basic string')
        }
        const ch = this.scanner.next()
        if (ch === '"') {
          let count = 1
          while (this.scanner.peek() === '"') {
            this.scanner.next()
            count++
          }
          if (count >= 3) {
            buffer += '"'.repeat(count - 3)
            return buffer
          }
          buffer += '"'.repeat(count)
          continue
        }
        if (ch === '\\') {
          const next = this.scanner.peek()
          if (next === '\n') {
            this.scanner.next()
            while (true) {
              const code = this.scanner.peekCode()
              if (code === NEWLINE) {
                this.scanner.next()
                continue
              }
              if (!isWhitespace(code)) break
              this.scanner.next()
            }
            continue
          }
          buffer += this.parseEscapeSequence()
          continue
        }
        if (ch === '\r') continue
        if (isControlCharacter(ch.charCodeAt(0))) {
          this.scanner.error('Control character not allowed in string')
        }
        buffer += ch
      }
    }

    this.scanner.consume('"')
    let buffer = ''
    while (true) {
      if (this.scanner.eof()) {
        this.scanner.error('Unterminated basic string')
      }
      const ch = this.scanner.next()
      if (ch === '"') return buffer
      if (ch === '\n') {
        this.scanner.error('Newline not permitted in basic string')
      }
      if (ch === '\\') {
        buffer += this.parseEscapeSequence()
        continue
      }
      if (!asKey && ch === '\r') continue
      if (isControlCharacter(ch.charCodeAt(0))) {
        this.scanner.error('Control character not allowed in string')
      }
      buffer += ch
    }
  }

  parseLiteralString ({ multiline = false, asKey = false } = {}) {
    if (multiline) {
      this.scanner.consume("'")
      this.scanner.consume("'")
      this.scanner.consume("'")
      let buffer = ''
      if (this.scanner.peekCode() === NEWLINE) {
        this.scanner.next()
      }
      while (true) {
        if (this.scanner.eof()) {
          this.scanner.error('Unterminated multiline literal string')
        }
        const ch = this.scanner.next()
        if (ch === "'") {
          let count = 1
          while (this.scanner.peek() === "'") {
            this.scanner.next()
            count++
          }
          if (count >= 3) {
            buffer += "'".repeat(count - 3)
            return buffer
          }
          buffer += "'".repeat(count)
          continue
        }
        buffer += ch
      }
    }

    this.scanner.consume("'")
    let buffer = ''
    while (true) {
      if (this.scanner.eof()) {
        this.scanner.error('Unterminated literal string')
      }
      const ch = this.scanner.next()
      if (ch === "'") return buffer
      if (ch === '\n') {
        this.scanner.error('Newline not permitted in literal string')
      }
      if (!asKey && ch === '\r') continue
      buffer += ch
    }
  }

  parseEscapeSequence () {
    const ch = this.scanner.next()
    if (ESCAPE_SEQUENCES.has(ch)) {
      return ESCAPE_SEQUENCES.get(ch)
    }
    if (ch === 'u' || ch === 'U') {
      const length = ch === 'u' ? 4 : 8
      let hex = ''
      for (let i = 0; i < length; i++) {
        const code = this.scanner.peekCode()
        if (
          !(
            isDigit(code) ||
            (code >= 0x41 && code <= 0x46) ||
            (code >= 0x61 && code <= 0x66)
          )
        ) {
          this.scanner.error('Invalid unicode escape')
        }
        hex += this.scanner.next()
      }
      const codePoint = parseInt(hex, 16)
      return String.fromCodePoint(codePoint)
    }
    this.scanner.error(`Invalid escape sequence \\${ch}`)
  }

  assignValue (table, basePath, keyPath, value) {
    let target = table
    const path = basePath.slice()

    for (let i = 0; i < keyPath.length - 1; i++) {
      const segment = keyPath[i]
      path.push(segment)
      this.ensureTableOpen(target, path.slice(0, -1))
      if (!Object.prototype.hasOwnProperty.call(target, segment)) {
        const child = this.createTable()
        target[segment] = child
        target = child
        continue
      }
      const existing = target[segment]
      if (Array.isArray(existing)) {
        this.error(
          `Cannot assign dotted key through array at "${path.join('.')}"`
        )
      }
      if (!isPlainObject(existing)) {
        this.error(`Key "${path.join('.')}" is not a table`)
      }
      this.ensureTableOpen(existing, path)
      target = existing
    }

    const finalKey = keyPath[keyPath.length - 1]
    path.push(finalKey)
    this.ensureTableOpen(target, path.slice(0, -1))

    if (Object.prototype.hasOwnProperty.call(target, finalKey)) {
      this.error(`Key "${path.join('.')}" is already defined`)
    }

    if (isPlainObject(value) && !META.has(value)) {
      META.set(value, { declared: false, closed: false })
    }

    target[finalKey] = value
  }

  error (message) {
    this.scanner.error(message)
  }
}

/**
 * Parse a TOML document and return a JavaScript representation.
 *
 * @param {string} source
 * @param {{ reviver?: (key: string, value: unknown) => unknown }} [options]
 * @returns {any}
 */
export function parse (source, options = {}) {
  const parser = new Parser(source, options.reviver)
  return parser.parse()
}

function assertPlainTable (value, path) {
  if (!isPlainObject(value)) {
    const target = formatPath(path)
    throw new TypeError(`Expected a plain object for table ${target}`)
  }
}

function formatKey (key) {
  if (typeof key !== 'string') {
    throw new TypeError('TOML keys must be strings')
  }
  if (BARE_KEY_RE.test(key)) {
    return key
  }
  return formatBasicStringLiteral(key)
}

function formatPath (segments) {
  if (!Array.isArray(segments) || segments.length === 0) {
    return '<root>'
  }
  return segments.map(formatKey).join('.')
}

function formatBasicStringLiteral (value) {
  let out = '"'
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i)
    const ch = value.charAt(i)
    switch (ch) {
      case '"':
        out += '\\"'
        break
      case '\\':
        out += '\\\\'
        break
      case '\b':
        out += '\\b'
        break
      case '\f':
        out += '\\f'
        break
      case '\n':
        out += '\\n'
        break
      case '\r':
        out += '\\r'
        break
      case '\t':
        out += '\\t'
        break
      default:
        if (code >= 0x20 && code !== 0x7f) {
          out += ch
        } else {
          out += `\\u${code.toString(16).padStart(4, '0')}`
        }
        break
    }
  }
  out += '"'
  return out
}

function valueTypeForArray (value) {
  if (value instanceof TomlLocalDate) return 'local-date'
  if (value instanceof TomlLocalTime) return 'local-time'
  if (value instanceof TomlLocalDateTime) return 'local-date-time'
  if (value instanceof Date) return 'offset-date-time'
  if (Array.isArray(value)) return 'array'
  if (isPlainObject(value)) return 'table'
  return typeof value
}

function formatPrimitiveValue (value) {
  if (typeof value === 'string') {
    return formatBasicStringLiteral(value)
  }
  if (typeof value === 'number') {
    if (Number.isNaN(value)) return 'nan'
    if (!Number.isFinite(value)) return value < 0 ? '-inf' : 'inf'
    if (Object.is(value, -0)) return '-0.0'
    return String(value)
  }
  if (typeof value === 'boolean') {
    return value ? 'true' : 'false'
  }
  if (value instanceof Date) {
    return value.toISOString()
  }
  if (
    value instanceof TomlLocalDate ||
    value instanceof TomlLocalTime ||
    value instanceof TomlLocalDateTime
  ) {
    return value.toString()
  }
  throw new TypeError(`Unsupported TOML value type: ${String(value)}`)
}

function isArrayOfTablesForStringify (arr) {
  if (!Array.isArray(arr)) return false
  if (arr.length === 0) return false
  return arr.every(isPlainObject)
}

function formatArrayValue (arr) {
  if (arr.length === 0) {
    return '[]'
  }
  const types = arr.map(valueTypeForArray)
  const firstType = types[0]
  for (let i = 1; i < types.length; i++) {
    if (types[i] !== firstType) {
      throw new TypeError('TOML arrays must contain values of a single type')
    }
  }
  const formatted = arr.map((item) => {
    if (isPlainObject(item)) {
      throw new TypeError(
        'Array of tables must be encoded using array-of-table syntax'
      )
    }
    if (Array.isArray(item)) {
      return formatArrayValue(item)
    }
    return formatPrimitiveValue(item)
  })
  return `[${formatted.join(', ')}]`
}

class StringifyContext {
  constructor (options = {}) {
    this.lines = []
    this.options = options
  }

  writeLine (line) {
    this.lines.push(line)
  }

  ensureSeparation () {
    if (this.lines.length > 0 && this.lines[this.lines.length - 1] !== '') {
      this.lines.push('')
    }
  }
}

function writeTable (ctx, path, table) {
  assertPlainTable(table, path)
  const keys = Object.keys(table)
  const simpleKeys = []
  const tableKeys = []

  for (const key of keys) {
    const value = table[key]
    if (value === undefined) {
      throw new TypeError(
        `Key "${formatPath([...path, key])}" cannot be undefined`
      )
    }
    if (typeof value === 'function' || typeof value === 'symbol') {
      throw new TypeError(
        `Key "${formatPath([...path, key])}" cannot be a function or symbol`
      )
    }
    if (isPlainObject(value) || isArrayOfTablesForStringify(value)) {
      tableKeys.push(key)
    } else {
      simpleKeys.push(key)
    }
  }

  for (const key of simpleKeys) {
    const value = table[key]
    let rendered
    if (Array.isArray(value)) {
      if (isArrayOfTablesForStringify(value)) {
        throw new TypeError(
          `Key "${formatPath([...path, key])}" contains array of tables; encode as array-of-tables`
        )
      }
      rendered = formatArrayValue(value)
    } else {
      rendered = formatPrimitiveValue(value)
    }
    ctx.writeLine(`${formatKey(key)} = ${rendered}`)
  }

  for (const key of tableKeys) {
    const value = table[key]
    const childPath = [...path, key]
    if (isArrayOfTablesForStringify(value)) {
      for (const element of value) {
        assertPlainTable(element, childPath)
        ctx.ensureSeparation()
        ctx.writeLine(`[[${childPath.map(formatKey).join('.')}]]`)
        writeTable(ctx, childPath, element)
      }
    } else {
      ctx.ensureSeparation()
      ctx.writeLine(`[${childPath.map(formatKey).join('.')}]`)
      writeTable(ctx, childPath, value)
    }
  }
}

/**
 * Serialize a JavaScript object into a TOML document.
 *
 * @param {Record<string, any>} table
 * @param {{}} [options]
 * @returns {string}
 */
export function stringify (table, options = {}) {
  if (table === null || typeof table !== 'object' || Array.isArray(table)) {
    throw new TypeError('TOML.stringify expects a plain object')
  }
  const ctx = new StringifyContext(options)
  writeTable(ctx, [], table)
  return ctx.lines.join('\n')
}

export default {
  parse,
  stringify,
  TomlLocalDate,
  TomlLocalTime,
  TomlLocalDateTime
}
