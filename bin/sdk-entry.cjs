const fs = require('node:fs')
const path = require('node:path')
const { pathToFileURL } = require('node:url')
async function run () {
  const generation = process.env.ORO_SDK_GENERATION
  const home = path.dirname(path.dirname(generation))
  const receipt = JSON.parse(fs.readFileSync(path.join(generation, 'generation.json'), 'utf8'))
  if (receipt.schema !== 1) throw new Error('Unsupported installed SDK generation')
  const api = path.join(generation, 'packages', '@oro-computer', 'runtime')
  const sdk = path.join(generation, 'packages', ...receipt.sdkPackage.split('/'))
  process.env.ORO_HOME ||= sdk
  process.env.ORO_HOME_API ||= api
  const legacy = path.join(home, 'configuration', 'sdk.env')
  if (!process.env.ORO_ENV_FILE && !process.env.ORO_CONFIG_HOME && fs.existsSync(legacy)) process.env.ORO_ENV_FILE = legacy
  await (await import(pathToFileURL(path.join(api, 'bin', 'oroc.js')).href)).run()
}
run().catch(error => { console.error(error.message); process.exitCode = 1 })
