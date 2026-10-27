import test from 'oro:test'
import os from 'oro:os'
import platform from 'oro:platform'
import { getStatus } from 'oro:network-status'
import documentAccess from 'oro:document-access'
import { perform } from 'oro:haptics'
import { getBuildIdentity, getCapabilities } from 'oro:diagnostics/runtime'
import { getStatus as getSecureStorageStatus } from 'oro:secure-storage'
import application from 'oro:application'
import { getStatus as getAuthenticationStatus } from 'oro:authentication'
import { getStatus as getLauncherStatus } from 'oro:launcher'
import { getSupport as getDeferredSupport } from 'oro:background/deferred'

test('Android capabilities validate before native UI', async t => {
  t.equal(await perform('selection'), false, 'feedback requires the app user preference')
  await t.rejects(perform('unknown', { enabled: true }), TypeError, 'semantic kind is validated')
  await t.rejects(documentAccess.stat('content://example/a\0b'), TypeError, 'raw URI controls are rejected before JNI')
  await t.rejects(documentAccess.stat('file:///private'), TypeError, 'metadata requires a content URI')
  await t.rejects(documentAccess.readDirectory('content://example/tree/root', { limit: 129 }), TypeError, 'provider page limits are bounded')
  await t.rejects(documentAccess.readDirectory('content://example/tree/root', { offset: -1 }), TypeError, 'provider offsets are bounded')
  const window = await application.getCurrentWindow()
  await t.rejects(window.setFullscreen('yes'), TypeError, 'fullscreen is explicitly boolean')
  await t.rejects(window.setContentProtection('yes'), TypeError, 'screen protection is explicitly boolean')
  await t.rejects(documentAccess.persist('file:///private'), TypeError, 'persistent grants require content URIs')
  await t.rejects(documentAccess.persist('content://example/document', { read: false }), TypeError, 'empty grant modes fail')
})

test('Native capability availability follows the platform inventory', async t => {
  if (os.platform() !== 'android') {
    for (const request of [documentAccess.list, getBuildIdentity, getCapabilities, getSecureStorageStatus]) {
      try {
        await request()
        t.fail('unsupported Android capability should reject')
      } catch (error) {
        t.equal(error.code, 'ENOSYS', 'unsupported platform is observable')
      }
    }
    if (os.platform() === 'linux') {
      const status = await getStatus()
      t.equal(typeof status.available, 'boolean', 'Linux reports network-monitor availability')
      t.equal(status.transportDetailsAvailable, false, 'Linux does not infer transport details')
      t.equal(status.serverReachabilityKnown, false, 'network state does not establish server health')
    } else {
      try {
        await getStatus()
        t.fail('missing network-status backend should reject')
      } catch (error) {
        t.equal(error.code, 'ENOSYS', 'missing network-status backend is observable')
      }
    }
    const inventory = await platform.getCapabilities()
    for (const [feature, probe] of [['authentication', getAuthenticationStatus], ['launcher', getLauncherStatus], ['deferred', getDeferredSupport]]) {
      const descriptor = inventory.features[feature]
      t.ok(descriptor, 'backend inventory describes ' + feature)
      if (descriptor.implementation === 'implemented') {
        // The headless harness uses inventory; Linux private native calls require a GUI document.
        t.equal(descriptor.platform, inventory.platform, 'implemented backend identifies its platform')
      } else {
        const status = await probe()
        t.equal(status.available, false, 'missing or restricted backend reports unavailable')
        t.equal(status.implementation, descriptor.implementation, 'support result preserves backend classification')
        t.equal(status.reason, descriptor.reason, 'support result preserves the unavailable reason')
      }
    }
    return
  }
  const identity = await getBuildIdentity()
  t.equal(identity.androidBridgeProtocol, 5, 'running native bridge matches packaged protocol')
  t.equal(identity.assetsVerified, true, 'native verified assets before loading this document')
  t.ok(/^[a-f0-9]{64}$/.test(identity.assetManifestSha256), 'native manifest digest is SHA-256')
  const capabilities = await getCapabilities()
  t.equal(capabilities.backgroundJavaScript, false, 'arbitrary background JS stays unavailable')
  t.equal(capabilities.push, false, 'no unconfigured native push is advertised')
  t.equal(capabilities.deferredWrites, false, 'queued writes stay unavailable')
  const authentication = await getAuthenticationStatus()
  t.equal(authentication.enabled, capabilities.biometricSecretUnlock, 'protected key operations require explicit opt-in')
  t.equal(authentication.cryptographicOperationsOnly, true, 'authentication is cryptographic, not a JavaScript identity boolean')
  const launcher = await getLauncherStatus()
  t.equal(launcher.available, capabilities.namedLauncherDestinations, 'launcher opt-in is observable')
  const deferred = await getDeferredSupport()
  t.equal(deferred.available, capabilities.nativeDeferredReads, 'native scheduler configuration is observable')
  t.equal(deferred.readOnly, true, 'deferred work only supports native reads/reconciliation')
  t.equal(deferred.maximumAttempts, 5, 'attempt budget is bounded')
  t.equal(capabilities.documentQueryWorkers, 2, 'provider worker count is bounded')
  t.equal(capabilities.documentQueryDeadlineMs, 5000, 'provider response deadline is explicit')
  t.ok(Array.isArray(identity.processExits.entries) && identity.processExits.entries.length <= 16, 'exit summaries are bounded')
  const protection = await getSecureStorageStatus()
  t.equal(protection.readability, 'not-probed', 'key metadata does not decrypt stored values')
  const status = await getStatus()
  t.equal(typeof status.validated, 'boolean', 'network validation is reported')
  t.equal(status.name, status.validated ? 'online' : 'offline', 'online requires Android validation')
  const grants = await documentAccess.list()
  t.ok(Array.isArray(grants) && grants.length <= 128, 'grant listing is bounded without opening a picker')
})
