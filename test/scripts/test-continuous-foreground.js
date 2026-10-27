// Public Android qualification only: verifies APK bytes and touches only the isolated fixture.
import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
import crypto from 'node:crypto'
import path from 'node:path'
const serial = process.env.ANDROID_SERIAL
const adbPath = process.env.ADB || (process.env.ANDROID_HOME ? path.join(process.env.ANDROID_HOME, 'platform-tools', process.platform === 'win32' ? 'adb.exe' : 'adb') : 'adb')
const apk = process.env.ORO_TEST_CONTINUOUS_APK
if (!serial || !apk) throw new Error('Set ANDROID_SERIAL and ORO_TEST_CONTINUOUS_APK to the exact installed public continuous fixture')
const application = 'oro.runtime.qualification.continuous'
const artifact = { apkSha256: crypto.createHash('sha256').update(fs.readFileSync(apk)).digest('hex') }
const report = { serial, androidApi: '', apkSha256: artifact.apkSha256, checks: [], unverified: ['Doze/reboot', 'other devices and Android versions', 'product authorization/remote outcomes', 'physical touch of notification actions'] }
const adb = (...args) => execFileSync(adbPath, ['-s', serial, ...args], { encoding: 'utf8', timeout: 30000 }).trim()
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
async function wait (message, predicate) {
  const deadline = Date.now() + 15000
  while (Date.now() < deadline) {
    if (await predicate()) return
    await sleep(200)
  }
  throw new Error(message)
}

function native (action) {
  const text = adb('shell', 'am', 'broadcast', '--receiver-foreground', '-a', 'oro.runtime.qualification.STATE.' + action,
    '-n', application + '/oro.runtime.qualification.extensions.StateQualificationReceiver')
  const raw = text.match(/data="(.*)"/)?.[1]
  if (!raw) throw new Error('Missing public receiver result: ' + text)
  try { return JSON.parse(raw) } catch { return JSON.parse(raw.replaceAll('\\"', '"')) }
}

let port
let socket
async function connect () {
  let pid
  await wait('Missing public process', () => {
    try {
      pid = adb('shell', 'pidof', application).split(/\s+/)[0]
      return /^\d+$/.test(pid)
    } catch { return false }
  })
  if (port) adb('forward', '--remove', 'tcp:' + port)
  port = adb('forward', 'tcp:0', 'localabstract:webview_devtools_remote_' + pid)
  let target
  await wait('Public renderer unavailable', async () => {
    try {
      const response = await fetch('http://127.0.0.1:' + port + '/json/list', { signal: AbortSignal.timeout(2000) })
      const targets = await response.json()
      target = targets.find(target => target.type === 'page' && target.url.includes(application) && new URL(target.url).pathname === '/index.html')
      return !!target
    } catch { return false }
  })
  socket = new globalThis.WebSocket(target.webSocketDebuggerUrl)
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Renderer socket timeout')), 5000)
    socket.addEventListener('open', () => { clearTimeout(timer); resolve() }, { once: true })
    socket.addEventListener('error', reject, { once: true })
  })
  const pending = new Map()
  let sequence = 0
  socket.addEventListener('message', event => {
    const message = JSON.parse(String(event.data))
    const request = pending.get(message.id)
    if (!request) return
    pending.delete(message.id)
    clearTimeout(request.timer)
    if (message.error) request.reject(new Error(JSON.stringify(message.error)))
    else request.resolve(message.result)
  })
  socket.addEventListener('close', () => {
    for (const request of pending.values()) {
      clearTimeout(request.timer)
      request.reject(new Error('Public process closed'))
    }
    pending.clear()
  })
  const evaluate = async expression => {
    const id = ++sequence
    const result = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(id)
        reject(new Error('Public evaluation timed out'))
      }, 15000)
      pending.set(id, { resolve, reject, timer })
      socket.send(JSON.stringify({
        id,
        method: 'Runtime.evaluate',
        params: { expression, awaitPromise: true, returnByValue: true, userGesture: true }
      }))
    })
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails))
    return result.result.value
  }
  await wait('Public document did not become ready', () => evaluate('globalThis.publicMonitor?.results.ready === true'))
  return evaluate
}
const options = id => ({ id, kind: 'qualification.wait', authorityRef: 'public-authority', accountRef: 'public-account', targetRef: 'public-target', operationRef: 'public-wait', userInitiated: true })
try {
  const installed = adb('shell', 'pm', 'path', application).replace(/^package:/, '')
  const hash = adb('shell', 'sha256sum', installed).split(/\s+/)[0]
  if (hash !== artifact.apkSha256) throw Error('Installed public APK differs from selected artifact')
  report.androidApi = adb('shell', 'getprop', 'ro.build.version.sdk')
  adb('shell', 'pm', 'grant', application, 'android.permission.POST_NOTIFICATIONS')
  adb('shell', 'am', 'force-stop', application)
  adb('shell', 'am', 'start', '-n', application + '/.MainActivity')
  let evaluate = await connect()
  const support = await evaluate('publicMonitor.monitoring.getSupport()')
  if (!support.available || !support.canStart || support.maximumDurationMs !== null || support.automaticRestart !== false) throw Error('Continuous admission support differs: ' + JSON.stringify(support))
  for (const api of ['monitoring', 'foreground']) await evaluate(`(async()=>{for(const task of await publicMonitor.${api}.list()){await publicMonitor.${api}.release(task.id,task.generation)}})()`)
  const id = 'public-' + crypto.randomUUID()
  const first = await evaluate('publicMonitor.monitoring.start(' + JSON.stringify(options(id)) + ')')
  await wait('Native monitor did not execute', () => native('CONTINUOUS').tasks.some(t => t.id === id && t.phase === 'running'))
  report.checks.push('Final installed APK hash, actual specialUse admission, compiled executor and encrypted native journal')
  const finite = await evaluate('publicMonitor.foreground.start(' + JSON.stringify({ ...options('public-finite'), durationMs: 300000 }) + ')')
  await wait('Independent finite executor did not execute', () => native('FOREGROUND').tasks.some(t => t.id === 'public-finite' && t.phase === 'running'))
  adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
  await sleep(500)
  if (!native('CONTINUOUS').tasks.some(t => t.id === id && t.phase === 'running') || !native('FOREGROUND').tasks.some(t => t.phase === 'running')) throw Error('Home interrupted admitted work')
  native('STOP_CONTINUOUS')
  await wait('Notification Stop did not retain UNKNOWN/close', () => {
    const s = native('CONTINUOUS')
    return s.closed === id && s.tasks.some(t => t.id === id && t.phase === 'unknown')
  })
  if (!native('FOREGROUND').tasks.some(t => t.id === 'public-finite' && t.phase === 'running')) throw Error('Continuous Stop cancelled finite execution')
  report.checks.push('Concurrent independent finite task, Home survival and actual notification PendingIntent Stop with native cleanup; finite task unaffected')
  adb('shell', 'am', 'start', '-n', application + '/.MainActivity')
  socket.close()
  evaluate = await connect()
  await evaluate('publicMonitor.foreground.stop("public-finite",' + JSON.stringify(finite.generation) + ')')
  await evaluate('publicMonitor.monitoring.release(' + JSON.stringify(id) + ',' + JSON.stringify(first.generation) + ')')
  const replacement = await evaluate('publicMonitor.monitoring.start(' + JSON.stringify(options(id)) + ')')
  const stale = await evaluate('(async()=>{try{await publicMonitor.monitoring.stop(' + JSON.stringify(id) + ',' + JSON.stringify(first.generation) + ');return "unexpected"}catch(error){return error.code}})()')
  if (stale !== 'ESTALE') throw Error('Stale stop was not refused: ' + stale)
  await wait('Replacement not running', () => native('CONTINUOUS').tasks.some(t => t.id === id && t.phase === 'running'))
  const before = native('CONTINUOUS').runs
  report.checks.push('Released ID reused with new generation; stale API Stop refused without cancelling replacement')
  const stacks = adb('shell', 'cmd', 'activity', 'stack', 'list').split(/(?=RootTask id=)/)
  const own = stacks.find(block => block.includes('taskId=') && block.includes(application + '/'))
  const ownTasks = own?.split('\n').filter(line => line.includes('taskId=')) || []
  const stackId = own?.match(/^RootTask id=(\d+)/)?.[1]
  if (!stackId || ownTasks.length !== 1 || !ownTasks[0].includes(': ' + application + '/')) throw Error('Own isolated task cannot be identified safely')
  socket.close()
  adb('shell', 'cmd', 'activity', 'stack', 'remove', stackId)
  await sleep(500)
  if (!native('CONTINUOUS').tasks.some(task => task.id === id && task.phase === 'running')) throw Error('Removing own task stopped continuous monitoring')
  report.checks.push('Actual OS removal of the isolated app task preserves continuous monitoring and its notification')
  adb('shell', 'am', 'start', '-n', application + '/.MainActivity')
  evaluate = await connect()
  socket.close()
  adb('shell', 'am', 'force-stop', application)
  adb('shell', 'am', 'start', '-n', application + '/.MainActivity')
  evaluate = await connect()
  const interrupted = await evaluate('publicMonitor.monitoring.getStatus(' + JSON.stringify(id) + ')')
  if (interrupted.phase !== 'unknown' || native('CONTINUOUS').runs !== before) throw Error('Process loss did not preserve uncertainty without replay')
  await evaluate('publicMonitor.monitoring.release(' + JSON.stringify(id) + ',' + JSON.stringify(replacement.generation) + ')')
  report.checks.push('Actual process stop/reopen retains UNKNOWN with no executor replay')
  const permissionId = 'permission-' + crypto.randomUUID()
  const permission = await evaluate('publicMonitor.monitoring.start(' + JSON.stringify(options(permissionId)) + ')')
  await wait('Permission test monitor not running', () => native('CONTINUOUS').tasks.some(task => task.id === permissionId && task.phase === 'running'))
  const permissionRuns = native('CONTINUOUS').runs
  socket.close()
  adb('shell', 'pm', 'revoke', application, 'android.permission.POST_NOTIFICATIONS')
  await wait('Revoked notification permission did not stop monitoring', () => native('CONTINUOUS').tasks.some(task => task.id === permissionId && task.phase === 'unknown'))
  adb('shell', 'am', 'start', '-n', application + '/.MainActivity')
  evaluate = await connect()
  if ((await evaluate('publicMonitor.monitoring.getSupport()')).canStart !== false) throw Error('Revoked notification permission still admits monitoring')
  if (native('CONTINUOUS').runs !== permissionRuns) throw Error('Permission loss restarted executor')
  adb('shell', 'pm', 'grant', application, 'android.permission.POST_NOTIFICATIONS')
  await evaluate('publicMonitor.monitoring.release(' + JSON.stringify(permissionId) + ',' + JSON.stringify(permission.generation) + ')')
  report.checks.push('Actual POST_NOTIFICATIONS revocation stops execution, retains UNKNOWN and blocks admission without replay')
  if (process.env.ORO_TEST_CONTINUOUS_REPORT) fs.writeFileSync(process.env.ORO_TEST_CONTINUOUS_REPORT, JSON.stringify(report, null, 2) + '\n')
  console.log('Passed ' + report.checks.length + ' continuous Android qualification groups')
} finally {
  socket?.close()
  if (port) { try { adb('forward', '--remove', 'tcp:' + port) } catch {} }
  try { adb('shell', 'pm', 'grant', application, 'android.permission.POST_NOTIFICATIONS') } catch {}
  try { adb('shell', 'am', 'force-stop', application) } catch {}
}
