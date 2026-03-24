import test from 'oro:test'
import URL from 'oro:url'

const directoryUrl = URL.resolve(import.meta.url, '../fixtures/directory/')
const directoryIndexUrl = URL.resolve(
  import.meta.url,
  '../fixtures/directory/index.html'
)
const directoryHtmlUrl = URL.resolve(
  import.meta.url,
  '../fixtures/directory.html'
)
const rootIndexUrl = URL.resolve(import.meta.url, '../fixtures/index.html')
const fixturesUrl = URL.resolve(import.meta.url, '../fixtures/')
const dotDirectoryUrl = URL.resolve(
  import.meta.url,
  '../fixtures/directory.css'
)
const autoindexEnabled =
  typeof process !== 'undefined' && process?.env?.ORO_TEST_AUTOINDEX === '1'

if (autoindexEnabled) {
  test('autoindex renders directory listing when enabled', async (t) => {
    const response = await fetch(directoryUrl)
    t.ok(response.ok, 'responds with 2xx status when autoindex is enabled')

    const contentType = response.headers.get('content-type') || ''
    t.ok(contentType.includes('text/html'), 'returns HTML content')

    const body = await response.text()
    t.ok(body.includes('Index of'), 'renders heading for directory listing')
    t.ok(
      body.includes('/fixtures/directory/'),
      'includes the resolved directory path'
    )
    t.ok(body.includes('0.txt'), 'lists regular files in the directory')
  })

  test('autoindex renders listing when requesting missing directory index.html', async (t) => {
    const response = await fetch(directoryIndexUrl)
    t.ok(
      response.ok,
      'responds with 2xx status for missing directory index.html'
    )

    const body = await response.text()
    t.ok(body.includes('Index of'), 'renders directory listing markup')
    t.ok(body.includes('/fixtures/directory/'), 'identifies the directory path')
    t.ok(body.includes('0.txt'), 'lists directory contents')
  })

  test('autoindex renders listing when requesting directory via .html alias', async (t) => {
    const response = await fetch(directoryHtmlUrl)
    t.ok(
      response.ok,
      'responds with 2xx status for missing directory .html file'
    )

    const body = await response.text()
    t.ok(
      body.includes('Index of'),
      'renders directory listing markup for alias request'
    )
    t.ok(
      body.includes('/fixtures/directory/'),
      'identifies the directory path for alias request'
    )
    t.ok(body.includes('0.txt'), 'lists directory contents for alias request')
  })

  test('autoindex renders root listing when root index.html is missing', async (t) => {
    const response = await fetch(rootIndexUrl)
    t.ok(response.ok, 'responds with 2xx status for missing root index.html')

    const body = await response.text()
    t.ok(body.includes('Index of'), 'renders root directory listing')
    t.ok(body.includes('/fixtures/'), 'identifies the fixtures directory path')
    t.ok(body.includes('directory/'), 'lists entries from the root directory')
  })

  test('autoindex renders root listing when requesting fixtures directory without extension', async (t) => {
    const response = await fetch(fixturesUrl)
    t.ok(
      response.ok,
      'responds with 2xx status when requesting fixtures directory'
    )

    const body = await response.text()
    t.ok(
      body.includes('Index of'),
      'renders root directory listing for fixtures'
    )
    t.ok(body.includes('/fixtures/'), 'identifies the fixtures directory path')
  })

  test('autoindex does not render listing when requesting directory with extension in path', async (t) => {
    const response = await fetch(dotDirectoryUrl)
    const body = await response.text()

    t.notOk(
      response.ok,
      'responds with non-2xx status for directory requested via extension'
    )
    t.ok(
      !body.includes('Index of'),
      'does not render directory listing for extension-based path'
    )
  })
} else {
  test('autoindex disabled by default', async (t) => {
    const response = await fetch(directoryUrl)
    const body = await response.text()

    t.notOk(
      response.ok,
      'responds with non-2xx status when autoindex is disabled'
    )
    t.ok(
      !body.includes('Index of'),
      'does not render directory listing HTML when disabled'
    )
  })
}
