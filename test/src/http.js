import test from 'oro:test'
import { ClientRequest, ServerResponse } from 'oro:http'

test('http.ClientRequest URL-derived properties', (t) => {
  const req = new ClientRequest({ url: 'http://example.com:8080/a?x=1' })
  t.equal(req.protocol, 'http:', 'protocol is http:')
  t.equal(req.host, 'example.com:8080', 'host includes port')
  t.equal(req.path, '/a?x=1', 'path includes search')
  t.equal(req.url, '/a?x=1', 'url accessor equals path')
})

test('http.ServerResponse header setters/getters', (t) => {
  const res = new ServerResponse({})
  res.setHeader('content-type', 'text/plain')
  t.equal(
    res.getHeader('Content-Type'),
    'text/plain',
    'getHeader case-insensitive'
  )
  t.equal(res.hasHeader('content-type'), true, 'hasHeader true')
  const headers = res.getHeaders()
  t.equal(
    headers['content-type'],
    'text/plain',
    'getHeaders returns plain object with lowercase keys'
  )
})
