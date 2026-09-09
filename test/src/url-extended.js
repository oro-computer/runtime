import test from 'oro:test'
import url from 'oro:url'

// Additional URL parsing/formatting/resolution coverage.

// Resolve relative paths against path-only bases
const resolvePairs = [
  ['/a/b/c', '..', '/a/'],
  ['/a/b/c', '../..', '/'],
  ['/a/b/c', '../../d', '/d'],
  ['/a/b/c', './d', '/a/b/d'],
  ['/a/b/c/', 'd', '/a/b/c/d'],
  ['/a', 'b', '/b'],
  ['/', 'a', '/a'],
  ['/', '.', '/'],
  ['/', '..', '/']
]
for (let i = 0; i < resolvePairs.length; i++) {
  const [base, rel, expect] = resolvePairs[i]
  test(`url.resolve path base ${i + 1}`, (t) => {
    t.equal(url.resolve(base, rel), expect)
  })
}
// Resolve against absolute URL bases
const resolveUrlPairs = [
  ['https://example.com/a/b', 'c', 'https://example.com/a/c'],
  ['https://example.com/a/b', '../c', 'https://example.com/c'],
  ['https://example.com/a/b/', '../c', 'https://example.com/a/c'],
  ['https://example.com/a/b/', './c', 'https://example.com/a/b/c'],
  ['https://example.com/a/b', '/c', 'https://example.com/c'],
  ['file:///a/b', 'c', 'file:///a/c'],
  ['oro:///app/index.html', '..', 'oro:///']
]
for (let i = 0; i < resolveUrlPairs.length; i++) {
  const [base, rel, expect] = resolveUrlPairs[i]
  test(`url.resolve URL base ${i + 1}`, (t) => {
    t.equal(url.resolve(base, rel), expect)
  })
}

// Object formatting preserves explicit ports; parsing normalizes default ports.
const defaultPorts = [
  ['http:', '80'],
  ['https:', '443'],
  ['ws:', '80'],
  ['wss:', '443']
]
for (let i = 0; i < defaultPorts.length; i++) {
  const [protocol, port] = defaultPorts[i]
  const formatted = url.format({
    protocol,
    hostname: 'example.com',
    port,
    pathname: '/'
  })
  test(`url.format preserves explicit default port ${protocol}`, (t) => {
    t.equal(formatted, `${protocol}//example.com:${port}/`)
  })
  const parsed = url.parse(`${protocol}//example.com:${port}/`)
  test(`url.parse normalizes default port in host ${protocol}`, (t) => {
    t.equal(parsed.host, 'example.com')
  })
}

// Query and hash round-trips
const qh = [
  'https://example.com/path?x=1#h',
  'https://example.com/?a=1&b=2#hash',
  'http://example.com/#',
  'http://example.com/?'
]
for (let i = 0; i < qh.length; i++) {
  const href = qh[i]
  test(`url.parse preserves search/hash ${i + 1}`, (t) => {
    const u = url.parse(href)
    const rebuilt = url.format(u)
    t.equal(rebuilt, href)
  })
}

// Origins
const originPairs = [
  ['https://example.com/', 'https://example.com'],
  ['http://example.com:80/', 'http://example.com'],
  ['https://example.com:443/', 'https://example.com'],
  ['file:///a/b', 'file://'],
  ['data:text/plain,hello', 'null'],
  ['custom:foo', 'null'],
  ['oro:///app/index.html', 'oro://']
]

for (let i = 0; i < originPairs.length; i++) {
  const [href, expect] = originPairs[i]
  test(`url.origin ${i + 1}`, (t) => {
    const u = url.parse(href)
    t.equal(u.origin, expect)
  })
}

// Auth edge cases
const authPairs = [
  [
    {
      protocol: 'https:',
      hostname: 'e.com',
      username: 'u',
      password: '',
      pathname: '/'
    },
    'https://u@e.com/'
  ],
  [
    {
      protocol: 'https:',
      hostname: 'e.com',
      username: '',
      password: 'p',
      pathname: '/'
    },
    'https://:p@e.com/'
  ],
  [
    {
      protocol: 'https:',
      hostname: 'e.com',
      username: 'u s',
      password: 'p@:',
      pathname: '/'
    },
    'https://u%20s:p%40%3A@e.com/'
  ]
]
for (let i = 0; i < authPairs.length; i++) {
  const [obj, expect] = authPairs[i]
  test(`url.format auth edge ${i + 1}`, (t) => {
    t.equal(url.format(obj), expect)
  })
}

// IPv6 format/parse symmetry
const ipv6 = [
  'https://[2001:db8::1]/a',
  'https://[fe80::1%25en0]/a',
  'http://[::1]/'
]
for (let i = 0; i < ipv6.length; i++) {
  const href = ipv6[i]
  test(`url.parse IPv6 ${i + 1}`, (t) => {
    const u = url.parse(href)
    t.ok(u.hostname.includes(':'))
  })
  test(`url.format IPv6 ${i + 1}`, (t) => {
    const s = url.format(url.parse(href))
    t.equal(s, href)
  })
}

// Object formatting preserves the supplied pathname.
const filePaths = [
  ['/a/b', 'file:///a/b'],
  ['/a/../b', 'file:///a/../b'],
  ['/a/./b', 'file:///a/./b']
]
for (let i = 0; i < filePaths.length; i++) {
  const [pathname, expect] = filePaths[i]
  test(`url.format file path ${i + 1}`, (t) => {
    t.equal(url.format({ protocol: 'file:', pathname }), expect)
  })
}
