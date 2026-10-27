import app from 'oro:application'
import monitoring from 'oro:foreground/continuous'
import foreground from 'oro:foreground'
const win = await app.getCurrentWindow()
const results = { ready: false }
globalThis.publicMonitor = { app, monitoring, foreground, win, results }
const output = () => { document.querySelector('#status').textContent = JSON.stringify(results, null, 2) }
const options = id => ({ id, kind: 'qualification.wait', authorityRef: 'public-authority', accountRef: 'public-account', targetRef: 'public-target', operationRef: 'public-wait', userInitiated: true })
document.querySelector('#start').onclick = async () => { try { results.admission = await monitoring.start(options('public-monitor')); output() } catch (error) { results.error = error.code || error.message; output() } }
document.querySelector('#finite').onclick = async () => { try { results.finite = await foreground.start({ ...options('public-finite'), durationMs: 300000 }); output() } catch (error) { results.error = error.code || error.message; output() } }
document.querySelector('#stop').onclick = async () => { if (results.admission) { results.stopped = await monitoring.stop(results.admission.id, results.admission.generation); output() } }
results.support = await monitoring.getSupport()
await win.markPresented(); await win.markReady(); results.ready = true; output()
