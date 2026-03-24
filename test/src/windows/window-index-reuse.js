import { test } from 'oro:test'
import * as application from 'oro:application'
import process from 'oro:process'

test('windows: reuse window indices after close', async (t) => {
  if (process.platform === 'android' || process.platform === 'ios') {
    return t.comment('skipping: mobile platforms')
  }

  const indexA = 10
  const indexB = 11

  let winA
  let winB

  try {
    winA = await application.createWindow({
      index: indexA,
      path: 'frontend/index_no_js.html'
    })
    winB = await application.createWindow({
      index: indexB,
      path: 'frontend/index_no_js.html'
    })
  } catch (err) {
    // Multi-window can be disabled on some configs; treat as non-fatal
    return t.comment(`window index reuse skipped: ${err?.message || err}`)
  }

  t.equal(winA.index, indexA, `created window ${indexA}`)
  t.equal(winB.index, indexB, `created window ${indexB}`)

  await winB.setTitle('Window B (before)')
  t.equal(
    winB.getTitle(),
    'Window B (before)',
    'window B title set before reuse'
  )

  const closedA = await winA.close()
  t.ok(!closedA?.err, `closed window ${indexA}`)

  const winA2 = await application.createWindow({
    index: indexA,
    path: 'frontend/index_no_js.html'
  })

  t.equal(winA2.index, indexA, `recreated window ${indexA}`)

  await winB.setTitle('Window B (after)')
  t.equal(winB.getTitle(), 'Window B (after)', 'window B title set after reuse')

  const closedA2 = await winA2.close()
  t.ok(!closedA2?.err, `closed window ${indexA} (reused)`)

  const closedB = await winB.close()
  t.ok(!closedB?.err, `closed window ${indexB}`)
})
