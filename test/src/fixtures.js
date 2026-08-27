import process from 'oro:process'
import path from 'oro:path'
import os from 'oro:os'

const fallback = /android/i.test(os.platform())
  ? '/data/local/tmp/oro-test-fixtures'
  : path.join(os.tmpdir(), 'oro-test-fixtures')

const configured = process.env.ORO_TEST_FIXTURES_DIR || fallback
const FIXTURES = configured.endsWith(path.sep)
  ? configured
  : configured + path.sep

export default FIXTURES
