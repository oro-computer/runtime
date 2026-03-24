const test = require('oro:test')

test('commonjs - globals', (t) => {
  t.ok(process === require('oro:process'), 'process is global')
  t.ok(typeof global === 'object', 'global is global')
  t.ok(typeof module === 'object', 'module is global')
  t.ok(
    typeof exports === 'object' && exports === module.exports,
    'exports is global'
  )
  t.ok(typeof __filename === 'string', '__filename is global')
  t.ok(typeof __dirname === 'string', '__filename is global')
})
