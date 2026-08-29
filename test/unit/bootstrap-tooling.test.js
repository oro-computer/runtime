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

test('functions.sh discovers only compiler executables', () => {
  const script = readFile('bin/functions.sh')
  const npmRuntime = readFile('npm/src/index.js')
  const firstTimeSetup = script.match(
    /function first_time_experience_setup\(\) \{([\s\S]*?)\n\}/
  )?.[1]
  assert.match(
    script,
    /for compiler in clang\+\+ clang\+\+-\{26\.\.14\} g\+\+; do[\s\S]*command -v "\$compiler"[\s\S]*! -f "\$compiler_path"[\s\S]*! -x "\$compiler_path"/,
    'compiler discovery should resolve executable compiler commands instead of package-owned files'
  )
  assert.match(
    script,
    /ROCm ships a private LLVM toolchain[\s\S]*\/opt\/rocm/,
    'ROCm compilers should require an explicit CXX selection'
  )
  assert.match(
    script,
    /function linux_build_dependencies_available\(\)[\s\S]*command -v pkg-config[\s\S]*-std=c\+\+20[\s\S]*pkg-config --exists "\$dependency"[\s\S]*if linux_build_dependencies_available; then[\s\S]*Linux build dependencies are already installed/,
    'Linux first-time setup should install packages only when build capabilities are missing'
  )
  assert.doesNotMatch(
    script,
    /dpkg -S clang/,
    'compiler discovery must not execute paths returned by package metadata'
  )
  assert.ok(firstTimeSetup, 'first-time setup should exist')
  assert.equal(
    firstTimeSetup.match(/determine_cxx/g)?.length,
    1,
    'first-time setup should resolve the compiler once, after dependency setup'
  )
  assert.ok(
    firstTimeSetup.indexOf('determine_cxx') >
      firstTimeSetup.indexOf('Installing $(host_os) dependencies...'),
    'compiler resolution should not persist configuration before dependency setup completes'
  )
  assert.match(
    npmRuntime,
    /if \(exitCode !== 0\) \{[\s\S]*fs\.unlinkSync\(preferredEnvPath\)[\s\S]*Oro dependency setup failed/,
    'failed dependency setup should not leave a configuration file that suppresses the next setup attempt'
  )
  assert.match(
    script,
    /case "\$arg" in[\s\S]*--fte\)[\s\S]*first_time_experience_setup "\$@"[\s\S]*return \$\?[\s\S]*--update-env-data\)[\s\S]*update_env_data "\$@"[\s\S]*return \$\?/,
    'the functions dispatcher should return the selected operation status instead of a later condition status'
  )
  assert.doesNotMatch(
    script,
    /\[\[ "\$arg" == "--fte" \]\] &&[\s\S]*\[\[ "\$arg" == "--update-env-data" \]\]/,
    'successful first-time setup must not be overwritten by a false update-env-data condition'
  )
})

test('publish-npm-modules.sh stages Oro CLI packages', () => {
  const script = readFile('bin/publish-npm-modules.sh')
  assert.match(
    script,
    /CLI_PACKAGE_SPECS=\([\s\S]*"@oro-computer:runtime"[\s\S]*\)/,
    'publish-npm-modules.sh should stage @oro-computer/runtime'
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
    /reuse_release_artifacts:[\s\S]*default: false[\s\S]*runtime_artifact_id: linux-x64-android-sdk/,
    'standalone packaging should rebuild by default while release calls can reuse verified runtimes'
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
    4,
    'source-build, artifact-reuse, Windows, and top-level branches should produce inspectable tarballs before publication'
  )
  assert.match(
    workflow,
    /Download verified release runtime distribution[\s\S]*Stage verified release runtime \(Unix\)[\s\S]*--strip-components=1[\s\S]*Pack verified release runtime \(Unix\)[\s\S]*--no-rebuild --no-remove-oro-home/,
    'tag publication should package the already-built release runtime instead of compiling it again'
  )
  assert.match(
    workflow,
    /Stage verified release runtime \(Windows\)[\s\S]*Expand-Archive[\s\S]*ORO_NPM_STAGING_HOME/,
    'Windows tag publication should stage the verified release archive before packing'
  )
  assert.match(
    workflow,
    /compression-level: 0[\s\S]*retention-days: 14/,
    'precompressed npm tarballs should avoid redundant artifact compression and long-lived staging storage'
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
    /uses: \.\/\.github\/workflows\/publish-npm\.yml[\s\S]*publish: true[\s\S]*reuse_release_artifacts: true/,
    'a signed release tag should publish npm packages from the verified runtime artifacts'
  )
  assert.match(
    workflow,
    /Upload workflow artifact[\s\S]*compression-level: 0[\s\S]*retention-days: 14/,
    'release artifacts should avoid recompressing archives and expire workflow staging copies'
  )
  assert.match(
    workflow,
    /verify-release:[\s\S]*uses: \.\/\.github\/workflows\/ci\.yml[\s\S]*build-release-artifacts:[\s\S]*needs: \[validate-release, verify-release\]/,
    'release builds should wait for reusable source validation'
  )
  assert.match(
    workflow,
    /artifact_matrix: \$\{\{ steps\.matrix\.outputs\.artifact_matrix \}\}[\s\S]*Select release artifact runners[\s\S]*matrix: \$\{\{ fromJSON\(needs\.validate-release\.outputs\.artifact_matrix\) \}\}/,
    'a targeted manual release should allocate only its selected artifact runner'
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
})

test('runtime-artifacts.sh defines Oro artifact name', () => {
  const script = readFile('bin/runtime-artifacts.sh')
  assert.match(
    script,
    /ORO_RUNTIME_ARTIFACT_NAME:=oro-runtime/,
    'runtime artifacts should default to oro-runtime naming'
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
  assert.match(
    script,
    /if ! ln "\$obj" "\$dst"[\s\S]*cp -f "\$obj" "\$dst"/,
    'archive staging should avoid object-byte copies where hard links are supported'
  )
  assert.match(
    script,
    /\$ar crs[\s\S]*archive_rc=\$\?[\s\S]*rm -rf "\$stage_dir"/,
    'archive staging should be removed after both successful and failed archive attempts'
  )
})

test('build cleanup separates disposable staging from expensive caches', () => {
  const script = readFile('bin/clean.sh')
  assert.match(
    script,
    /--staging[\s\S]*--cache[\s\S]*--dry-run/,
    'cleanup should expose explicit staging, cache, and inspection modes'
  )
  assert.match(
    script,
    /--help[\s\S]*usage[\s\S]*unknown option/,
    'cleanup should describe supported modes and reject unknown destructive input'
  )
  assert.match(
    script,
    /-name \.archive_objects[\s\S]*llama\/build\/bin/,
    'staging cleanup should include runtime archive copies and disabled Llama outputs'
  )
  assert.match(
    script,
    /rust\/oro-iroh\/target[\s\S]*libipfs\/\.gocache[\s\S]*bundle_static\/target/,
    'cache cleanup should include the large Rust and legacy Go build caches'
  )
  assert.match(
    script,
    /if \(\( dry_run \)\)[\s\S]*would clean/,
    'dry-run cleanup should report targets without removing them'
  )
  assert.match(
    script,
    /elif \(\( do_clean_env_only \)\); then\s+:/,
    'environment-only cleanup should preserve compiled targets and caches'
  )
  assert.match(
    script,
    /if \(\( do_clean_llama \)\)[\s\S]*llama\/build[\s\S]*if \(\( ! do_clean_llama \)\); then/,
    'targeted Llama cleanup should not discard unrelated runtime outputs'
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
    /RUNTIME_ENV_KEYS[\s\S]*ORO_TEST_FIXTURES_DIR[\s\S]*ORO_TEST_SKIP_TEST_EXTENSIONS[\s\S]*ORO_TEST_GREP/,
    'fixtures, extension controls, and test filters should be embedded in the runtime environment'
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
  const crsqliteCompiler = script.match(
    /function _compile_crsqlite_loadable \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(
    crsqliteCompiler,
    'the cr-sqlite loadable-extension compiler function should exist'
  )
  assert.match(
    script,
    /cargo_target_dir="\$\{CARGO_TARGET_DIR:-\$crate_dir\/target\}"/,
    'oro-iroh artifacts should be located from CARGO_TARGET_DIR'
  )
  assert.match(
    script,
    /ORO_PRUNE_IROH_BUILD_OUTPUTS:-false[\s\S]*cargo clean --target-dir "\$cargo_target_dir"/,
    'mobile CI should be able to reclaim oro-iroh outputs after staging the library'
  )
  assert.match(
    script,
    /function _crsqlite_rust_toolchain[\s\S]*rust-toolchain\.toml[\s\S]*parsed_toolchain/,
    'cr-sqlite cleanup should derive the pinned toolchain from its checked-in configuration'
  )
  assert.match(
    script,
    /pruning transient Android Rust build outputs[\s\S]*bundle_static\/target[\s\S]*rustup toolchain uninstall "\$crsqlite_rust_toolchain"/,
    'Android CI should prune cr-sqlite intermediates after the final ABI is staged'
  )
  assert.match(
    script,
    /_install_cli[\s\S]*ORO_PRUNE_BUILD_OUTPUTS_AFTER_INSTALL:-false[\s\S]*"\$ORO_HOME" == "\$BUILD_DIR\/"\*[\s\S]*rm -rf -- "\$BUILD_DIR"/,
    'Android CI should reclaim the source build tree only after staged artifacts are installed'
  )
  assert.match(
    crsqliteCompiler,
    /CARGO_TARGET_DIR="\$crsqlite_cargo_target_dir" quiet make/,
    'cr-sqlite should use its vendored target directory'
  )
  assert.match(
    crsqliteCompiler,
    /Win32[\s\S]*CI_GCC=clang[\s\S]*LOADABLE_CFLAGS=-std=c99 -shared -Wall[\s\S]*rs_lib_loadable=\.\/rs\/bundle_static\/target\/release\/crsql_bundle_static\.lib[\s\S]*CARGO_TARGET_DIR="\$crsqlite_cargo_target_dir" quiet make "\$\{crsqlite_make_args\[@\]\}"/,
    'Windows cr-sqlite builds should use MSVC-compatible Clang flags and the MSVC static-library filename'
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

test('iOS llama cross-builds do not probe host BLAS', () => {
  const installer = readFile('bin/install.sh')
  const llamaCompiler = installer.match(
    /function _compile_llama \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(llamaCompiler, 'the llama compiler function should exist')

  const iosCompiler = llamaCompiler.match(
    /elif \[ "\$platform" == "iPhoneOS" \] \|\| \[ "\$platform" == "iPhoneSimulator" \]; then([\s\S]*?)elif \[ "\$platform" == "android" \]; then/
  )?.[1]
  assert.ok(iosCompiler, 'the iOS llama compiler branch should exist')
  assert.match(
    iosCompiler,
    /-DGGML_BLAS=OFF/,
    'iOS llama builds should disable the host-oriented BLAS backend'
  )
})

test('Apple prebuilds restore host tools and select target headers', () => {
  const installer = readFile('bin/install.sh')
  const functions = readFile('bin/functions.sh')
  const iosPrebuild = installer.match(
    /function _prebuild_ios_main \(\) \{([\s\S]*?)\n\}/
  )?.[1]

  assert.ok(iosPrebuild, 'the iOS main prebuild function should exist')
  assert.match(
    iosPrebuild,
    /TARGET_OS_IPHONE=1 ARCH="\$arch" "\$root\/bin\/cflags\.sh"/,
    'iOS device flags should select arm64 staged dependency headers on Intel hosts'
  )
  assert.match(
    installer,
    /unset PLATFORM CC CXX[\s\S]*determine_cxx \|\| exit \$\?/,
    'the host compiler should be restored after iOS dependency builds'
  )
  assert.match(
    functions,
    /function die \{[\s\S]*if \(\( status != 0 \)\); then/,
    'fatal build commands should reject command-not-found status 127'
  )
})

test('Apple runtime sources exclude APIs unavailable to iOS builds', () => {
  const hidBackend = readFile(
    'src/runtime/core/services/hid/macos_backend.mm'
  )
  const xpcBackend = readFile('src/runtime/core/services/xpc.mm')
  const cookies = readFile('src/runtime/webview/cookies.cc')
  const process = readFile('src/runtime/process/unix.cc')

  assert.match(
    hidBackend,
    /#include <TargetConditionals\.h>[\s\S]*#if defined\(__APPLE__\) && !TARGET_OS_IPHONE/,
    'the IOKit HID backend should compile only for macOS'
  )
  assert.match(
    xpcBackend,
    /#if TARGET_OS_IPHONE[\s\S]*handle = xpc_connection_create\([\s\S]*#else[\s\S]*xpc_connection_create_mach_service/,
    'iOS XPC builds should not reference the unavailable Mach-service constructor'
  )
  assert.match(
    cookies,
    /\.secure = static_cast<bool>\(nsCookie\.isSecure\)[\s\S]*\.httpOnly = static_cast<bool>\(nsCookie\.isHTTPOnly\)/,
    'Objective-C BOOL cookie properties should be explicitly converted to C++ bool'
  )
  assert.match(
    process,
    /#if defined\(__APPLE__\)[\s\S]*unsetenv\(name\.c_str\(\)\)[\s\S]*#else[\s\S]*clearenv\(\)/,
    'Apple process launches should clear inherited variables without unavailable clearenv'
  )
})

test('desktop Unix process launch avoids GUI atfork handlers', () => {
  const process = readFile('src/runtime/process/unix.cc')

  assert.match(
    process,
    /#elif defined\(__ANDROID__\)[\s\S]*return open\(\[&command, &path, this\][\s\S]*#else[\s\S]*posix_spawnp\(/,
    'desktop Unix should use posix_spawnp while Android retains its supported fork path'
  )
  assert.match(
    process,
    /posix_spawn_file_actions_addchdir_np[\s\S]*POSIX_SPAWN_SETPGROUP/,
    'spawned desktop processes should retain cwd and process-group behavior'
  )
})

test('llama dependency builds omit standalone utilities and command-line tools', () => {
  const installer = readFile('bin/install.sh')
  const llamaCompiler = installer.match(
    /function _compile_llama \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(llamaCompiler, 'the llama compiler function should exist')
  assert.match(
    llamaCompiler,
    /-DLLAMA_BUILD_TOOLS=OFF/,
    'dependency builds should not compile unused llama command-line tools'
  )
  assert.match(
    llamaCompiler,
    /-DLLAMA_BUILD_COMMON=OFF/,
    'dependency builds should not compile the unused llama common utility library'
  )
  assert.match(
    installer,
    /function _prune_disabled_llama_outputs[\s\S]*build\/bin[\s\S]*build\/common[\s\S]*build\/tools[\s\S]*build\/vendor/,
    'disabled Llama targets should not retain stale CMake outputs'
  )
})

test('CMake cache invalidation tracks build inputs without forcing legacy caches cold', () => {
  const installer = readFile('bin/install.sh')
  assert.match(
    installer,
    /function _cmake_configuration_signature[\s\S]*schema=oro-cmake-v1[\s\S]*CFLAGS=%q[\s\S]*arg=%q/,
    'CMake fingerprints should include the source, toolchain flags, and configure arguments'
  )
  assert.match(
    installer,
    /-f "\$cache_file"[\s\S]*-f "\$configuration_file"[\s\S]*!= "\$configuration"[\s\S]*refresh=1/,
    'only previously fingerprinted caches should be refreshed after a configuration change'
  )
})

test('fresh Android libuv builds do not fail after successful compilation', () => {
  const installer = readFile('bin/install.sh')
  const libuvCompiler = installer.match(
    /function _compile_libuv_android \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(libuvCompiler, 'the Android libuv compiler function should exist')
  assert.match(
    libuvCompiler,
    /rm -f -- "\$static_library"/,
    'removing an absent stale archive should leave the compile worker successful'
  )
})

test('Windows libusb builds use the upstream Visual Studio project', () => {
  const installer = readFile('bin/install.sh')
  const libusbCompiler = installer.match(
    /function _compile_libusb \{([\s\S]*?)\n\}/
  )?.[1]

  assert.ok(libusbCompiler, 'the libusb compiler function should exist')
  assert.match(
    libusbCompiler,
    /msvc\/libusb_static\.vcxproj[\s\S]*command -v MSBuild\.exe[\s\S]*-p:Configuration=\$config[\s\S]*-p:Platform=\$msbuild_platform/,
    'Windows builds should compile the project shipped by the pinned libusb source'
  )
  assert.match(
    libusbCompiler,
    /env "_CL_=\$\{_CL_:\+\$_CL_ \}-wd5287" MSBuild\.exe/,
    'the pinned libusb build should pass an MSYS-safe enum warning suppression to cl.exe'
  )
  assert.doesNotMatch(
    libusbCompiler,
    /DisableSpecificWarnings/,
    'Windows builds should not rely on an MSBuild property that the upstream project ignores'
  )
  assert.doesNotMatch(
    libusbCompiler,
    /cmake -S \.\. -B \./,
    'Windows builds should not require a top-level CMake project that libusb does not ship'
  )
})

test('verbose non-interactive builds retain detected CPU parallelism', () => {
  const functions = readFile('bin/functions.sh')
  const cpuSelector = functions.match(
    /function set_cpu_cores\(\) \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(cpuSelector, 'the CPU core selector should exist')
  assert.doesNotMatch(
    cpuSelector,
    /VERBOSE[\s\S]*! -t 1[\s\S]*CPU_CORES=1/,
    'verbose CI and npm builds should not be serialized to one CPU'
  )
  assert.match(
    cpuSelector,
    /CPU_CORES < 1[\s\S]*CPU_CORES=1/,
    'invalid CPU detection should still fall back to one core'
  )
})

test('runtime target builds share the detected CPU budget', () => {
  const installer = readFile('bin/install.sh')
  const runtimeBuilder = readFile('bin/build-runtime-library.sh')
  const runtimeOrchestrator = installer.match(
    /function _build_runtime_library\(\) \{([\s\S]*?)\n\}/
  )?.[1]

  assert.ok(
    runtimeOrchestrator,
    'the runtime target orchestrator should exist'
  )
  assert.match(
    runtimeOrchestrator,
    /CPU_CORES \/ runtime_target_count[\s\S]*ORO_RUNTIME_BUILD_JOBS="\$target_jobs"/,
    'parallel target families should divide the detected CPU budget'
  )
  assert.match(
    runtimeBuilder,
    /ORO_RUNTIME_BUILD_JOBS:-\$CPU_CORES[\s\S]*pids\[@\]\} >= max_concurrency/,
    'each runtime builder should enforce its assigned compile-job budget'
  )
  assert.doesNotMatch(
    runtimeBuilder,
    /2 \* max_concurrency/,
    'runtime object compilation should not oversubscribe its assigned budget'
  )
  assert.match(
    runtimeBuilder,
    /compile_status[\s\S]*failed to compile runtime objects/,
    'runtime compiler subprocess failures should propagate to the installer'
  )
})

test('CI shards Apple-mobile builds without changing installer defaults', () => {
  const installer = readFile('bin/install.sh')
  const workflow = readFile('.github/workflows/ci.yml')
  const appleTargetSelector = installer.match(
    /function _configure_apple_mobile_targets\(\) \{([\s\S]*?)\n\}/
  )?.[1]

  assert.ok(
    appleTargetSelector,
    'the Apple-mobile target selector should exist'
  )
  assert.match(
    appleTargetSelector,
    /ORO_CI_APPLE_MOBILE_TARGETS[\s\S]*arm64-iPhoneOS x86_64-iPhoneSimulator[\s\S]*arm64-iPhoneSimulator/,
    'normal source builds should retain the complete host-eligible Apple target set'
  )
  assert.match(
    installer,
    /function _build_runtime_library\(\)[\s\S]*for apple_target in "\$\{apple_mobile_targets\[@\]\}"[\s\S]*runtime_arches\+=/,
    'runtime compilation should use the selected Apple target shard'
  )
  assert.match(
    installer,
    /for apple_target in "\$\{apple_mobile_targets\[@\]\}"; do[\s\S]*_queue_target_dependency "llama \(\$apple_target\)"/,
    'Apple dependency builds should use the same selected target shard'
  )
  assert.match(
    workflow,
    /macOS \+ iOS x64[\s\S]*apple_mobile_targets: x86_64-iPhoneSimulator[\s\S]*macOS \+ iOS arm64[\s\S]*apple_mobile_targets: arm64-iPhoneOS arm64-iPhoneSimulator/,
    'Intel and Apple Silicon CI should not rebuild each other\'s mobile targets'
  )
  assert.match(
    workflow,
    /read -r -a required_ios_targets <<< "\$ORO_CI_APPLE_MOBILE_TARGETS"[\s\S]*Apple CI built an unassigned iOS target/,
    'CI should validate both missing and unexpectedly duplicated Apple targets'
  )
  assert.equal(
    workflow.match(/ccache --max-size 1G/g)?.length,
    2,
    'native CI lanes should retain enough compiler output to avoid cache churn'
  )
})

test('runtime metadata changes invalidate one cacheable object', () => {
  const cflags = readFile('bin/cflags.sh')
  const runtimeBuilder = readFile('bin/build-runtime-library.sh')
  const runtimeHeader = readFile('src/runtime/version.hh')
  const runtimeSource = readFile('src/runtime/version.cc')

  assert.match(
    cflags,
    /ORO_EXCLUDE_BUILD_METADATA[\s\S]*ORO_RUNTIME_BUILD_TIME[\s\S]*ORO_RUNTIME_VERSION_HASH/,
    'shared compiler flags should support excluding volatile build metadata'
  )
  assert.match(
    runtimeBuilder,
    /ORO_EXCLUDE_BUILD_METADATA=1[\s\S]*runtime_metadata_cflags[\s\S]*source" == "\$root\/src\/runtime\/version\.cc"[\s\S]*compile_flags\+=\("\$\{runtime_metadata_cflags\[@\]\}"\)/,
    'runtime libraries should apply revision metadata only to version.cc'
  )
  assert.match(
    runtimeHeader,
    /extern const String VERSION_FULL_STRING[\s\S]*extern const String VERSION_HASH_STRING[\s\S]*extern const String VERSION_STRING/,
    'runtime consumers should reference one metadata-bearing translation unit'
  )
  assert.match(
    runtimeSource,
    /VERSION_FULL_STRING[\s\S]*ORO_RUNTIME_VERSION_HASH[\s\S]*VERSION_STRING/,
    'version.cc should retain full, hash, and semantic runtime version reporting'
  )
})

test('parallel Android installs propagate every ABI failure', () => {
  const installer = readFile('bin/install.sh')
  assert.match(
    installer,
    /runtime install android \(\$abi\)[\s\S]*_wait_for_target_dependencies "not ok - Android runtime install failed"/,
    'Android staging should wait for and validate each ABI-specific installer'
  )
  assert.doesNotMatch(
    installer,
    /_install "\$abi" android[\s\S]{0,160}\n\s*wait\s*\n/,
    'Android staging should not use a bare wait that discards child status'
  )
})

test('mobile dependency builders share the detected CPU budget', () => {
  const installer = readFile('bin/install.sh')
  const scheduler = installer.match(
    /function _queue_target_dependency \(\) \{([\s\S]*?)\n\}/
  )?.[1]

  assert.ok(scheduler, 'the target dependency scheduler should exist')
  assert.match(
    scheduler,
    /CPU_CORES >= 4[\s\S]*builder_limit=2[\s\S]*builder_jobs=\$\(\( CPU_CORES \/ builder_limit \)\)[\s\S]*pids\[@\]\} >= builder_limit/,
    'typical CI runners should execute two dependency builders with divided compiler budgets'
  )
  assert.match(
    scheduler,
    /export CPU_CORES="\$builder_jobs"[\s\S]*export CARGO_BUILD_JOBS="\$builder_jobs"/,
    'nested Make, CMake, and Cargo builds should receive the divided budget'
  )
  assert.match(
    installer,
    /_queue_target_dependency "libwhisper desktop[\s\S]*_queue_target_dependency "llama \(\$apple_target\)"[\s\S]*_wait_for_target_dependencies "not ok - desktop or iOS dependency build failed"/,
    'Apple-mobile dependencies should run through the bounded scheduler'
  )
  assert.match(
    installer,
    /_queue_target_dependency "llama android \(\$abi\)"[\s\S]*_wait_for_target_dependencies "not ok - Android dependency build failed \(\$abi\)"/,
    'each Android ABI should complete its bounded dependency batch before the next ABI'
  )
})

test('native dependency builds do not perform serial work before parallel builds', () => {
  const installer = readFile('bin/install.sh')
  const llamaCompiler = installer.match(
    /function _compile_llama \{([\s\S]*?)\n\}/
  )?.[1]
  const libuvCompiler = installer.match(
    /function _compile_libuv \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(llamaCompiler, 'the llama compiler function should exist')
  assert.ok(libuvCompiler, 'the libuv compiler function should exist')
  assert.doesNotMatch(
    llamaCompiler,
    /cmake --build build &&[\s\S]*cmake --build build -- -j"\$CPU_CORES"/,
    'llama should not complete a serial build before requesting parallel work'
  )
  assert.doesNotMatch(
    libuvCompiler,
    /quiet make\s+die \$\? "not ok - libuv desktop make"[\s\S]*quiet make "-j\$CPU_CORES"/,
    'libuv should not complete a serial build before requesting parallel work'
  )
  assert.match(
    installer,
    /cmake --build \. --config \$config --parallel "\$CPU_CORES"/,
    'Windows CMake builds should use detected CPU parallelism'
  )
  assert.match(
    installer,
    /MSBuild\.exe[\s\S]*"-m:\$CPU_CORES"/,
    'Windows MSBuild projects should use detected CPU parallelism'
  )
})

test('CI covers every supported host and mobile target family', () => {
  const workflow = readFile('.github/workflows/ci.yml')
  assert.match(
    workflow,
    /on:[\s\S]*workflow_call:/,
    'the signed-tag release chain should be able to reuse the CI validation workflow'
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
    /android\)[\s\S]*arm64-v8a-android x86_64-android[\s\S]*ios\)[\s\S]*required_ios_targets[\s\S]*Apple CI is missing required iOS target/,
    'CI should fail if an assigned Android ABI or iOS target is missing'
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

test('CI caches dependencies and runs focused platform coverage', () => {
  const workflow = readFile('.github/workflows/ci.yml')
  const releaseWorkflow = readFile('.github/workflows/release-artifacts.yml')
  const publishWorkflow = readFile('.github/workflows/publish-npm.yml')
  const workspace = readFile('pnpm-workspace.yaml')

  assert.match(
    workspace,
    /allowBuilds:[\s\S]*esbuild: true[\s\S]*puppeteer: true/,
    'pnpm should explicitly allow the build steps required by local tooling'
  )
  assert.doesNotMatch(
    workspace,
    /onlyBuiltDependencies/,
    'pnpm 11 should not use the removed dependency build policy'
  )
  assert.match(
    workflow,
    /pnpm\/setup@[0-9a-f]{40} # v2\.0\.2[\s\S]*runtime: node@24[\s\S]*cache: true[\s\S]*install: false/,
    'CI should use the current pnpm setup action and cache its content-addressed store'
  )
  assert.match(
    publishWorkflow,
    /pnpm\/setup@[0-9a-f]{40} # v2\.0\.2[\s\S]*cache: true[\s\S]*install: false/,
    'npm packaging should use the same cached pnpm setup action without installing implicitly'
  )
  assert.match(
    workflow,
    /PUPPETEER_SKIP_DOWNLOAD: 'true'/,
    'CI should not download a browser for jobs that do not render diagrams'
  )
  assert.match(
    workflow,
    /cache: pip[\s\S]*\.github\/requirements-lint\.txt/,
    'CI should cache the pinned Python lint dependency'
  )
  assert.match(
    workflow,
    /Restore native compiler cache[\s\S]*ccache-v1-/,
    'native Unix builds should restore compiler output caches'
  )
  for (const nativeWorkflow of [workflow, releaseWorkflow, publishWorkflow]) {
    assert.match(
      nativeWorkflow,
      /native-cache[\s\S]*github\.run_attempt[\s\S]*Save native compiler cache[\s\S]*!cancelled\(\)[\s\S]*native-cache\.outcome == 'success'/,
      'native compiler caches should retain non-cancelled failed-attempt work under an immutable attempt key'
    )
  }
  assert.match(
    workflow,
    /restore-keys:[\s\S]*ccache-v1-\$\{\{ runner\.os \}\}-\$\{\{ runner\.arch \}\}-\s/,
    'debug lanes should reuse matching compiler objects across platform families'
  )
  assert.match(
    releaseWorkflow,
    /release-ccache-v1-[^\n]*needs\.validate-release\.outputs\.commit_sha[\s\S]*release-ccache-v1-\$\{\{ runner\.os \}\}-\$\{\{ runner\.arch \}\}-\s/,
    'release caches should bind to the validated commit and share matching production objects'
  )
  assert.match(
    publishWorkflow,
    /release-ccache-v1-[^\n]*matrix\.runtime_artifact_id/,
    'standalone npm packaging should fall back to the corresponding release compiler cache'
  )
  assert.match(
    workflow,
    /linux-integration:[\s\S]*Restore Rust dependency cache[\s\S]*test-platform:/,
    'Linux integration should cache Rust registry and Git dependencies'
  )
  assert.doesNotMatch(
    `${workflow}\n${releaseWorkflow}\n${publishWorkflow}`,
    /Restore Rust dependency cache\s+if: [^\n]*build_android/,
    'Rust dependency caches should apply to desktop and mobile native builds'
  )
  assert.match(
    workflow,
    /Restore Go build cache[\s\S]*\.cache\/go-build[\s\S]*\.cache\/go-mod[\s\S]*hashFiles\('bin\/install\.sh'\)/,
    'native CI should preserve Go build and module caches across runs'
  )
  for (const nativeWorkflow of [workflow, releaseWorkflow, publishWorkflow]) {
    assert.match(
      nativeWorkflow,
      /Setup Windows native compiler cache[\s\S]*mozilla-actions\/sccache-action@[0-9a-f]{40} # v0\.0\.11[\s\S]*version: v0\.17\.0[\s\S]*RUSTC_WRAPPER=sccache[\s\S]*CMAKE_CXX_COMPILER_LAUNCHER=sccache/,
      'Windows native builds should cache C, C++, and Rust compiler outputs'
    )
    assert.match(
      nativeWorkflow,
      /brew install automake ccache libomp libtool pkg-config/,
      'macOS builds should install the OpenMP runtime staged with desktop artifacts'
    )
  }
  assert.match(
    readFile('bin/install.sh'),
    /GOCACHE:-\$root\/\.cache\/go-build\/\$goos-\$goarch[\s\S]*GOMODCACHE:-\$root\/\.cache\/go-mod/,
    'libipfs should build against workflow-cacheable Go directories'
  )
  assert.match(
    workflow,
    /cache: gradle[\s\S]*bin\/android-functions\.sh/,
    'Android builds should restore Gradle dependencies using the pinned toolchain inputs'
  )
  assert.match(
    workflow,
    /Restore Android build SDK cache[\s\S]*ndk\/29\.0\.14206865[\s\S]*platforms\/android-37\*/,
    'Android builds should cache the pinned NDK and SDK platform'
  )
  assert.match(
    workflow,
    /Restore Android emulator cache[\s\S]*system-images\/android-37\.0\/google_apis\/x86_64[\s\S]*~\/\.android\/avd/,
    'the Android test lane should cache its emulator image and AVD separately'
  )
  assert.match(
    workflow,
    /Reclaim Android build disk[\s\S]*\/usr\/share\/dotnet[\s\S]*\$ANDROID_HOME\/system-images[\s\S]*Build Oro Runtime CLI \(Unix\)/,
    'the Android lane should remove unused hosted toolchains and stale emulator images before compiling'
  )
  assert.match(
    workflow,
    /Build Oro Runtime CLI \(Unix\)[\s\S]*Restore Android emulator cache[\s\S]*Run Android emulator tests/,
    'the Android emulator should be restored after native build outputs are staged and pruned'
  )
  assert.match(
    workflow,
    /ORO_PRUNE_BUILD_OUTPUTS_AFTER_INSTALL: \$\{\{ matrix\.test_android \}\}/,
    'only the Android emulator test lane should discard its installed source-build tree'
  )
  for (const nativeWorkflow of [workflow, releaseWorkflow, publishWorkflow]) {
    assert.match(
      nativeWorkflow,
      /ORO_PRUNE_IROH_BUILD_OUTPUTS: \$\{\{ matrix\.build_android \}\}/,
      'Android builds should reclaim staged oro-iroh Cargo outputs before ABI compilation'
    )
    assert.match(
      nativeWorkflow,
      /ORO_PRUNE_TRANSIENT_BUILD_OUTPUTS: \$\{\{ matrix\.build_android \}\}/,
      'Android builds should remove isolated cross-build outputs after staging both ABIs'
    )
  }
  assert.match(
    workflow,
    /ACTIONLINT_VERSION: [\d.]+[\s\S]*ACTIONLINT_SHA256: [0-9a-f]{64}[\s\S]*sha256sum --check --status/,
    'lint should verify the pinned actionlint release checksum'
  )
  assert.doesNotMatch(
    workflow,
    /go install github\.com\/rhysd\/actionlint/,
    'lint should download the pinned actionlint release instead of compiling it on every run'
  )
  assert.match(
    workflow,
    /linux-integration:[\s\S]*needs: \[changes, lint\][\s\S]*test-platform:[\s\S]*needs: \[changes, lint\]/,
    'expensive native jobs should not start before the static gate succeeds'
  )
  assert.match(
    workflow,
    /linux-integration:[\s\S]*timeout-minutes: 90[\s\S]*test-platform:[\s\S]*timeout-minutes: 120/,
    'CI should stop wedged native builds before they consume the three-hour runner maximum'
  )
  assert.doesNotMatch(
    `${workflow}\n${releaseWorkflow}\n${publishWorkflow}`,
    /timeout-minutes: 180/,
    'native CI and packaging jobs should not retain three-hour timeout budgets'
  )
  assert.match(
    workflow,
    /Linux arm64[\s\S]*install_tests: false[\s\S]*macOS \+ iOS x64[\s\S]*install_tests: false/,
    'architecture-only lanes should build and smoke test without installing the integration harness'
  )
  assert.equal(
    (workflow.match(/run: (?:dbus-run-session -- )?npm run test:mcp$/gm) || [])
      .length,
    1,
    'the platform-independent MCP suite should run only on the comprehensive Linux lane'
  )
  assert.equal(
    (workflow.match(
      /run: (?:dbus-run-session -- )?npm run test:runtime-core$/gm
    ) || []).length,
    1,
    'the runtime-core suite should run only on the comprehensive Linux lane'
  )
  assert.match(
    workflow,
    /Documentation-only changes skip native builds and integration tests/,
    'documentation-only changes should avoid native runner allocation'
  )
  assert.match(
    workflow,
    /commits\/\$CURRENT_SHA\/pulls[\s\S]*run_ci=false[\s\S]*lint:[\s\S]*needs: changes[\s\S]*needs\.changes\.outputs\.run_ci == 'true'/,
    'an open pull request should suppress its duplicate feature-branch push workflow'
  )
  assert.match(
    releaseWorkflow,
    /uses: \.\/\.github\/workflows\/ci\.yml[\s\S]*run_cross_platform: false/,
    'release validation should not rebuild every platform before the release artifact matrix'
  )
})

test('Android bootstrap separates build packages from emulator packages', () => {
  const bootstrap = readFile('bin/android-functions.sh')
  const generatedGradle = readFile('bin/generate-gradle-files.sh')
  const cli = readFile('src/cli/main.cc')
  const cliTemplates = readFile('src/cli/templates.hh')
  const emulatorBootstrap = readFile(
    'test/scripts/bootstrap-android-emulator.sh'
  )
  const emulatorTest = readFile('test/scripts/test-android-emulator.sh')

  assert.match(
    bootstrap,
    /ANDROID_COMMAND_LINE_TOOLS_VERSION="16111833"[\s\S]*JDK_VERSION="17\.0\.20\.1"[\s\S]*GRADLE_VERSION="9\.5\.0"[\s\S]*ANDROID_PLATFORM="26"[\s\S]*ANDROID_SDK_PLATFORM="37\.0"[\s\S]*NDK_VERSION="29\.0\.14206865"/,
    'source bootstrap should pin the current compatible Android toolchain'
  )

  const buildPackages = bootstrap.match(
    /SDK_OPTIONS=""([\s\S]*?)local yes=/
  )?.[1]
  assert.ok(buildPackages, 'Android build package selection should exist')
  assert.match(
    buildPackages,
    /cmdline-tools;\$ANDROID_COMMAND_LINE_TOOLS_PACKAGE_VERSION[\s\S]*ndk;\$NDK_VERSION[\s\S]*platforms;android-\$ANDROID_SDK_PLATFORM[\s\S]*build-tools;\$ANDROID_BUILD_TOOLS_VERSION/,
    'source builds should install only the pinned build SDK packages'
  )
  assert.doesNotMatch(
    buildPackages,
    /system-images|"emulator"/,
    'source builds should not install test-only emulator packages'
  )

  for (const source of [generatedGradle, cliTemplates]) {
    assert.match(source, /com\.android\.tools\.build:gradle:9\.3\.2/)
    assert.match(source, /compileSdk 37/)
    assert.match(source, /ndkVersion "29\.0\.14206865"/)
    assert.match(source, /JavaVersion\.VERSION_17/)
    assert.doesNotMatch(source, /kotlin-android/)
  }

  assert.match(
    cli,
    /if \(flagBuildForAndroidEmulator\) \{[\s\S]*system-images;[\s\S]*google_apis/,
    'application builds should request a system image only for the emulator target'
  )
  assert.match(
    emulatorBootstrap,
    /android_system_image_arch[\s\S]*OROAVD_API_[\s\S]*emulator_packages[\s\S]*system_image_dir[\s\S]*sdkmanager" "\$\{emulator_packages\[@\]\}"/,
    'emulator tests should install only missing host-compatible packages and use a versioned AVD'
  )
  assert.match(
    emulatorTest,
    /ORO_ANDROID_EMULATOR_SETUP_TIMEOUT_SECONDS:-600[\s\S]*kill -0 "\$bootstrap_pid"[\s\S]*if \[\[ "\$bootstrap_exit_code" != "0" \]\][\s\S]*exit "\$bootstrap_exit_code"/,
    'emulator setup failures should terminate predictably'
  )
  assert.match(
    emulatorTest,
    /ORO_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS:-300[\s\S]*Android Emulator failed to boot[\s\S]*exit "\$bootstrap_exit_code"[\s\S]*Android Emulator boot timed out/,
    'emulator boot failures and timeouts should terminate predictably'
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
