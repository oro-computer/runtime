import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = ['kotlinc', 'java'].every(command =>
  spawnSync(command, ['-version'], { stdio: 'ignore' }).status === 0)

test('Android activity platform waits for its attached application context', {
  skip: !available ? 'Kotlin and a JVM are required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/app/app.kt', import.meta.url), 'utf8')
  const platformStart = source.indexOf('open class AppPlatform (context: Context) {')
  const platformEnd = source.indexOf('\n  fun isDocumentURI', platformStart)
  const activityStart = source.indexOf('open class AppActivity :')
  const activityEnd = source.indexOf('\n  open val permissionRequests', activityStart)
  const platformProperty = source.slice(activityStart, activityEnd).split('\n')
    .find(line => line.includes('open val platform'))
  assert.ok(platformStart >= 0 && platformEnd > platformStart && platformProperty)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-activity-context-'))
  const filename = path.join(directory, 'Regression.kt')
  const jar = path.join(directory, 'regression.jar')
  writeFileSync(filename, `
    open class Context {
      var attached: Context? = null
      var applicationContextReads = 0
      val applicationContext: Context
        get() {
          applicationContextReads++
          return checkNotNull(attached) { "Activity base context is not attached" }
        }
    }
    ${source.slice(platformStart, platformEnd)}
      fun contextForFixture() = context
    }
    class Activity : Context() {
      ${platformProperty.replace('open val', 'val')}
    }
    fun main() {
      val activity = Activity()
      check(activity.applicationContextReads == 0) { "Construction precedes Android context attachment" }
      val application = Context()
      activity.attached = application
      val platform = activity.platform
      check(platform.contextForFixture() === application) { "Retain the application, not an activity" }
      check(activity.platform === platform)
      check(activity.applicationContextReads == 1) { "Create the platform only once after attachment" }
    }
  `)
  const compiled = spawnSync('kotlinc', [filename, '-include-runtime', '-d', jar], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync('java', ['-jar', jar], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
