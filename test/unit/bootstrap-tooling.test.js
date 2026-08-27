import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')

function readFile (relativePath) {
  return readFileSync(path.join(repoRoot, relativePath), 'utf8')
}

test('ci_version_check.sh prefers oroc CLI', () => {
  const script = readFile('bin/ci_version_check.sh')
  assert.match(
    script,
    /CLI_BIN="\${ORO_CLI_BIN:-oroc}"/,
    'ci_version_check.sh should default to oroc'
  )
})

test('ci_version_check.ps1 prefers oroc CLI', () => {
  const script = readFile('bin/ci_version_check.ps1')
  assert.match(
    script,
    /\$cliBin = if \(\$env:ORO_CLI_BIN .* "oroc"/,
    'ci_version_check.ps1 should default to oroc'
  )
})

test('source-build documentation keeps Android and iOS exclusions distinct', () => {
  const packageMetadata = JSON.parse(readFile('package.json'))
  assert.match(
    packageMetadata.scripts.lint,
    /npm run lint:build-env/,
    'the authoritative lint command should enforce the source-build environment contract'
  )

  const checker = readFile('bin/check-build-environment-docs.js')
  assert.match(
    checker,
    /hasAndroid !== hasIos/,
    'the documentation checker should reject unpaired mobile exclusion controls'
  )

  const buildDocumentation = [
    'AGENTS.md',
    'CONTRIBUTING.md',
    'README.md',
    'RELEASE_CHECKLIST.md',
    'SECURITY.md',
    'SUPPORT.md',
    'api/CLI.md',
    'docs/AI_WHISPER.md',
    'docs/BUILD_ENVIRONMENT.md',
    'docs/CI_MATRIX.md',
    'docs/MCP.md',
    'docs/NAVIGATOR_MOUNTS.md',
    'docs/ORO_ARTIFACT_NAMING.md',
    'docs/RUNTIME_ARCHITECTURE.md',
    'docs/TLS_QUICKSTART.md',
    'docs/llms.txt',
    'docs/release/ORO_RELEASE_AUTOMATION.md',
    'examples/asn1/README.md',
    'examples/dbus/README.md',
    'examples/navigator-mounts/README.md',
    'share/man/man1/oroc-build.1',
    'share/man/man1/oroc-env.1',
    'share/man/man1/oroc-mcp.1',
    'test/README.md'
  ]

  for (const relativePath of buildDocumentation) {
    const documentation = readFile(relativePath)
    assert.match(
      documentation,
      /NO_ANDROID/,
      `${relativePath} should describe the Android-only source-build exclusion`
    )
    assert.match(
      documentation,
      /NO_IOS/,
      `${relativePath} should describe the iOS-only source-build exclusion`
    )
  }

  const canonical = readFile('docs/BUILD_ENVIRONMENT.md')
  assert.match(
    canonical,
    /independent, presence-based environment/,
    'the canonical guide should state that the controls are independent presence flags'
  )
  assert.match(
    canonical,
    /NO_ANDROID=0[\s\S]*NO_IOS=false/,
    'the canonical guide should explain that false-like non-empty values still disable targets'
  )
  assert.match(
    canonical,
    /do not select the target for a downstream application build/,
    'the canonical guide should distinguish source bootstrap from application target selection'
  )
})

test('uninstall.sh removes the oroc binary', () => {
  const script = readFile('bin/uninstall.sh')
  assert.match(
    script,
    /declare bins=\("\$PREFIX\/bin\/oroc"\)/,
    'uninstall.sh should clean up oroc'
  )
})

test('functions.sh sudo prompt references Oro CLI', () => {
  const script = readFile('bin/functions.sh')
  assert.match(
    script,
    /'oroc' would like to use 'sudo'/,
    'sudo prompt should mention oroc as the CLI'
  )
})

test('publish-npm-modules.sh stages Oro CLI packages', () => {
  const script = readFile('bin/publish-npm-modules.sh')
  assert.match(
    script,
    /CLI_PACKAGE_SPECS=\([\s\S]*"@oro-computer:runtime"[\s\S]*\)/,
    'publish-npm-modules.sh should stage @oro-computer/runtime'
  )
  assert.doesNotMatch(
    script,
    /@socketsupply|socketsupply|socket-node|socket\b/,
    'publish-npm-modules.sh should not reference legacy packages'
  )
  assert.match(
    script,
    /ORO_HOME" != "\$npm_staging_root\/"\*/,
    'publish-npm-modules.sh should restrict repository staging to build/npm'
  )
  assert.match(
    script,
    /temporary_root" != "\/"[\s\S]*ORO_HOME" == "\$temporary_root\/"\*/,
    'publish-npm-modules.sh should only allow temporary staging below TMPDIR'
  )
  assert.match(
    script,
    /resolve_removal_path[\s\S]*realpathSync\.native/,
    'publish-npm-modules.sh should resolve symlinked staging ancestors'
  )
  assert.match(
    script,
    /npm_staging_root" != "\$expected_npm_staging_root"/,
    'publish-npm-modules.sh should reject a symlinked repository staging root'
  )
  assert.match(
    script,
    /--help[\s\S]*usage[\s\S]*exit 0/,
    'publish-npm-modules.sh should expose help without staging packages'
  )
  assert.match(
    script,
    /only_platforms && only_top_level/,
    'publish-npm-modules.sh should reject conflicting package selections'
  )
  assert.match(
    script,
    /--yes-deps[\s\S]*install_args\+=\("--yes-deps"\)[\s\S]*install\.sh[\s\S]*install_args/,
    'publish-npm-modules.sh should pass non-interactive dependency consent only to the source installer'
  )
  assert.match(
    script,
    /platform" == "win32"[\s\S]*built_cli="\$ORO_HOME\/bin\/oroc\.exe"/,
    'Windows package validation should inspect the executable produced by install.ps1'
  )
  assert.match(
    script,
    /NO_ANDROID[\s\S]*disables only Android artifacts[\s\S]*NO_IOS[\s\S]*disables only iOS\/iOS Simulator artifacts/,
    'publish-npm-modules.sh help should distinguish the two mobile target exclusions'
  )
  assert.match(
    script,
    /docs\/BUILD_ENVIRONMENT\.md/,
    'published runtime packages should include the source-build environment contract'
  )
  assert.match(
    script,
    /should_stage_target_directory[\s\S]*NO_ANDROID[\s\S]*\*-android[\s\S]*NO_IOS[\s\S]*iPhoneOS\|\*-iPhoneSimulator/,
    'platform package staging should filter stale target directories through both independent exclusions'
  )
  assert.match(
    script,
    /if \[\[ -z "\$\{NO_ANDROID:-\}" \]\]; then\s+for android_lib_dir/,
    'copy-mode package staging should not include Android libraries when NO_ANDROID is present'
  )
  assert.match(
    script,
    /validate_platform_target_selection[\s\S]*NO_ANDROID excluded Android[\s\S]*missing required ABI[\s\S]*NO_IOS excluded Apple-mobile targets[\s\S]*missing required iOS target/,
    'platform packaging should fail when staged target directories disagree with either exclusion'
  )
  assert.doesNotMatch(
    script,
    /npm publish/,
    'the local packaging helper must not bypass the protected OIDC publication job'
  )
  assert.match(
    script,
    /function _pack_or_link[\s\S]*npm pack/,
    'the local packaging helper should produce inspectable tarballs by default'
  )

  const nodeAdapterManifest = JSON.parse(
    readFile('npm/packages/@oro-computer/runtime-node/package.json')
  )
  assert.equal(
    nodeAdapterManifest.scripts.pub,
    undefined,
    'package manifests must not expose an unverified direct-publish shortcut'
  )

  const platformPack = script.lastIndexOf(
    'package_platform_variant "$scope" "$name" "$arch"'
  )
  const nodeAdapterPack = script.lastIndexOf('publish_node_adapter')
  assert.ok(platformPack >= 0, 'platform package pack call should exist')
  assert.ok(
    nodeAdapterPack > platformPack,
    'platform packages should be packed before the Node adapter and meta-package'
  )
})

test('install.sh help distinguishes source-bootstrap target exclusions', () => {
  const script = readFile('bin/install.sh')
  assert.match(
    script,
    /NO_ANDROID=<non-empty>[\s\S]*Disable only Android/,
    'installer help should describe the Android-only exclusion'
  )
  assert.match(
    script,
    /NO_IOS=<non-empty>[\s\S]*Disable only iOS and iOS Simulator/,
    'installer help should describe the iOS-only exclusion'
  )
  assert.match(
    script,
    /presence flags: 0 and false are non-empty/,
    'installer help should explain presence-flag behavior'
  )
  assert.match(
    script,
    /--no-android-fte[\s\S]*not a reliable Android artifact exclusion/,
    'installer help should distinguish prompt suppression from NO_ANDROID target exclusion'
  )

  const canonical = readFile('docs/BUILD_ENVIRONMENT.md')
  assert.match(
    canonical,
    /`--no-android-fte` is not `NO_ANDROID`/,
    'the canonical guide should distinguish Android first-time setup from target exclusion'
  )
  assert.match(
    canonical,
    /NO_ANDROID[\s\S]*ORO_ANDROID_CI/,
    'the canonical guide should define Android exclusion precedence in CI'
  )
})

test('source-build environment guidance ships with CLI and MCP distributions', () => {
  const installer = readFile('bin/install.sh')
  assert.match(
    installer,
    /docs\/BUILD_ENVIRONMENT\.md:docs\/BUILD_ENVIRONMENT\.md/,
    'the source installer should preserve the canonical guide below the installed docs root'
  )
  assert.match(
    installer,
    /source_doc_dir\/docs\/BUILD_ENVIRONMENT\.md/,
    'prefix linking should preserve the installed guide path'
  )

  const mcp = readFile('src/cli/mcp.cc')
  assert.match(
    mcp,
    /addWorkspaceDoc\("docs\/BUILD_ENVIRONMENT\.md"/,
    'MCP resources should advertise the workspace guide'
  )
  assert.match(
    mcp,
    /runtimeDocsRoot \/ "docs" \/ "BUILD_ENVIRONMENT\.md", "runtime-doc:\/BUILD_ENVIRONMENT\.md"/,
    'MCP resources should advertise the installed guide through a stable URI'
  )

  const cli = readFile('src/cli/main.cc')
  assert.match(
    cli,
    /envs\["NO_ANDROID"\][\s\S]*envs\["NO_IOS"\]/,
    'oroc env should inspect both source-bootstrap controls'
  )
})

test('npm publish workflow uses exact tarballs, OIDC, and the signed source', () => {
  const workflow = readFile('.github/workflows/publish-npm.yml')
  assert.doesNotMatch(
    workflow,
    /NPM_TOKEN|NODE_AUTH_TOKEN/,
    'npm trusted publishing must not depend on a long-lived registry token'
  )
  assert.match(
    workflow,
    /workflow_call:[\s\S]*publish:[\s\S]*type: boolean/,
    'the signed-tag release workflow should be able to invoke npm publication'
  )
  assert.match(
    workflow,
    /environment: npm-publish[\s\S]*id-token: write/,
    'only the final publication job should enter the npm OIDC environment'
  )
  assert.equal(
    workflow.match(/environment: npm-publish/g)?.length,
    1,
    'package build jobs should not enter the protected publishing environment'
  )
  assert.equal(
    workflow.match(/ref: \$\{\{ needs\.validate-tag\.outputs\.commit_sha \}\}/g)
      ?.length,
    2,
    'publishing jobs should checkout the commit contained in the verified tag'
  )
  assert.doesNotMatch(
    workflow,
    /NO_ANDROID: \$\{\{ matrix\./,
    'the npm workflow must not assign a matrix boolean directly to NO_ANDROID'
  )
  assert.doesNotMatch(
    workflow,
    /NO_IOS: \$\{\{ matrix\./,
    'the npm workflow must not assign a matrix boolean directly to NO_IOS'
  )
  assert.match(
    workflow,
    /package: linux-x64[\s\S]*build_android: true[\s\S]*exclude_android: false[\s\S]*exclude_ios: true/,
    'the Linux x64 package should opt into Android and explicitly exclude iOS'
  )
  assert.match(
    workflow,
    /package: darwin-x64[\s\S]*exclude_android: true[\s\S]*exclude_ios: false[\s\S]*package: darwin-arm64[\s\S]*exclude_android: true[\s\S]*exclude_ios: false/,
    'macOS packages should explicitly exclude Android and include Apple-mobile targets'
  )
  assert.match(
    workflow,
    /Configure platform package target families[\s\S]*NO_ANDROID=1[\s\S]*NO_ANDROID=[\s\S]*NO_IOS=1[\s\S]*NO_IOS=/,
    'the npm workflow should translate matrix intent into presence flags without false-like values'
  )
  assert.match(
    workflow,
    /ORO_ANDROID_CI=1[\s\S]*ANDROID_SDK_MANAGER_ACCEPT_LICENSES=yes[\s\S]*--only-platforms --yes-deps/,
    'the Android npm package should use explicit non-interactive CI bootstrap controls'
  )
  assert.match(
    workflow,
    /Build Windows runtime for npm package[\s\S]*install\.ps1 -yesdeps -verbose[\s\S]*Pack platform package \(Windows\)[\s\S]*--only-platforms --no-rebuild --no-remove-oro-home/,
    'the Windows npm package should initialize the Visual Studio toolchain before packing the same staged runtime'
  )
  assert.equal(
    workflow.match(/--dry-run --pack-destination/g)?.length,
    3,
    'both platform branches and the top-level job should always produce inspectable tarballs before publication'
  )
  assert.match(
    workflow,
    /platform-packages:[\s\S]*needs: \[validate-tag, top-level-packages\][\s\S]*Download exact top-level tarballs for native smoke test[\s\S]*Smoke install exact package family on native runner/,
    'every native platform package job should wait for and smoke-install the exact top-level tarballs'
  )
  assert.match(
    workflow,
    /platform_archive=.*PACKAGE_ID.*RELEASE_VERSION[\s\S]*node_archive=.*runtime-node.*RELEASE_VERSION[\s\S]*meta_archive=.*runtime-\$RELEASE_VERSION[\s\S]*npm install[\s\S]*oroc\.js" --version[\s\S]*oroc\.js" --help[\s\S]*runtime-node\/index\.cjs/,
    'native smoke tests should install all three exact tarballs and exercise the packaged CLI and Node adapter'
  )
  assert.match(
    workflow,
    /Verify complete npm package set[\s\S]*manifest_name[\s\S]*manifest_version/,
    'the publication job should verify every downloaded tarball manifest'
  )
  const platformIndex = workflow.indexOf(
    '"@oro-computer/runtime-win32-x64|oro-computer-runtime-win32-x64-$RELEASE_VERSION.tgz"',
    workflow.indexOf('Publish packages in dependency order')
  )
  const adapterIndex = workflow.indexOf(
    '"@oro-computer/runtime-node|oro-computer-runtime-node-$RELEASE_VERSION.tgz"',
    platformIndex
  )
  const metaIndex = workflow.indexOf(
    '"@oro-computer/runtime|oro-computer-runtime-$RELEASE_VERSION.tgz"',
    adapterIndex
  )
  assert.ok(
    platformIndex !== -1 && platformIndex < adapterIndex && adapterIndex < metaIndex,
    'platform packages must publish before the Node adapter and runtime meta-package'
  )
  assert.match(
    workflow,
    /published_integrity[\s\S]*local_integrity[\s\S]*already published with the verified integrity/,
    'release reruns should skip only an identical package already present in npm'
  )
  assert.equal(
    workflow.match(/^\s+npm publish "\$archive" --access public$/gm)?.length,
    1,
    'only the protected publication job should contain a registry publish command'
  )
})

test('npm package family consistently uses the @oro-computer organization scope', () => {
  const packageNames = [
    'runtime-linux-x64',
    'runtime-linux-arm64',
    'runtime-darwin-x64',
    'runtime-darwin-arm64',
    'runtime-win32-x64',
    'runtime-node',
    'runtime'
  ]

  for (const packageName of packageNames) {
    const manifest = JSON.parse(
      readFile(`npm/packages/@oro-computer/${packageName}/package.json`)
    )
    assert.equal(
      manifest.name,
      `@oro-computer/${packageName}`,
      `${packageName} should publish from the GitHub-matching npm organization scope`
    )
  }

  const scopeSurfaces = [
    '.github/workflows/publish-npm.yml',
    'README.md',
    'api/node-esm-loader.js',
    'bin/bootstrap-npm-packages.js',
    'bin/check-release-version.js',
    'bin/publish-npm-modules.sh',
    'bin/set-release-version.js',
    'docs/ORO_ARTIFACT_NAMING.md',
    'docs/release/ORO_RELEASE_AUTOMATION.md',
    'npm/bin/oroc.js',
    'package.json',
    'pnpm-lock.yaml',
    'pnpm-workspace.yaml',
    'test/package-lock.json',
    'test/package.json'
  ]

  for (const relativePath of scopeSurfaces) {
    assert.doesNotMatch(
      readFile(relativePath),
      /@orocomputer|orocomputer-runtime|npm\/packages\/@orocomputer/,
      `${relativePath} should not retain the previous npm scope or tarball stem`
    )
  }

  const testLock = JSON.parse(readFile('test/package-lock.json'))
  assert.ok(
    testLock.packages['node_modules/@oro-computer/runtime'],
    'the tracked test lockfile should install the canonical runtime scope on clean CI runners'
  )

  const releaseChecker = readFile('bin/check-release-version.js')
  assert.match(
    releaseChecker,
    /expectedPackageNames[\s\S]*@oro-computer\/runtime-linux-x64[\s\S]*expectedOptionalDependencies/,
    'release metadata validation should enforce the exact scoped package family and dependency set'
  )
})

test('npm package-name bootstrap is explicit, prerelease-only, and local', () => {
  const packageMetadata = JSON.parse(readFile('package.json'))
  assert.equal(
    packageMetadata.scripts['release:bootstrap-npm'],
    'node ./bin/bootstrap-npm-packages.js',
    'maintainers should have a discoverable first-publication bootstrap command'
  )

  const script = readFile('bin/bootstrap-npm-packages.js')
  assert.match(
    script,
    /0\.0\.0-trusted-publishing-bootstrap\.0/,
    'package-name reservation must not consume the first release version'
  )
  assert.match(
    script,
    /publish && !args\.has\('--yes'\)/,
    'registry mutation should require a second explicit confirmation flag'
  )
  assert.match(
    script,
    /\['publish', packageRoot, '--access', 'public', '--tag', 'bootstrap'\]/,
    'reservations should use a non-default bootstrap dist-tag'
  )
  assert.match(
    script,
    /https:\/\/registry\.npmjs\.org[\s\S]*\[\.\.\.npmArgs, '--registry', registry\]/,
    'package-name inspection and reservation should ignore an unrelated user default registry'
  )

  for (const workflow of [
    '.github/workflows/ci.yml',
    '.github/workflows/publish-npm.yml',
    '.github/workflows/release-artifacts.yml'
  ]) {
    assert.doesNotMatch(
      readFile(workflow),
      /bootstrap-npm-packages/,
      `${workflow} must never automate the credentialed package-name reservation`
    )
  }
})

test('release workflow binds builds to the validated source commit', () => {
  const workflow = readFile('.github/workflows/release-artifacts.yml')
  assert.match(
    workflow,
    /ref: \$\{\{ needs\.validate-release\.outputs\.commit_sha \}\}/,
    'release builds should checkout the validated source commit'
  )
  assert.match(
    workflow,
    /target_sha" != "\$SOURCE_COMMIT"/,
    'signed tag verification should match the checked-out source commit'
  )
  assert.doesNotMatch(
    workflow,
    /NO_ANDROID: \$\{\{ matrix\./,
    'the presence flag must not receive the non-empty string false from a matrix value'
  )
  assert.doesNotMatch(
    workflow,
    /NO_IOS: \$\{\{ matrix\./,
    'the iOS presence flag must not receive the non-empty string false from a matrix value'
  )
  assert.match(
    workflow,
    /EXCLUDE_ANDROID: \$\{\{ matrix\.exclude_android \}\}[\s\S]*EXCLUDE_IOS: \$\{\{ matrix\.exclude_ios \}\}/,
    'release jobs should model Android and iOS exclusion intent separately before exporting presence flags'
  )
  assert.match(
    workflow,
    /Validate packaged target families \(Unix\)[\s\S]*case "\$RELEASE_SUPPORT"[\s\S]*desktop\)[\s\S]*android\)[\s\S]*ios\)/,
    'release archives should fail when their actual mobile target directories disagree with matrix intent'
  )
  assert.match(
    workflow,
    /android\)[\s\S]*arm64-v8a-android x86_64-android[\s\S]*ios\)[\s\S]*arm64-iPhoneOS x86_64-iPhoneSimulator[\s\S]*arm64-iPhoneSimulator/,
    'release archives should require every advertised Android ABI and iOS device/simulator target'
  )
  assert.match(
    workflow,
    /uses: \.\/\.github\/workflows\/publish-npm\.yml[\s\S]*publish: true/,
    'a signed release tag should invoke npm publication automatically'
  )
  assert.match(
    workflow,
    /verify-release:[\s\S]*uses: \.\/\.github\/workflows\/ci\.yml[\s\S]*build-release-artifacts:[\s\S]*needs: \[validate-release, verify-release\]/,
    'release builds should wait for the reusable full-platform CI matrix'
  )
  assert.match(
    workflow,
    /publish-github-release:[\s\S]*needs: \[validate-release, verify-release-assets, publish-npm\]/,
    'the public GitHub release should wait for both archives and npm publication'
  )
  assert.match(
    workflow,
    /RELEASE_LABEL:[\s\S]*version=\$\{RELEASE_LABEL#v\}[\s\S]*build-release-artifacts:[\s\S]*RELEASE_VERSION: \$\{\{ needs\.validate-release\.outputs\.version \}\}/,
    'release tags should retain their v prefix while archive versions use normalized release metadata'
  )
  assert.match(
    workflow,
    /verify-release-assets:[\s\S]*needs: \[validate-release, build-release-artifacts\][\s\S]*verify-release-assets\.sh[\s\S]*publish-npm:[\s\S]*needs: \[validate-release, verify-release-assets\]/,
    'exact release asset verification should complete before npm publication can begin'
  )
  assert.equal(
    workflow.match(/verify-release-assets\.sh/g)?.length,
    2,
    'release assets should be verified before npm and again immediately before attachment'
  )
  assert.match(
    workflow,
    /Publish GitHub release with verified assets[\s\S]*draft: false/,
    'the final release job should publish the verified release instead of leaving a draft'
  )

  const verifier = readFile('bin/verify-release-assets.sh')
  assert.match(
    verifier,
    /linux-x64-desktop\|tar\.gz[\s\S]*windows-x64-desktop\|zip/,
    'release verification should enumerate every exact archive identity'
  )
  assert.match(
    verifier,
    /checksum_name" != "\$archive"[\s\S]*sha256sum --check[\s\S]*SPDXRef-DOCUMENT/,
    'release verification should bind checksum records to archive names and validate SPDX documents'
  )
})

test('version.sh delegates to synchronized release tooling', () => {
  const script = readFile('bin/version.sh')
  assert.match(
    script,
    /set-release-version\.js/,
    'version.sh must update every release manifest through the shared setter'
  )
  assert.match(
    script,
    /check-release-version\.js/,
    'version.sh must verify synchronized release metadata'
  )
  assert.doesNotMatch(
    script,
    /@socketsupply|socketsupply|socket-node/,
    'version.sh should not reference legacy packages'
  )
})

test('runtime-artifacts.sh defines Oro artifact name', () => {
  const script = readFile('bin/runtime-artifacts.sh')
  assert.match(
    script,
    /ORO_RUNTIME_ARTIFACT_NAME:=oro-runtime/,
    'runtime artifacts should default to oro-runtime naming'
  )
  assert.doesNotMatch(
    script,
    /socket-runtime/,
    'runtime artifacts should not define legacy aliases'
  )
})

test('runtime library rebuilds first-party objects after header changes', () => {
  const script = readFile('bin/build-runtime-library.sh')
  assert.match(
    script,
    /--ignore-header-mtimes[\s\S]*ignore_header_mtimes=1/,
    'the header mtime escape hatch should be parsed explicitly'
  )
  assert.match(
    script,
    /newest_header_mtime > \$\(stat_mtime "\$object"\)/,
    'first-party objects should be invalidated by newer headers'
  )
  assert.match(
    script,
    /newest_header_mtime > \$\(stat_mtime "\$destination"\)/,
    'the Linux desktop extension should be invalidated by newer headers'
  )
})

test('Linux headless launches use collision-safe Xvfb allocation', () => {
  const source = readFile('src/cli/main.cc')
  assert.match(
    source,
    /headlessRunnerFlags = " -a --server-args=/,
    'xvfb-run should automatically select an available display'
  )
})

test('desktop and runtime-core tests isolate writable runtime state', () => {
  for (const filename of [
    'test/scripts/test-desktop.js',
    'test/scripts/test-runtime-core.js'
  ]) {
    const script = readFile(filename)
    for (const variable of [
      'XDG_DATA_HOME',
      'XDG_CONFIG_HOME',
      'XDG_CACHE_HOME',
      'XDG_STATE_HOME'
    ]) {
      assert.match(
        script,
        new RegExp(`${variable}:`),
        `${filename} should isolate ${variable}`
      )
    }
    assert.match(
      script,
      /env\.TMPDIR = tmpdir/,
      `${filename} should isolate runtime temporary files`
    )
    assert.match(
      script,
      /isWritableDirectory\(env\.XDG_RUNTIME_DIR\)/,
      `${filename} should replace an unwritable inherited XDG runtime directory`
    )
  }
})

test('desktop tests expose their isolated fixtures to runtime code', () => {
  const script = readFile('test/scripts/test-desktop.js')
  assert.match(
    script,
    /RUNTIME_ENV_KEYS[\s\S]*ORO_TEST_FIXTURES_DIR[\s\S]*ORO_TEST_GREP/,
    'fixtures and test filters should be embedded in the runtime environment'
  )

  const cli = readFile('src/cli/main.cc')
  assert.match(
    cli,
    /mergeEnvironmentAssignments\([\s\S]*configDocument,[\s\S]*envAssignments/,
    'CLI environment overrides should merge with an existing [env] table'
  )
  assert.match(
    cli,
    /value\.find\('='\)[\s\S]*Vector<String> \{value\}/,
    'explicit CLI environment assignments should preserve whitespace in values'
  )

  assert.match(
    script,
    /'no-strict': \{ type: 'boolean' \}/,
    'documented negative desktop runner flags should be accepted'
  )
})

test('quick desktop tests refresh sources in reused workdirs', () => {
  const script = readFile('test/scripts/test-desktop.js')
  assert.match(
    script,
    /else \{\s+copyTestApp\(workdir\)\s+log\(`workdir sources updated/,
    'quick runs should copy new and changed test sources into reused workdirs'
  )
  assert.match(
    script,
    /!\['build', 'node_modules', 'tmp'\]\.includes\(topLevel\)/,
    'source synchronization should preserve staged build and scratch outputs'
  )
  assert.match(
    script,
    /Test entry does not exist:/,
    'missing staged entries should produce an actionable error'
  )
  assert.match(
    script,
    /entry\.startsWith\('\.\/src\/'\).*entry\.slice\('\.\/src\/'.length\)/,
    'source-relative entry paths should normalize to packaged resource paths'
  )
})

test('Rust build outputs honor caller target directories without contaminating cr-sqlite', () => {
  const script = readFile('bin/install.sh')
  assert.match(
    script,
    /cargo_target_dir="\$\{CARGO_TARGET_DIR:-\$crate_dir\/target\}"/,
    'oro-iroh artifacts should be located from CARGO_TARGET_DIR'
  )
  assert.match(
    script,
    /CARGO_TARGET_DIR="\$crsqlite_cargo_target_dir" quiet make/,
    'cr-sqlite should use its vendored target directory'
  )
})

test('Cargo license collection is locked, offline, and target-aware', () => {
  const script = readFile('bin/collect-cargo-licenses.js')
  assert.match(script, /'--locked'/, 'Cargo metadata should honor Cargo.lock')
  assert.match(script, /'--offline'/, 'license collection should not use the network')
  assert.match(
    script,
    /'--filter-platform',[\s\S]*targetTriple/,
    'license collection should inventory the selected host dependency graph'
  )
})

test('CI covers every supported host and mobile target family', () => {
  const workflow = readFile('.github/workflows/ci.yml')
  assert.match(
    workflow,
    /on:[\s\S]*workflow_call:/,
    'the signed-tag release chain should be able to depend on the exact CI matrix'
  )
  assert.match(
    workflow,
    /Validate generated documentation and declarations are committed[\s\S]*git diff --exit-code/,
    'CI should reject stale generated release output'
  )
  for (const runner of [
    'ubuntu-24.04',
    'ubuntu-24.04-arm',
    'macos-15-intel',
    'macos-14',
    'windows-2022'
  ]) {
    assert.match(
      workflow,
      new RegExp(runner.replaceAll('.', '\\.')),
      `CI should include ${runner}`
    )
  }
  assert.match(
    workflow,
    /Android x86_64 \+ arm64-v8a[\s\S]*build_android: true[\s\S]*test_android: true/,
    'CI should cross-build and exercise both supported Android ABIs on Linux x64'
  )
  assert.match(
    workflow,
    /android\)[\s\S]*arm64-v8a-android x86_64-android[\s\S]*ios\)[\s\S]*arm64-iPhoneOS x86_64-iPhoneSimulator[\s\S]*arm64-iPhoneSimulator/,
    'CI should fail if a promised Android ABI or iOS device/simulator target is missing'
  )
  assert.match(
    workflow,
    /macOS \+ iOS x64[\s\S]*macOS \+ iOS arm64/,
    'CI should build and test Apple hosts and iOS from Intel and Apple Silicon runners'
  )
  assert.match(
    workflow,
    /npm run test:child-process/,
    'CI should execute the focused child process suite'
  )
  assert.match(
    workflow,
    /npm run test:mcp/,
    'CI should execute the MCP protocol suites'
  )
  assert.match(
    workflow,
    /npm run test:android-emulator/,
    'CI should exercise Android in an emulator'
  )
  assert.match(
    workflow,
    /npm run test:ios-simulator/,
    'CI should exercise iOS in a simulator'
  )
  assert.match(
    workflow,
    /Validate staged target families \(Windows\)/,
    'Windows CI should reject unexpected mobile artifacts'
  )
})

test('test runners resolve the host architecture instead of assuming x64', () => {
  const resolver = readFile('test/scripts/oroc-path.js')
  assert.match(
    resolver,
    /case 'x64':[\s\S]*return 'x86_64'[\s\S]*case 'arm64':[\s\S]*return 'arm64'/,
    'the shared test resolver should map Node host architectures to runtime build directories'
  )

  for (const filename of [
    'test/scripts/test-desktop.js',
    'test/scripts/test-runtime-core.js',
    'test/scripts/test-ios-simulator.js',
    'test/scripts/test-android.js',
    'test/scripts/test-desktop-autoindex.js',
    'test/scripts/test-desktop-spa.js'
  ]) {
    assert.match(
      readFile(filename),
      /resolveOrocExecutable/,
      `${filename} should use the shared host-aware CLI resolver`
    )
  }

  assert.match(
    readFile('test/scripts/test-runtime-core.js'),
    /result\.status \?\? \(result\.error \? 1 : 0\)/,
    'runtime-core should fail when the CLI process cannot be spawned'
  )
  assert.match(
    readFile('test/scripts/test-ios-simulator.js'),
    /fixturesReady[\s\S]*child\.once\('exit'[\s\S]*process\.exitCode/,
    'iOS Simulator tests should fail when fixture setup or the child process fails'
  )
  assert.match(
    readFile('test/scripts/test-android.js'),
    /execFileSync\([\s\S]*cli,[\s\S]*'--platform=android'/,
    'Android tests should preserve the CLI path and argument boundaries without a shell command string'
  )
})
