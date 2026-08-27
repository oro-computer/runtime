import test from 'oro:test'

test('test: assertion aliases are available', async (t) => {
  t.same({ nested: ['value'] }, { nested: ['value'] }, 'same deeply compares')
  t.notOk(0, 'notOk accepts a falsy value')
  t.match('oro-runtime', /^oro-/, 'match accepts a regular expression')
  await t.rejects(
    async () => {
      throw new Error('expected rejection')
    },
    /expected rejection/,
    'rejects accepts an async function'
  )
})
