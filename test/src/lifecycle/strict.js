import test from 'oro:test'
import '../internal/database.js'
import './desktop-config-net.js'
import './desktop-config-pause.js'

test('strict lifecycle configuration reaches the document', t => {
  t.equal(process.env.ORO_LIFECYCLE_TEST_STRICT, '1', 'strict environment is explicitly exposed')
})
