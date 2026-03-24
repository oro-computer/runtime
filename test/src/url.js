import test from 'oro:test'
import url from 'oro:url'

test('url.resolve(url, base)', (t) => {
  t.equal(
    url.resolve('/a/b/c', 'oro://com.app/index.html'),
    'oro://com.app/index.html',
    '/a/b/c ~ oro://com.app/index.html -> oro://com.app/index.html'
  )

  t.equal(
    url.resolve('oro:///a/b/c', '..'),
    'oro:///a/',
    'oro:///a/b/c ~ .. -> oro:///a/'
  )

  t.equal(url.resolve('/a/b/c', '..'), '/a/', '/a/b/c ~ .. -> /a/')
  t.equal(url.resolve('/a/b/c', '.'), '/a/b/', '/a/b/c ~ . -> /a/b/')
  t.equal(url.resolve('/a/b/c', '/'), '/', '/a/b/c ~ / -> /')
})

test('url.parse with auth, port, path, query, hash', (t) => {
  const u = url.parse('https://user:pass@example.com:8080/a/b?x=1#h')
  t.equal(u.protocol, 'https:', 'protocol')
  t.equal(u.host, 'example.com:8080', 'host includes port')
  t.equal(u.hostname, 'example.com', 'hostname')
  t.equal(u.port, '8080', 'port')
  t.equal(u.username, 'user', 'username')
  t.equal(u.password, 'pass', 'password')
  t.equal(u.pathname, '/a/b', 'pathname')
  t.equal(u.search, '?x=1', 'search')
  t.equal(u.hash, '#h', 'hash')
  t.equal(u.path, '/a/b', 'path alias')
  t.equal(u.origin, 'https://example.com:8080', 'origin includes port')
  t.equal(u.href, 'https://user:pass@example.com:8080/a/b?x=1#h', 'href')
})

test('url.parse options true for query object', (t) => {
  const u = url.parse('https://example.com/?x=1&y=2', true)
  t.deepEqual(u.query, { x: '1', y: '2' }, 'query object parsed')
})

test('url.parse strict invalid', (t) => {
  t.equal(
    url.parse('not a url', { strict: true }),
    null,
    'strict invalid returns null'
  )
})

test('url.format from object with port and auth', (t) => {
  const s = url.format({
    protocol: 'https:',
    hostname: 'example.com',
    port: '8080',
    username: 'user',
    password: 'pass',
    pathname: '/a/b',
    query: { x: 1 },
    hash: 'h'
  })
  t.equal(
    s,
    'https://user:pass@example.com:8080/a/b?x=1#h',
    'formatted with auth + port + query + hash'
  )
})

test('url.format supports host directly', (t) => {
  const s = url.format({
    protocol: 'https:',
    host: 'example.com:8080',
    pathname: '/a'
  })
  t.equal(s, 'https://example.com:8080/a', 'formatted with host')
})

test('url.parse/format file UNC host', (t) => {
  const u = url.parse('file://server/share/path')
  t.equal(u.hostname, 'server', 'file host preserved')
  const s = url.format({
    protocol: 'file:',
    hostname: 'server',
    pathname: '/share/path'
  })
  t.equal(s, 'file://server/share/path', 'file UNC formatted')
})

test('url.format brackets IPv6 hostname', (t) => {
  const s = url.format({
    protocol: 'https:',
    hostname: '2001:db8::1',
    port: '8080',
    pathname: '/a'
  })
  t.equal(s, 'https://[2001:db8::1]:8080/a', 'IPv6 host bracketed with port')
})

test('url.parse IPv6 host', (t) => {
  const u = url.parse('https://[2001:db8::1]:8080/a')
  t.equal(u.host, '[2001:db8::1]:8080', 'host includes brackets and port')
  t.equal(u.hostname, '2001:db8::1', 'hostname without brackets')
})

test('url.parse encodes/decodes auth correctly', (t) => {
  const s = url.format({
    protocol: 'https:',
    hostname: 'example.com',
    username: 'us er',
    password: 'pa:ss@',
    pathname: '/'
  })
  t.equal(s, 'https://us%20er:pa%3Ass%40@example.com/', 'auth encoded')
  const u = url.parse(s)
  t.equal(u.username, 'us er', 'username decoded')
  t.equal(u.password, 'pa:ss@', 'password decoded')
})

test('url.parse data: URL', (t) => {
  const u = url.parse('data:text/plain,hello')
  t.equal(u.protocol, 'data:', 'protocol')
  t.equal(u.origin, 'null', 'non-special origin is null string')
  t.equal(u.pathname, 'text/plain,hello', 'pathname contains payload')
})

test('url.parse mailto:', (t) => {
  const u = url.parse('mailto:user@example.com')
  t.equal(u.protocol, 'mailto:', 'protocol')
  t.equal(u.hostname, null, 'no hostname')
  t.equal(u.pathname, 'user@example.com', 'address in pathname')
})

test('url.parse ws/wss', (t) => {
  let u = url.parse('ws://example.com/chat')
  t.equal(u.protocol, 'ws:', 'ws protocol')
  t.equal(u.hostname, 'example.com', 'hostname')
  t.equal(u.pathname, '/chat', 'pathname')
  u = url.parse('wss://example.com/chat')
  t.equal(u.protocol, 'wss:', 'wss protocol')
})

test('url.format file with empty host', (t) => {
  const s = url.format({ protocol: 'file:', pathname: '/path/to/file' })
  t.equal(
    s,
    'file:///path/to/file',
    'file with empty host formats with triple slash'
  )
})

test('url.parse/format preserve percent-encoded pathname', (t) => {
  const u = url.parse('https://example.com/a%2Fb%20c')
  t.equal(u.pathname, '/a%2Fb%20c', 'encoded path preserved in parse')
  const s = url.format({
    protocol: 'https:',
    hostname: 'example.com',
    pathname: '/a%2Fb%20c'
  })
  t.equal(
    s,
    'https://example.com/a%2Fb%20c',
    'encoded path preserved in format'
  )
})

test('url.format IPv6 with zone identifier', (t) => {
  const s = url.format({
    protocol: 'https:',
    hostname: 'fe80::1%25en0',
    pathname: '/a'
  })
  t.equal(s, 'https://[fe80::1%25en0]/a', 'IPv6 with zone id formatted')
  const u = url.parse(s)
  t.equal(u.hostname, 'fe80::1%25en0', 'zone id preserved in hostname')
})
