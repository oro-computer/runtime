import test from 'oro:test'
import {
  parse,
  stringify,
  TomlLocalDate,
  TomlLocalTime,
  TomlLocalDateTime
} from 'oro:toml'

test('toml.parse basic scalars', (t) => {
  const doc = `
title = "Example"
enabled = true
count = 42
ratio = 3.1415
`
  const cfg = parse(doc)
  t.equal(cfg.title, 'Example', 'parses strings')
  t.equal(cfg.enabled, true, 'parses booleans')
  t.equal(cfg.count, 42, 'parses integers')
  t.equal(cfg.ratio, 3.1415, 'parses floats')
})

test('toml.parse handles string variants', (t) => {
  const doc = `
basic = "Line 1\\nLine 2"
literal = 'C:\\\\Path\\\\file.txt'
multiline = """
Roses are red\\
  Violets are blue
"""
literal_multiline = '''
Line A
Line B "quoted"
'''
`
  const cfg = parse(doc)
  t.equal(cfg.basic, 'Line 1\nLine 2', 'supports escaped newline')
  t.equal(cfg.literal, 'C:\\Path\\file.txt', 'supports literal strings')
  t.equal(
    cfg.multiline,
    'Roses are redViolets are blue',
    'supports line continuations'
  )
  t.equal(
    cfg.literal_multiline,
    'Line A\nLine B "quoted"\n',
    'supports literal multiline strings'
  )
})

test('toml.parse rejects unterminated strings', (t) => {
  t.throws(
    () => parse('title = "abc'),
    /unterminated basic string/i,
    'detects unterminated basic string'
  )
  t.throws(
    () => parse("path = 'abc"),
    /unterminated literal string/i,
    'detects unterminated literal string'
  )
  t.throws(
    () => parse('note = """abc'),
    /unterminated multiline basic string/i,
    'detects unterminated multiline basic string'
  )
  t.throws(
    () => parse("msg = '''abc"),
    /unterminated multiline literal string/i,
    'detects unterminated multiline literal string'
  )
})

test('toml.parse numbers and special floats', (t) => {
  const doc = `
hex = 0xDEADBEEF
oct = 0o755
bin = 0b1111_0000
nan = nan
inf = +inf
`
  const cfg = parse(doc)
  t.equal(cfg.hex, 0xdeadbeef, 'parses hexadecimal integer')
  t.equal(cfg.oct, 0o755, 'parses octal integer')
  t.equal(cfg.bin, 0b11110000, 'parses binary integer')
  t.ok(Number.isNaN(cfg.nan), 'parses NaN')
  t.equal(cfg.inf, Infinity, 'parses Infinity')
})

test('toml.parse enforces 64-bit integer range', (t) => {
  t.throws(
    () => parse('value = 9223372036854775808'),
    /out of range/i,
    'rejects integers above int64 max'
  )
  t.throws(
    () => parse('value = -9223372036854775809'),
    /out of range/i,
    'rejects integers below int64 min'
  )
  t.throws(
    () => parse('value = 0x8000000000000000'),
    /out of range/i,
    'rejects oversized hexadecimal integers'
  )
  const within = parse('value = 9223372036854775807')
  t.ok(
    typeof within.value === 'number' && Number.isFinite(within.value),
    'allows int64 max value'
  )
})

test('toml.parse rejects leading zero floats', (t) => {
  t.throws(
    () => parse('value = 01.0'),
    /Invalid numeric value/,
    'rejects leading zero before decimal'
  )
  t.throws(
    () => parse('value = 01e2'),
    /Invalid numeric value/,
    'rejects leading zero before exponent'
  )
})

test('toml.parse date and time types', (t) => {
  const doc = `
offset = 1979-05-27T07:32:00Z
local_dt = 1979-05-27T00:32:00
local_date = 1979-05-27
local_time = 07:32:00.123456
`
  const cfg = parse(doc)
  t.ok(cfg.offset instanceof Date, 'offset date-time becomes Date')
  t.equal(
    cfg.offset.toISOString(),
    '1979-05-27T07:32:00.000Z',
    'offset date-time keeps zone'
  )
  t.ok(
    cfg.local_dt instanceof TomlLocalDateTime,
    'local date-time returns TomlLocalDateTime'
  )
  t.ok(cfg.local_dt.date instanceof TomlLocalDate, 'local date captured')
  t.ok(cfg.local_dt.time instanceof TomlLocalTime, 'local time captured')
  t.ok(cfg.local_date instanceof TomlLocalDate, 'local date parsed')
  t.ok(cfg.local_time instanceof TomlLocalTime, 'local time parsed')
  t.equal(
    cfg.local_time.nanosecond,
    123456000,
    'precision preserved for fractional seconds'
  )
})

test('toml.parse rejects invalid offset date-times', (t) => {
  t.throws(
    () => parse('ts = 1979-02-31T07:32:00Z'),
    /invalid offset date-time/i,
    'rejects impossible calendar dates'
  )
  t.throws(
    () => parse('ts = 1979-05-27T07:32:00+24:00'),
    /invalid offset date-time/i,
    'rejects invalid offset hours'
  )
})

test('toml.parse tables and arrays of tables', (t) => {
  const doc = `
[database]
ports = [ 8000, 8001, 8002 ]
connection_max = 5000

[servers.alpha]
ip = "10.0.0.1"

[servers.beta]
ip = "10.0.0.2"

[[products]]
name = "Hammer"
sku = 738594937

[[products]]
name = "Nail"
sku = 284758393
`
  const cfg = parse(doc)
  t.deepEqual(
    cfg.database.ports,
    [8000, 8001, 8002],
    'nested table array parsed'
  )
  t.equal(cfg.servers.alpha.ip, '10.0.0.1', 'nested table alpha parsed')
  t.equal(cfg.servers.beta.ip, '10.0.0.2', 'nested table beta parsed')
  t.equal(cfg.products.length, 2, 'array of tables yields array of objects')
  t.equal(cfg.products[0].name, 'Hammer', 'first product name parsed')
  t.equal(cfg.products[1].sku, 284758393, 'second product sku parsed')
})

test('toml.parse dotted keys and inline tables', (t) => {
  const doc = `
user.name = "alice"
user.contact.email = "alice@example.com"
settings = { theme = "dark", window = { width = 800, height = 600 } }
`
  const cfg = parse(doc)
  t.equal(cfg.user.name, 'alice', 'dotted key created nested table')
  t.equal(cfg.user.contact.email, 'alice@example.com', 'deep dotted key works')
  t.equal(cfg.settings.theme, 'dark', 'inline table parsed')
  t.equal(cfg.settings.window.width, 800, 'inline nested table parsed')
})

test('toml.parse enforces array homogeneity', (t) => {
  t.throws(
    () => parse('mixed = [1, "two"]'),
    /homogeneous/i,
    'mixed arrays rejected'
  )
})

test('toml.parse prevents modifying inline tables', (t) => {
  t.throws(
    () => parse('config = { key = 1 }\nconfig.other = 2'),
    /inline table/i,
    'inline table is sealed'
  )
})

test('toml.parse prevents redefining tables via headers', (t) => {
  const doc = `
[app]
name = "one"

[app]
name = "two"
`
  t.throws(
    () => parse(doc),
    /already defined/i,
    'duplicate table header rejected'
  )
})

test('toml.parse prevents redefining implicit tables', (t) => {
  const doc = `
app.window.width = 800

[app.window]
height = 600
`
  t.throws(
    () => parse(doc),
    /already defined/i,
    'implicit table cannot be redefined'
  )
})

test('toml.parse supports reviver', (t) => {
  const doc = 'value = 2'
  const cfg = parse(doc, {
    reviver (key, value) {
      if (key === 'value') return value * 10
      return value
    }
  })
  t.equal(cfg.value, 20, 'reviver transforms values')
})

test('toml.stringify basic object', (t) => {
  const data = {
    title: 'Example',
    enabled: true,
    count: 42,
    ratio: 3.1415,
    tags: ['alpha', 'beta']
  }
  const doc = stringify(data)
  const roundTrip = parse(doc)
  t.deepEqual(roundTrip, data, 'stringify round-trips simple types')
})

test('toml.stringify nested tables and arrays of tables', (t) => {
  const data = {
    database: {
      server: '192.168.1.1',
      ports: [8000, 8001],
      connection_max: 5000
    },
    servers: {
      alpha: { ip: '10.0.0.1' },
      beta: { ip: '10.0.0.2' }
    },
    products: [
      { name: 'Hammer', sku: 738594937 },
      { name: 'Nail', sku: 284758393 }
    ]
  }
  const doc = stringify(data)
  const roundTrip = parse(doc)
  t.deepEqual(roundTrip, data, 'nested tables preserve structure')
  t.match(
    doc,
    /\[\[products\]\]\nname = "Hammer"\n/,
    'array of tables emitted with headers'
  )
})

test('toml.stringify temporal values', (t) => {
  const data = {
    offset: new Date('1979-05-27T07:32:00Z'),
    localDate: new TomlLocalDate(1979, 5, 27),
    localTime: new TomlLocalTime(7, 32, 0, 123000000),
    localDateTime: new TomlLocalDateTime(
      new TomlLocalDate(1979, 5, 27),
      new TomlLocalTime(0, 32, 0, 0)
    )
  }
  const doc = stringify(data)
  const roundTrip = parse(doc)
  t.equal(
    roundTrip.offset.toISOString(),
    data.offset.toISOString(),
    'offset datetime round-trips through Date ISO'
  )
  t.equal(
    roundTrip.localDate.toString(),
    data.localDate.toString(),
    'local date preserved'
  )
  t.equal(
    roundTrip.localTime.toString(),
    data.localTime.toString(),
    'local time preserved'
  )
  t.equal(
    roundTrip.localDateTime.toString(),
    data.localDateTime.toString(),
    'local date-time preserved'
  )
})

test('toml.stringify rejects mixed arrays', (t) => {
  const data = { mixed: [1, 'two'] }
  t.throws(() => stringify(data), /single type/i, 'mixed arrays are rejected')
})
