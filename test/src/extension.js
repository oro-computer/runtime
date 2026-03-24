import process from 'oro:process'

const skip = process.env.ORO_TEST_SKIP_TEST_EXTENSIONS === '1'

if (!skip) {
  await import('./extensions/simple.js')
  await import('./extensions/sqlite3.js')
  await import('./extensions/feature-policies.js')
  // await import('./extensions/wasm.js')
}
