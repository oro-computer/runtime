import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { generateApiModuleManpage } from '../../bin/docs-generator/api-module.js'
import { generateCApiManpage } from '../../bin/docs-generator/c-api.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')

async function readSource (relativePath) {
  return readFile(path.join(repoRoot, relativePath), 'utf8')
}

test('JavaScript and C APIs generate distinct section 3 manpages', async () => {
  const cases = [
    {
      apiLocation: 'api/extension.js',
      moduleSpecifier: 'oro:extension',
      headerLocation: 'include/oro/extension.h',
      filenames: ['oro-extension.3', 'oro-extension-c.3']
    },
    {
      apiLocation: 'api/dbus.js',
      moduleSpecifier: 'oro:dbus',
      headerLocation: 'include/oro/dbus.h',
      filenames: ['oro-dbus.3', 'oro-dbus-c.3']
    },
    {
      apiLocation: 'api/iroh.js',
      moduleSpecifier: 'oro:iroh',
      headerLocation: 'include/iroh/oro_iroh.h',
      filenames: ['oro-iroh.3', 'oro-iroh-c.3']
    }
  ]

  for (const testCase of cases) {
    const apiManpage = generateApiModuleManpage({
      src: await readSource(testCase.apiLocation),
      location: testCase.apiLocation,
      moduleSpecifier: testCase.moduleSpecifier
    })
    const cApiManpage = generateCApiManpage({
      source: await readSource(testCase.headerLocation),
      location: testCase.headerLocation
    })

    assert.deepEqual(
      [apiManpage.filename, cApiManpage.filename],
      testCase.filenames
    )
    assert.equal(
      new Set([apiManpage.filename, cApiManpage.filename]).size,
      2
    )
  }
})
