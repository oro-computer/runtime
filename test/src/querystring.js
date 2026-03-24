import test from 'oro:test'
import qs from 'oro:querystring'

test('querystring.stringify basic', (t) => {
  t.equal(qs.stringify({ a: 1, b: 'x y' }), 'a=1&b=x%20y', 'encodes spaces')
  t.equal(qs.stringify({ a: ['1', '2'] }), 'a=1&a=2', 'array values repeated')
})

test('querystring.parse basic', (t) => {
  t.deepEqual(
    qs.parse('a=1&b=x%20y'),
    { a: '1', b: 'x y' },
    'decodes %20 to space'
  )
  t.deepEqual(
    qs.parse('a=1&a=2'),
    { a: ['1', '2'] },
    'duplicates collected as array'
  )
})

test('querystring.parse plus to space', (t) => {
  t.deepEqual(
    qs.parse('a+b=c+d'),
    { 'a b': 'c d' },
    '+ treated as space in key and value'
  )
})

test('querystring.parse maxKeys', (t) => {
  t.deepEqual(
    qs.parse('a=1&b=2', '&', '=', { maxKeys: 1 }),
    { a: '1' },
    'respects maxKeys'
  )
})

test('querystring.stringify custom encoder', (t) => {
  const enc = (s) => `X_${encodeURIComponent(s)}`
  t.equal(
    qs.stringify({ a: 'b' }, '&', '=', { encodeURIComponent: enc }),
    'a=X_b',
    'uses custom encoder'
  )
})

test('querystring.parse tolerates bad encoding', (t) => {
  const out = qs.parse('a=%E0%A4%A')
  t.equal(typeof out.a, 'string', 'did not throw for bad encoding')
})
