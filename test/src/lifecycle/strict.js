import test from 'oro:test'

test('strict lifecycle configuration reaches the document', t => {
  t.equal(process.env.ORO_LIFECYCLE_TEST_STRICT, '1', 'strict environment is explicitly exposed')
})

await import('./desktop-config-net.js')
await import('./desktop-config-pause.js')
