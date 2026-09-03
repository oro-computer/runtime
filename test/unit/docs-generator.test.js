import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { generateCApiManpage } from '../../bin/docs-generator/c-api.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')

async function readSource (relativePath) {
  return readFile(path.join(repoRoot, relativePath), 'utf8')
}

test('JavaScript and C APIs generate distinct section 3 manpages', async () => {
  const cases = [
    {
      headerLocation: 'include/oro/extension.h',
      apiFilename: 'oro-extension.3',
      cApiFilename: 'oro-extension-c.3'
    },
    {
      headerLocation: 'include/oro/dbus.h',
      apiFilename: 'oro-dbus.3',
      cApiFilename: 'oro-dbus-c.3'
    },
    {
      headerLocation: 'include/iroh/oro_iroh.h',
      apiFilename: 'oro-iroh.3',
      cApiFilename: 'oro-iroh-c.3'
    }
  ]

  for (const testCase of cases) {
    const cApiManpage = generateCApiManpage({
      source: await readSource(testCase.headerLocation),
      location: testCase.headerLocation
    })

    assert.equal(cApiManpage.filename, testCase.cApiFilename)
    assert.notEqual(cApiManpage.filename, testCase.apiFilename)
  }
})
