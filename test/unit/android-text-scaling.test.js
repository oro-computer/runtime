import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const source = readFileSync(new URL('../../src/runtime/window/window.kt', import.meta.url), 'utf8')
const available = ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('interactive text zoom is absolute before navigation and refreshed by fragment configuration changes', () => {
  const attach = source.slice(source.indexOf('  private fun attachWebView ('), source.indexOf('      // features'))
  assert.ok(attach.includes('this.currentWebView = webview'))
  assert.ok(attach.includes('this.applySystemTextScale(webview.resources.configuration)'))
  assert.ok(attach.indexOf('this.applySystemTextScale(') < attach.indexOf('this.beginStartup()'))
  assert.ok(source.includes('override fun onConfigurationChanged (configuration: Configuration) {\n    super.onConfigurationChanged(configuration)\n    applySystemTextScale(configuration)'))
  assert.ok(source.includes('getUserConfigValue("webview_text_zoom"), configuration.fontScale'))
  const app = readFileSync(new URL('../../src/runtime/app/app.kt', import.meta.url), 'utf8')
  assert.ok(app.includes('.put("systemTextScaling", true)'))
})

test('compiled Android text scale updates each window once and supports fixed/manual ownership', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const start = source.indexOf('internal object SystemTextScaling {')
  const end = source.indexOf('\n/**', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-text-scaling-'))
  const filename = path.join(directory, 'Scaling.kt')
  const jar = path.join(directory, 'scaling.jar')
  writeFileSync(filename, `
    import kotlin.math.roundToInt
    class Settings {
      var writes = 0
      var textZoom = 100
        set(value) { field = value; writes++ }
    }
    class WebView { val settings = Settings() }
    ${source.slice(start, end)}
    fun main() {
      val primary = WebView(); val secondary = WebView(); val worker = WebView()
      for (view in listOf(primary, secondary)) {
        SystemTextScaling.apply(view, false, "", 2f)
        check(view.settings.textZoom == 200 && view.settings.writes == 1)
        // Existing native app workarounds assigning the same absolute zoom do not multiply it.
        view.settings.textZoom = 200
        val previous = view.settings.writes
        repeat(4) { SystemTextScaling.apply(view, false, "system", 2f) }
        check(view.settings.textZoom == 200 && view.settings.writes == previous)
        SystemTextScaling.apply(view, false, "system", 1.3f)
        check(view.settings.textZoom == 130)
        SystemTextScaling.apply(view, false, "system", 1f)
        check(view.settings.textZoom == 100)
        SystemTextScaling.apply(view, false, "125", 2f)
        check(view.settings.textZoom == 125)
        SystemTextScaling.apply(view, false, "100", 3f)
        check(view.settings.textZoom == 100)
        view.settings.textZoom = 175
        val manualWrites = view.settings.writes
        SystemTextScaling.apply(view, false, "manual", 2f)
        check(view.settings.textZoom == 175 && view.settings.writes == manualWrites)
      }
      SystemTextScaling.apply(worker, true, "system", 2f)
      check(worker.settings.textZoom == 100 && worker.settings.writes == 0)
      for (scale in listOf(Float.NaN, Float.POSITIVE_INFINITY, 0f, -1f)) {
        check(SystemTextScaling.zoom(scale, "system") == 100)
      }
      check(SystemTextScaling.zoom(Float.MAX_VALUE, "system") == 1000)
      check(SystemTextScaling.zoom(0.001f, "system") == 1)
      for (policy in listOf("0", "-1", "1001", "+100", "0100", "1.5", "unknown")) {
        check(runCatching { SystemTextScaling.zoom(1f, policy) }.exceptionOrNull() is IllegalArgumentException)
      }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})
