import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { generateCApiManpage } from '../../bin/docs-generator/c-api.js'
import { generateCli, generateCliManpages } from '../../bin/docs-generator/cli.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')

async function readSource (relativePath) {
  return readFile(path.join(repoRoot, relativePath), 'utf8')
}

test('CLI manuals merge detailed options without losing flags, paragraphs, or examples', () => {
  const source = `
constexpr auto gHelpTextUpdateSign = R"TEXT(
Sign a manifest.
Usage:
  oroc update sign (--keys=<file> | --private-key=<hex>) [options]
Options:
  --keys=<file>                        signing keys
  --out=<path>                         output path
Documentation:
  man oroc-update-sign
)TEXT";
constexpr auto gManualTextUpdateSign = R"TEXT(
Options:
  --keys=<file>                        JSON containing a private key
                                      and an optional key ID
Notes:
  Keep the key secret.

  Distribute only public keys.
Examples:
  oroc update sign --keys keys.json
                                      sign with a JSON key file
)TEXT";`
  const [page] = generateCliManpages(source, { cliVersion: '1.0.0' })
  assert.equal(page.filename, 'oroc-update-sign.1')
  assert.match(page.content, /JSON containing a private key and an optional key ID/)
  assert.match(page.content, /output path/)
  assert.match(page.content, /Keep the key secret\.\n\.PP\nDistribute only public keys\./)
  assert.match(page.content, /\.SH EXAMPLES/)
  assert.doesNotMatch(page.content, /\.SH DOCUMENTATION/)
  const markdown = generateCli(source)
  assert.match(markdown, /## oroc update sign\n/)
  assert.match(markdown, /```bash\noroc update sign --keys keys.json\n```/)
})

test('CLI manuals cover nested commands and all platform dependency sections', async () => {
  const source = (await readSource('src/cli/templates.hh'))
    .replaceAll('{{cli_name}}', 'oroc')
    .replaceAll('{{cli_version}}', '1.0.0')
  const pages = generateCliManpages(source, { cliVersion: '1.0.0' })
  const byName = new Map(pages.map(page => [page.filename, page.content]))
  assert.equal(byName.size, pages.length)
  for (const match of source.matchAll(/Documentation:\n {2}man (oroc[\w-]*)/g)) {
    assert.ok(byName.has(`${match[1]}.1`), `Missing manual for ${match[1]}`)
  }
  const build = byName.get('oroc-build.1')
  for (const platform of ['LINUX', 'MACOS', 'WINDOWS']) {
    assert.ok(build.includes(`.SH ${platform} DEPENDENCIES\n`))
  }
  assert.match(build, /dpkg/)
  assert.match(build, /Xcode/)
  assert.match(build, /Visual Studio/)
  assert.ok(byName.has('oroc-tooling.1'))
  assert.ok(byName.has('oroc-tooling-kotlin.1'))
  assert.ok(byName.has('oroc-update-verify.1'))
})

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
      headerLocation: 'include/oro/platform.h',
      apiFilename: 'oro-platform.3',
      cApiFilename: 'oro-platform-c.3'
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
