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
  assert.match(
    script,
    /archive_inputs="\$\(printf '%s\\n' "\$\{objects\[@\]\}"\)"[\s\S]*cat "\$archive_inputs_file"[\s\S]*build_static=1[\s\S]*printf '%s\\n' "\$archive_inputs" > "\$archive_inputs_file"/,
    'the static archive should be rebuilt when its selected object surface changes'
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

test('headless macOS launches use the supervised child process path', () => {
  const source = readFile('src/cli/main.cc')
  assert.match(
    source,
    /if \(platform\.mac && !headless\) \{[\s\S]*openApplicationAtURL:[\s\S]*#endif[\s\S]*appProcess = std::make_shared<Process>/,
    'headless macOS apps should bypass NSWorkspace so their output and exit status remain attached to the CLI'
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

  assert.match(
    readFile('test/src/extension.js'),
    /String\(process\.env\.ORO_TEST_SKIP_TEST_EXTENSIONS\) === '1'/,
    'extension tests should accept the numeric environment value decoded by the runtime configuration'
  )

  assert.match(
    readFile('api/test/index.js'),
    /requestAnimationFrame \(msg = null\)[\s\S]*Promise\.race\([\s\S]*globalThis\.requestAnimationFrame[\s\S]*setTimeout\(resolve, 50\)/,
    'DOM test helpers should not hang when a focused headless window suppresses animation frames'
  )
})

test('VM context windows use an explicit startup handshake', () => {
  const vm = readFile('api/vm.js')
  const ipc = readFile('api/ipc.js')
  const workerThreads = readFile('api/worker_threads.js')
  const vmWorker = readFile('api/vm/worker.js')
  const vmInit = readFile('api/vm/init.js')
  const vmWorld = readFile('api/vm/world.js')
  const sharedWorker = readFile('api/shared-worker/index.js')
  const sharedWorkerInit = readFile('api/shared-worker/init.js')
  const sharedWorkerRuntime = readFile('api/shared-worker/worker.js')

  for (const source of [vm, sharedWorker]) {
    assert.match(
      source,
      /let contextWindowRequest = null[\s\S]*if \(contextWindowRequest\) \{[\s\S]*return await contextWindowRequest[\s\S]*contextWindowRequest = request[\s\S]*contextWindowRequest === request/,
      'concurrent callers should share one context-window initialization request'
    )
    assert.match(
      source,
      /channel\.postMessage\(\{ probe: index \}\)[\s\S]*build_headless !== true/,
      'headless context windows should stay active after requesting their ready acknowledgement'
    )
    assert.doesNotMatch(
      source,
      /setTimeout\(resolve, 500\)/,
      'context startup should not treat an elapsed timer as a ready signal'
    )
  }

  for (const source of [vmInit, sharedWorkerInit]) {
    assert.match(
      source,
      /probe === currentWindow\.index[\s\S]*announceReady\(\)/,
      'context windows should answer readiness probes after initialization'
    )
  }

  assert.match(
    vm,
    /Promise\.all\(\[getContextWorker\(\), window\.ready\]\)[\s\S]*throw error/,
    'VM scripts should wait for both startup handshakes and propagate initialization errors'
  )
  assert.match(
    vm,
    /function waitForContextWorkerReady[\s\S]*if \(event\.data === VM_WORKER_ACK\)[\s\S]*worker\.port\.postMessage\(VM_WORKER_PROBE\)[\s\S]*worker\.port\.start\(\)[\s\S]*worker\.ready\.then\(\(\) => \{[\s\S]*setInterval\(probe, 250\)[\s\S]*VM Context SharedWorker did not acknowledge startup[\s\S]*10_000/,
    'the worker ACK timeout should start after initialization, actively probe the port, and ignore unrelated messages'
  )
  assert.match(
    vmWorker,
    /if \(event\.data === VM_WORKER_PROBE\) \{\s+port\.postMessage\(VM_WORKER_ACK\)/,
    'the VM worker should acknowledge active startup probes'
  )
  assert.doesNotMatch(
    vm,
    /port\.addEventListener\('message',[\s\S]{0,180}\{ once: true \}/,
    'unrelated worker messages should not consume the startup acknowledgement listener'
  )
  assert.match(
    vm,
    /async function awaitContextWorkerReady[\s\S]*catch \(error\)[\s\S]*contextWorker = null[\s\S]*closeContextWorker\(worker\)/,
    'failed VM worker startup should clear the cached worker and release its ports'
  )
  assert.match(
    vm,
    /worker\.port\.addEventListener\('message',[\s\S]*type === 'terminate-worker'[\s\S]*closeContextWorker\(worker\)/,
    'VM worker termination should be observed on the worker message port'
  )
  assert.doesNotMatch(
    vm,
    /contextWorker\.addEventListener\('message'/,
    'VM worker messages should not be observed on the SharedWorker wrapper'
  )
  assert.equal(
    (vm.match(/worker\.port\.postMessage\([\s\S]*?\n\s*transfer\n\s*\)/g) || [])
      .length,
    2,
    'VM context messages should use the transferable-list form supported by native MessagePorts'
  )
  assert.doesNotMatch(
    [vm, ipc, workerThreads].join('\n'),
    /\.postMessage\([\s\S]{0,300}\{ transfer \}\s*\)/,
    'native Worker and MessagePort calls should not use unsupported structured-clone option dictionaries'
  )
  assert.match(
    ipc,
    /const nativeTransfers = options\.transfer\.filter\([\s\S]*?entry instanceof IPCMessagePort[\s\S]*?tx\.postMessage\([\s\S]*?nativeTransfers\n\s*\)/,
    'IPC MessagePorts should keep logical port transfers out of the native transfer list'
  )
  assert.match(
    ipc,
    /else if \(object instanceof MessagePort\)[\s\S]*else if \(Object\.getPrototypeOf\(object\) === Object\.prototype\) \{[\s\S]*findIPCMessageTransfers\(transfers, object\[key\]\)/,
    'IPC transferable discovery should recurse into plain objects instead of treating them as transferables'
  )
  assert.doesNotMatch(
    ipc,
    /else \{\s+add\(object\)\s+return object\s+\}\s+\} else if \(Object\.getPrototypeOf/,
    'IPC transferable discovery should not leave its plain-object branch unreachable'
  )
  assert.match(
    ipc,
    /else if \(object instanceof MessagePort\) \{\s+transfers\.delete\(object\)[\s\S]*createIPCMessagePortBridge\(options\)[\s\S]*nativePort\.addEventListener\('message'[\s\S]*relay\.addEventListener\('message'/,
    'native MessagePorts should be bridged through paired IPC endpoints instead of replaced by disconnected ports'
  )
  assert.match(
    ipc,
    /static transfer \(port\)[\s\S]*IPCMessagePort\.ports\.delete\(port\.id\)[\s\S]*\[Symbol\.for\('oro\.runtime\.serialize'\)\][\s\S]*transferred: true,[\s\S]*\/\/ swap rx\/tx/,
    'a received IPC port should preserve its channel orientation when transferred through another worker hop'
  )
  for (const source of [vmInit, vmWorld]) {
    assert.match(
      source,
      /function serializeWindowMessage[\s\S]*serialize\(ipc\.findIPCMessageTransfers\(transfers, message\)\)[\s\S]*ipc\.IPCMessagePort\.transfer\(transfer\)/,
      'VM window boundaries should serialize logical IPC ports before native structured cloning'
    )
  }
  assert.match(
    vmInit,
    /this\.worker\.port\.postMessage\(\{ \.\.\.data, type: 'result' \}\)[\s\S]*await world\.postMessage\(serializeWindowMessage\(event\.data\)\)/,
    'the VM coordinator should preserve serialized results and serialize worker messages sent to a world'
  )
  assert.match(
    vmInit,
    /frame\.addEventListener\('load'[\s\S]*setTimeout\(\(\) => \{[\s\S]*VM world \$\{id\} did not load[\s\S]*10_000[\s\S]*target\.appendChild\(this\.frame\)/,
    'VM worlds should install load listeners before attachment and reject stalled loads promptly'
  )
  assert.match(
    vmInit,
    /catch \(error\) \{[\s\S]*this\.worlds\.delete\(id\)[\s\S]*type: 'result',[\s\S]*err: createTransferredError\(error\)/,
    'VM coordinator forwarding errors should be returned to the waiting script call'
  )
  assert.match(
    vmWorld,
    /function postWorldResult[\s\S]*realm\.postMessage\(serializeWindowMessage\(message\)\)[\s\S]*const eventData = ipc\.inflateIPCMessageTransfers\(event\.data\)/,
    'VM worlds should inflate coordinator messages and serialize their results'
  )
  assert.match(
    vm,
    /event\.data\?\.type === 'result'[\s\S]*inflateIPCMessageTransfers\(event\.data\)/,
    'VM clients should inflate serialized world results after the final native message boundary'
  )
  assert.match(
    sharedWorkerInit,
    /const installations = new Map\(\)[\s\S]*function ensureWorkerInstalled[\s\S]*if \(installations\.has\(info\.hash\)\)[\s\S]*return installations\.get\(info\.hash\)[\s\S]*waitForWorkerInstallation/,
    'concurrent clients should share the same SharedWorker installation promise'
  )
  assert.match(
    sharedWorkerInit,
    /export async function onConnect[\s\S]*const worker = await ensureWorkerInstalled\(info\)[\s\S]*worker\.postMessage\(\{ connect: info \}\)/,
    'connections should not reach a SharedWorker before installation completes'
  )
  assert.match(
    sharedWorkerRuntime,
    /let installation = null[\s\S]*installation = install\(data\.install\)[\s\S]*await installation[\s\S]*dispatchConnection\(data\.connect\)/,
    'the SharedWorker runtime should serialize install and connect messages'
  )
  assert.match(
    sharedWorkerRuntime,
    /async function install \(info\)[\s\S]*try \{[\s\S]*await installWorker\(info\)[\s\S]*catch \(err\)[\s\S]*reportWorkerError\(info\?\.id, err\)/,
    'all SharedWorker installation phases should report failures'
  )
  assert.match(
    sharedWorkerRuntime,
    /try \{\s+dispatchConnection\(data\.connect\)\s+\} catch \(err\) \{\s+reportWorkerError\(data\.connect\.id, err\)/,
    'connection deserialization and dispatch failures should reach the client'
  )
  assert.match(
    sharedWorkerRuntime,
    /Environment\.open\(\{[\s\S]*type: 'sharedWorker',[\s\S]*scope: String\(id\)[\s\S]*\}\)/,
    'each SharedWorker should open an environment scoped to its identity'
  )
  assert.match(
    sharedWorkerRuntime,
    /new MessageEvent\('connect', \{ data: connection \}\)/,
    'module connect handlers should receive the connection metadata'
  )
  assert.doesNotMatch(
    sharedWorkerRuntime,
    /module\.reportError\.bind/,
    'CommonJS worker error handlers should be bound from module.exports'
  )
  assert.match(
    sharedWorker,
    /function registerWorker[\s\S]*new Set\(\)[\s\S]*export async function init[\s\S]*const registration = registerWorker\(sharedWorker\)[\s\S]*await application\.getCurrentWindow\(\)/,
    'SharedWorker errors should reach every registered client, including during startup'
  )
  assert.match(
    sharedWorkerInit,
    /this\.addEventListener\('error',[\s\S]*SharedWorker bootstrap failed[\s\S]*channel\.postMessage/,
    'native worker bootstrap errors should be forwarded to SharedWorker clients'
  )
  assert.match(
    sharedWorker,
    /const port = serialize\([\s\S]*IPCMessagePort\.transfer\(sharedWorker\.channel\.port2\)[\s\S]*await currentWindow\.send\([\s\S]*port,/,
    'the transferred port relay should be active before the worker can acknowledge'
  )
  assert.match(
    sharedWorker,
    /const id = crypto\.murmur3\(`\$\{url\.toString\(\)\}\\0\$\{name \?\? ''\}`\)/,
    'SharedWorker identity should include the complete URL and worker name'
  )
})

test('macOS window teardown preserves WebKit-owned views', () => {
  const appleWindow = readFile('src/runtime/window/apple.mm')

  assert.match(
    appleWindow,
    /initWithContentRect:[\s\S]*?defer: NO\s+\];\s+this->window\.releasedWhenClosed = NO;/,
    'the C++ Window owner should retain the NSWindow until its destructor releases it'
  )
  assert.doesNotMatch(
    appleWindow,
    /for \(NSView\* view in contentView\.subviews\)[\s\S]*\[view release\]/,
    'window teardown must not release WKWebView-owned subviews'
  )
  const shouldClose = appleWindow.match(
    /- \(BOOL\) windowShouldClose:[\s\S]*?\n}\n#elif ORO_RUNTIME_PLATFORM_IOS/
  )?.[0]
  assert.ok(shouldClose, 'the macOS window-close delegate should exist')
  assert.doesNotMatch(
    shouldClose,
    /window->window\.(?:contentView|titleBarView) = nullptr|window->window = nullptr|\[window->window\.titleBarView release\]/,
    'the close delegate should not destroy native window state while Cocoa is executing it'
  )
  assert.match(
    shouldClose,
    /window->window\.delegate = nullptr;[\s\S]*objc_setAssociatedObject\(self, "window", nil,[\s\S]*app->dispatch\([\s\S]*destroyWindow\(index\)/,
    'the close delegate should detach callbacks and defer manager-owned teardown'
  )
})

test('Windows AI archives are installed and linked with shared ggml', () => {
  const install = readFile('bin/install.sh')

  for (const archive of [
    'llama.lib',
    'whisper.lib',
    'ggml.lib',
    'ggml-cpu.lib',
    'ggml-base.lib'
  ]) {
    assert.match(
      install,
      new RegExp(`win_static_libs\\+=\\(.*?${archive.replace('.', '\\.')}.*?\\)`),
      `the Windows CLI should link ${archive}`
    )
  }

  assert.match(
    install,
    /_cmake_configure \.\. \. \\\s+-DCMAKE_INSTALL_PREFIX=.*?\\\s+-DCMAKE_INSTALL_LIBDIR="lib\$d"[\s\S]*?cmake --install \. --config "\$config"[\s\S]*?missing installed \$windows_llama_archive/,
    'the Windows llama build should install and validate its current CMake archive layout'
  )
  assert.match(
    install,
    /local ggml_dir="\$BUILD_DIR\/\$target-\$platform\/lib\$d\/cmake\/ggml"[\s\S]*?WHISPER_USE_SYSTEM_GGML=ON[\s\S]*?_cmake_configure \.\. \. \\\s+-DCMAKE_INSTALL_PREFIX=[\s\S]*?cmake --install \. --config "\$config"[\s\S]*?missing installed whisper\.lib/,
    'Whisper should reuse the installed llama ggml package and install its archive before linking'
  )
})

test('Linux window teardown completes synchronous GTK closes', () => {
  const manager = readFile('src/runtime/window/manager.cc')

  assert.match(
    manager,
    /if \(window->window != nullptr\) \{\s+window->close\(\);\s+if \(window->window != nullptr\) \{\s+return;/,
    'synchronously destroyed GTK windows should be removed before close replies allow more windows to be created'
  )
  assert.doesNotMatch(
    manager,
    /if \(window->window != nullptr\) \{\s+window->close\(\);\s+return;/,
    'GTK close should defer cleanup only while the native window remains alive'
  )
})

test('Linux headless focus changes bypass native compositor operations', () => {
  const linuxWindow = readFile('src/runtime/window/linux.cc')
  const focus = linuxWindow.match(
    /void Window::focus \(\) \{([\s\S]*?)\n {2}\}\n\n {2}void Window::blur/
  )?.[1]
  const blur = linuxWindow.match(
    /void Window::blur \(\) \{([\s\S]*?)\n {2}\}\n\n {2}void Window::setAlwaysOnTop/
  )?.[1]

  assert.ok(focus, 'the Linux focus implementation should exist')
  assert.ok(blur, 'the Linux blur implementation should exist')
  assert.match(
    focus,
    /options\.headless == false[\s\S]*gtk_window_present[\s\S]*evalDomFocusThrottled/,
    'headless focus should mirror DOM state without presenting a hidden GTK window'
  )
  assert.match(
    blur,
    /options\.headless == false[\s\S]*gdk_window_lower[\s\S]*evalDomBlurThrottled/,
    'headless blur should mirror DOM state without entering the native compositor'
  )
})

test('OTP tests inject their request transport without mutating ESM imports', () => {
  const credentials = readFile('api/internal/credentials.js')
  const otpTest = readFile('test/src/otp.js')

  assert.match(
    credentials,
    /async function get \(options, \.\.\.args\)[\s\S]*typeof args\[0\] === 'function'[\s\S]*ipc\.request\('otp\.credentials\.get'/,
    'credentials.get should provide an injectable request transport while preserving its literal production route'
  )
  assert.doesNotMatch(
    otpTest,
    /ipc\.request\s*=/,
    'OTP tests should not assign to a read-only ESM module namespace'
  )
})

test('Windows runtime builds avoid incompatible headers and archives', () => {
  const platform = readFile('src/runtime/platform/system.hh')
  const cflags = readFile('bin/cflags.sh')
  const ldflags = readFile('bin/ldflags.sh')
  const installer = readFile('bin/install.sh')
  const pkgConfig = readFile('bin/generate-oro-runtime-pkg-config.sh')
  const runtimeBuilder = readFile('bin/build-runtime-library.sh')
  const bridge = readFile('src/runtime/bridge/bridge.cc')
  const processService = readFile('src/runtime/core/services/process.cc')
  const secureStorage = readFile('src/runtime/core/services/secure_storage.cc')
  const updateService = readFile('src/runtime/core/services/update.cc')
  const appHeader = readFile('src/runtime/app.hh')
  const appWindow = readFile('src/runtime/app/win.cc')
  const platformWindow = readFile('src/runtime/window/win.cc')
  const processWindow = readFile('src/runtime/process/win.cc')
  const runtimeString = readFile('src/runtime/string/string.cc')
  const tar = readFile('src/runtime/tar.cc')
  const desktop = readFile('src/desktop/main.cc')
  const cli = readFile('src/cli/main.cc')
  const extensionJson = readFile('src/extension/json.cc')
  const bluetooth = readFile('src/runtime/core/services/bluetooth/win.cc')
  const dbusHeader = readFile('src/runtime/core/services/dbus.hh')
  const dbusService = readFile('src/runtime/core/services/dbus.cc')
  const hid = readFile('src/runtime/core/services/hid/windows_backend.cc')
  const routes = readFile('src/runtime/ipc/routes.cc')
  const tlsClient = readFile('src/runtime/tls/client_schannel_windows.cc')
  const tlsServer = readFile('src/runtime/tls/server_schannel_windows.cc')
  const tlsServerHeader = readFile('src/runtime/tls/server.hh')
  const tlsUtil = readFile('src/runtime/tls/schannel_util_windows.hh')

  assert.match(
    platform,
    /#define NOMINMAX[\s\S]*#include <windows\.h>[\s\S]*#include <roapi\.h>[\s\S]*#include <wrl\.h>/,
    'Windows SDK compatibility macros and headers should be ordered correctly'
  )
  assert.doesNotMatch(
    platform,
    /#undef interface/,
    'the Windows interface macro must remain available to WinRT headers included by platform sources'
  )
  assert.match(
    cflags,
    /-DNOMINMAX[\s\S]*-DWINVER=0x0A00[\s\S]*-D_WIN32_WINNT=0x0A00[\s\S]*-DNTDDI_VERSION=0x0A000000/,
    'Windows SDK compatibility macros should be defined before any source header is included'
  )
  assert.match(
    cflags,
    /-Wl,-NODEFAULTLIB:libcmt[\s\S]*-Wl,-NXCOMPAT[\s\S]*-Wl,-DYNAMICBASE[\s\S]*-Wl,-HIGHENTROPYVA[\s\S]*-Wl,-guard:cf/,
    'Windows linker options should remain opaque to Git Bash path conversion'
  )
  assert.doesNotMatch(
    [cflags, cli].join('\n'),
    /(?:-Xlinker |-Wl,)\/(?:NODEFAULTLIB|NXCOMPAT|DYNAMICBASE|HIGHENTROPYVA|guard:cf)/,
    'Windows build commands should not emit path-like slash linker arguments'
  )
  assert.equal(
    (cli.match(/-Wl,-NODEFAULTLIB:libcmt/g) || []).length,
    2,
    'CLI-generated Windows build commands should keep linker options opaque to Git Bash'
  )
  assert.match(
    pkgConfig,
    /ldflags\+=\("-Wl,-NODEFAULTLIB:libcmt"\)/,
    'Windows pkg-config metadata should expose linker options through Libs without path conversion'
  )
  assert.match(
    cflags,
    /zlib_include_dir=""[\s\S]*zlib\.h[\s\S]*zconf\.h[\s\S]*ORO_RUNTIME_HAS_ZLIB=1[\s\S]*-I\$zlib_include_dir/,
    'zlib should be advertised only when its library and generated headers are usable'
  )
  assert.match(
    cflags,
    /sodium\.h" && -f "\$candidate\/sodium\/version\.h[\s\S]*if \[\[ "\$host" == "Win32" \]\]; then\s+have_libipfs=0/,
    'optional Windows dependencies should be enabled only when their usable headers and ABI are present'
  )
  assert.match(
    ldflags,
    /have_sodium=0[\s\S]*libsodium\.lib[\s\S]*if \(\( have_sodium \)\)[\s\S]*host" != "Win32"[\s\S]*have_libipfs/,
    'Windows linkage should follow the same optional-dependency availability checks'
  )
  assert.match(
    installer,
    /host" == "Win32"[\s\S]*MinGW C archive that is incompatible with the MSVC runtime build; skipping/,
    'the Windows bootstrap should skip the incompatible and expensive libipfs archive build'
  )
  assert.match(
    installer,
    /install_prefix="\$BUILD_DIR\/\$target-\$platform"[\s\S]*host" == "Win32"[\s\S]*native_path "\$install_prefix"[\s\S]*CMAKE_INSTALL_PREFIX="\$install_prefix"/,
    'native Windows CMake should receive a native zlib install prefix'
  )
  assert.doesNotMatch(
    processService,
    /String (?:stdout|stderr);/,
    'Windows stdio macros should not replace process output variable names'
  )
  assert.doesNotMatch(
    updateService,
    /JSON::Object::Entries json \{/,
    'Windows conditional compilation should not leave update response names colliding in one scope'
  )
  assert.equal(
    bridge.match(/string::convertStringToWString/g)?.length,
    2,
    'Windows custom schemes should call the runtime string conversion namespace explicitly'
  )
  assert.match(
    secureStorage,
    /ORO_RUNTIME_PLATFORM_WINDOWS[\s\S]*namespace \{\s+using oro::runtime::String;[\s\S]*oro::runtime::bytes::base64::encode[\s\S]*oro::runtime::bytes::base64::decode/,
    'Windows secure-storage helpers should qualify runtime string and byte helpers in their file-local namespace'
  )
  assert.match(
    secureStorage,
    /result\(static_cast<size_t>\(required\), L?'\\0'\)[\s\S]*result\.resize\(static_cast<size_t>\(written - 1\)\)/,
    'Windows secure-storage conversion should reserve space for the Win32 terminator before trimming it'
  )
  assert.match(
    platformWindow,
    /namespace oro::runtime::window \{[\s\S]*class DragDrop final : public IDropTarget[\s\S]*Window::Window/,
    'the Windows implementation and drag-drop type should compile in the declared window namespace'
  )
  assert.doesNotMatch(
    appWindow,
    /dynamic_cast<[^>]*Window\*>|->bridge\.emit|->evalDom(?:Focus|Blur)Throttled/,
    'the Windows app should qualify window types and use public shared-pointer APIs'
  )
  assert.match(
    appWindow,
    /static_cast<oro::runtime::window::Window\*>\(window\)/,
    'known managed windows should use a valid derived-to-base cast'
  )
  assert.match(
    appWindow,
    /using oro::runtime::javascript::getEmitToRenderProcessJavaScript;[\s\S]*oro::runtime::window::HotKeyBinding::ID/,
    'the Windows app should qualify JavaScript and hotkey symbols from sibling namespaces'
  )
  assert.doesNotMatch(
    appWindow,
    /auto userconfig/,
    'the Windows app should preserve the declared userConfig identifier casing'
  )
  assert.match(
    [appHeader, appWindow].join('\n'),
    /WNDCLASSEXW wcex;[\s\S]*app->wcex = \{\};[\s\S]*app->wcex\.cbSize[\s\S]*RegisterClassExW\(&app->wcex\)/,
    'the Windows app should initialize and register a Unicode window class'
  )
  assert.match(
    bluetooth,
    /#include <windows\.h>[\s\S]*#include <combaseapi\.h>[\s\S]*#include <windows\.foundation\.h>[\s\S]*#include "\.\.\/\.\.\/\.\.\/runtime\.hh"/,
    'the Windows Bluetooth backend should parse WinRT ABI headers before project headers can alter SDK macros'
  )
  assert.match(
    bluetooth,
    /#include <bluetoothapis\.h>[\s\S]*#include <robuffer\.h>[\s\S]*#include <setupapi\.h>[\s\S]*using Windows::Storage::Streams::IBufferByteAccess;/,
    'the Windows Bluetooth backend should use the SDK declarations for Bluetooth, WinRT buffers, and SetupAPI'
  )
  assert.match(
    bluetooth,
    /decltype\(&BluetoothGATTGetCharacteristicValue\)[\s\S]*BluetoothGATTGetCharacteristicValue[\s\S]*bthLeUuidToGuid[\s\S]*bthLeUuidEqualsGuid/,
    'the Windows Bluetooth backend should derive dynamic API types from the SDK and normalize GATT UUIDs'
  )
  assert.doesNotMatch(
    bluetooth,
    /BluetoothGATT(?:Read|Write)CharacteristicValue|BluetoothGetDeviceInfoW|guidToString\([^\n]*\.ServiceUuid\)|memcmp\(&[^\n]*Uuid|typedef struct _BLUETOOTH_|typedef PVOID HDEVINFO|\.detach\(\)/,
    'the Windows Bluetooth backend should not use nonexistent exports, incompatible UUID layouts, or handwritten SDK structures'
  )
  assert.match(
    bluetooth,
    /BluetoothGATTSetCharacteristicValue[\s\S]*PBLUETOOTH_GATT_VALUE_CHANGED_EVENT[\s\S]*BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION/,
    'the Windows Bluetooth backend should use the SDK GATT write and value-change event contracts'
  )
  assert.doesNotMatch(
    [dbusHeader, dbusService, routes].join('\n'),
    /(?:String|options|signal|pending|object)\.interface\b|String interface;/,
    'D-Bus implementation names should not collide with the Windows interface macro'
  )
  assert.match(
    dbusHeader,
    /String interfaceName;/,
    'D-Bus options should retain a Windows-safe interface field'
  )
  assert.match(
    hid,
    /struct RequestState \{[\s\S]*HID::RequestDeviceOptions options;[\s\S]*Vector<HID::Backend::DeviceDescriptor> pending;/,
    'the Windows HID backend should declare its pending request state'
  )
  assert.match(
    hid,
    /#include "\.\.\/\.\.\/\.\.\/app\.hh"[\s\S]*#include "\.\.\/\.\.\/\.\.\/platform\/types\.hh"/,
    'the Windows HID backend should resolve runtime headers from its nested service directory'
  )
  assert.match(
    hid,
    /SetupDiEnumDeviceInterfaces\(/,
    'the Windows HID backend should call the declared SetupAPI interface enumerator'
  )
  assert.doesNotMatch(
    hid,
    /SetupDiEnumDeviceInterface\(/,
    'the Windows HID backend should not call the nonexistent singular SetupAPI enumerator'
  )
  assert.doesNotMatch(
    hid,
    /\[className\]\(\)|base64::encode\(payload\)|it->second\.running/,
    'the Windows HID backend should use valid static storage, buffer encoding, and shared-pointer access'
  )
  assert.doesNotMatch(
    hid,
    /void dispatchDeviceEvent\([^{};]*\)\s*;|DeviceDescriptor makeRemovedDescriptor\([^{};]*\)\s*;|LRESULT CALLBACK NotificationWindowProc\([^{};]*\)\s*;/,
    'inline Windows HID definitions should not have duplicate in-class declarations'
  )
  assert.match(
    hid,
    /FILE_FLAG_OVERLAPPED[\s\S]*OVERLAPPED overlapped = \{\};[\s\S]*WriteFile\([\s\S]*&overlapped[\s\S]*CancelIoEx\(handle, &overlapped\)/,
    'overlapped Windows HID handles should use bounded, cancellable overlapped writes'
  )
  assert.doesNotMatch(
    platformWindow,
    /app->(?:isReady|hInstance|getcwd\(\))|navigateFunction|evaluateJavaScriptFunction/,
    'the Windows window backend should use the current application and navigation APIs'
  )
  assert.match(
    platformWindow,
    /app->instance[\s\S]*navigateHandler[\s\S]*evaluateJavaScriptHandler/,
    'the Windows window backend should use current application and navigation members'
  )
  assert.match(
    platformWindow,
    /#include "\.\.\/env\.hh"[\s\S]*using oro::runtime::string::convertStringToWString;[\s\S]*env::get\("APPDATA"\)[\s\S]*config::isDebugEnabled\(\)/,
    'the Windows window backend should use current namespaced runtime helpers'
  )
  assert.match(
    platformWindow,
    /certificate->ToPemEncoding\(&pemW\)/,
    'the Windows certificate handler should call the WebView2 Win32 certificate method'
  )
  assert.doesNotMatch(
    platformWindow,
    /get_PemEncodedCertificate|SchemeHandlers::Body/,
    'the Windows window backend should not reference nonexistent WebView2 or scheme-handler members'
  )
  assert.match(
    platformWindow,
    /request\.setBody\(\s+size,\s+reinterpret_cast<const unsigned char\*>\(buffer\.get\(\)\)/,
    'the Windows request handler should use the declared scheme-handler body overload'
  )
  assert.match(
    platformWindow,
    /version::VERSION_FULL_STRING[\s\S]*config::getUserConfig\(\)/,
    'the Windows window backend should qualify version and configuration helpers'
  )
  assert.doesNotMatch(
    platformWindow,
    /MAKEINTRESOURCE\(IDI_APPLICATION\)/,
    'the predefined Windows application icon should not be wrapped as a second resource identifier'
  )
  assert.match(
    platformWindow,
    /class DragDrop final : public IDropTarget[\s\S]*Window::drop owns this object[\s\S]*CreateDataObject\(&format, &medium, 1,[\s\S]*ReleaseStgMedium\(&medium\);[\s\S]*dragDropResult == DRAGDROP_S_CANCEL/,
    'the Windows drag target and outbound data object should have single, explicit ownership'
  )
  assert.match(
    platformWindow,
    /CF_HDROP[\s\S]*TYMED_HGLOBAL[\s\S]*dropFiles->fWide = TRUE[\s\S]*DragQueryFileW[\s\S]*window\.dispatchEvent\(event\)/,
    'Windows file drag and drop should use Unicode HDROP storage and dispatch the resulting event'
  )
  assert.doesNotMatch(
    platformWindow,
    /GlobalUnlock\(list\)|CreateDataObject\(&format, &medium, 2|const dtail|MapWindowPoints\(/,
    'Windows drag and drop should not unlock a data pointer, overrun format arrays, corrupt point storage, or emit invalid JavaScript'
  )
  assert.match(
    platformWindow,
    /ScreenToClient\(child, &point\)/,
    'Windows drag coordinates should be converted from screen to client space without aliasing adjacent stack values'
  )
  assert.match(
    runtimeString,
    /MultiByteToWideChar\(\s*CP_UTF8,\s*MB_ERR_INVALID_CHARS[\s\S]*WideCharToMultiByte\(\s*CP_UTF8,\s*WC_ERR_INVALID_CHARS/,
    'shared Windows string conversion should validate UTF-8 in both directions'
  )
  assert.match(
    processWindow,
    /STARTUPINFOW[\s\S]*CREATE_UNICODE_ENVIRONMENT[\s\S]*CreateProcessW\([\s\S]*processThread = Thread/,
    'Windows process creation should preserve Unicode arguments and own its waiter thread'
  )
  assert.match(
    tar,
    /CreateFileW\([\s\S]*while \(remaining > 0\)[\s\S]*remaining > static_cast<size_t>\(MAXDWORD\)/,
    'Windows archive I/O should preserve Unicode paths and chunk DWORD-sized transfers'
  )
  assert.match(
    desktop,
    /Software\\\\Classes\\\\[\s\S]*RegCreateKeyExW[\s\S]*RegSetValueExW[\s\S]*L"\\"" \+ applicationPath \+ L"\\" \\"%1\\""/,
    'Windows protocol registration should use the per-user Classes hive, Unicode values, and quoted arguments'
  )
  assert.match(
    desktop,
    /wideBundleIdentifier\.empty\(\)[\s\S]*invalid UTF-8 bundle identifier[\s\S]*CreateMutexW/,
    'Windows single-instance startup should reject an invalid UTF-8 mutex name'
  )
  assert.doesNotMatch(
    cli,
    /CliLogLevel::ERROR/,
    'Windows headers should not macro-expand a CLI log-level member'
  )
  assert.match(
    extensionJson,
    /std::memcpy\(bytes, string\.data\(\), length\);\s+bytes\[length\] = '\\0';/,
    'extension JSON strings should initialize allocated output instead of appending to uninitialized memory'
  )
  assert.match(
    runtimeBuilder,
    /--syntax-only[\s\S]*-ferror-limit=0 -fsyntax-only "\$source"/,
    'the runtime builder should expose a compiler-backed source syntax check without hiding follow-on errors'
  )
  assert.match(
    installer,
    /_prepare\s+cd "\$BUILD_DIR" \|\| exit 1[\s\S]{0,160}_get_web_view2\s+_check_windows_runtime_source_syntax[\s\S]*_compile_llama/,
    'opt-in Windows source audits should run before dependency compilation'
  )
  assert.match(
    installer,
    /ORO_TLS_BUILD_PROVIDER=schannel "\$root\/bin\/build-runtime-library\.sh"[\s\S]*--syntax-only/,
    'the Windows source preflight should compile the platform-native TLS provider'
  )
  assert.match(
    installer,
    /entrypoint_sources=\([\s\S]*src\/init\.cc[\s\S]*src\/cli\/\*\.cc[\s\S]*src\/desktop\/\*\.cc[\s\S]*-ferror-limit=0 -fsyntax-only "\$source"/,
    'the early Windows source preflight should include CLI and desktop entrypoints'
  )
  assert.match(
    [tlsClient, tlsServer].join('\n'),
    /#define SECURITY_WIN32 1[\s\S]*PCERT_ALT_NAME_INFO[\s\S]*CERT_KEY_CONTEXT[\s\S]*SecApplicationProtocolNegotiationStatus_Success[\s\S]*DWORD shutdownToken = SCHANNEL_SHUTDOWN/,
    'Schannel sources should use declarations provided by the Windows SDK'
  )
  assert.match(
    tlsClient,
    /CertCreateCertificateChainEngine[\s\S]*CertVerifyCertificateChainPolicy\(\s*CERT_CHAIN_POLICY_SSL[\s\S]*policyStatus\.dwError/,
    'the Schannel client should apply hostname-aware SSL chain policy validation'
  )
  assert.match(
    tlsServer,
    /rootStore && !CertCreateCertificateChainEngine[\s\S]*CertCreateCertificateChainEngine failed[\s\S]*else if \(!CertGetCertificateChain/,
    'the Schannel server should fail closed when its exclusive root chain engine cannot be created'
  )
  assert.match(
    tlsUtil,
    /NCRYPTBUFFER_PKCS_SECRET[\s\S]*NCryptImportKey[\s\S]*NCRYPT_DO_NOT_FINALIZE_FLAG/,
    'encrypted PKCS#8 keys should be imported through the supported CNG password interface'
  )
  assert.match(
    tlsServerHeader,
    /private:\s+void close\(\);/,
    'the Schannel server cleanup implementation should have a class declaration'
  )
  assert.doesNotMatch(
    [tlsClient, tlsServer, tlsUtil].join('\n'),
    /ERROR_BAD_PASSWORD|PCCERT_ALT_NAME_INFO|CRYPT_KEY_CONTEXT|fCallerFreeProvOrNCryptKey|CryptDecryptPrivateKeyInfo|\.ProtoStatus|\bApplicationProtocolNegotiationStatus_Success|SCHANNEL_SHUTDOWN token/,
    'Schannel sources should not reference nonexistent Windows SDK declarations'
  )
})

test('local native includes are validated by the fast lint entrypoint', () => {
  const packageMetadata = JSON.parse(readFile('package.json'))
  const checker = readFile('bin/check-local-includes.js')

  assert.match(
    packageMetadata.scripts.lint,
    /^npm run lint:includes/,
    'native include validation should run before the more expensive lint stages'
  )
  assert.match(
    checker,
    /specifier\.startsWith\('\.'\)[\s\S]*fs\.existsSync\(target\)/,
    'the native include checker should reject unresolved relative includes'
  )
})

test('append writes preserve kernel-managed file positions', () => {
  const fs = readFile('api/fs/index.js')
  const fsStream = readFile('api/fs/stream.js')
  const fsServiceHeader = readFile('src/runtime/core/services/fs.hh')
  const fsService = readFile('src/runtime/core/services/fs.cc')
  const routes = readFile('src/runtime/ipc/routes.cc')

  assert.match(
    fs,
    /\(flags & constants\.O_APPEND\) === constants\.O_APPEND \? -1 : 0[\s\S]*ipc\.sendSync\('fs\.write', \{ id, offset \}/,
    'synchronous append writes should pass the kernel-position sentinel'
  )
  assert.match(
    fsStream,
    /\(handle\.flags & O_APPEND\) === O_APPEND\s+\? null\s+: this\.start \+ this\.bytesWritten/,
    'append streams should not pass an explicit positional offset'
  )
  for (const source of [fsServiceHeader, fsService]) {
    assert.match(
      source,
      /int64_t(?: offset)?/,
      'the native file service should preserve negative offset sentinels'
    )
  }
  assert.match(
    routes,
    /int64_t offset = 0;[\s\S]*REQUIRE_AND_GET_MESSAGE_VALUE\(offset, "offset", std::stoll\)/,
    'the file-write IPC route should preserve signed 64-bit positions'
  )
})

test('VM workers initialize from the explicit shared-worker scope', () => {
  const worker = readFile('api/vm/worker.js')

  assert.match(
    worker,
    /globalThis\.isSharedWorkerScope === true \|\|\s+\(globalThis\.self && !globalThis\.window\)/,
    'VM workers should initialize when the runtime identifies their shared-worker scope'
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

test('ASN.1 value rendering does not require compiler or fixer archives', () => {
  const runtimeBuilder = readFile('bin/build-runtime-library.sh')
  const asn1Service = readFile('src/runtime/core/services/asn1.cc')

  assert.match(
    runtimeBuilder,
    /asn1_source_dirs=\(\s+"\$root\/build\/asn1c\/libasn1parser"\s+\)/,
    'runtime builds should compile only the ASN.1 parser sources they execute'
  )
  assert.doesNotMatch(
    asn1Service,
    /asn1f_printable_value/,
    'ASN.1 JSON rendering should not link against the omitted fixer subsystem'
  )
  assert.match(
    asn1Service,
    /String ASN1::buildValueRepresentation[\s\S]*ATV_BITVECTOR[\s\S]*ATV_REFERENCED[\s\S]*ATV_CHOICE_IDENTIFIER/,
    'the runtime should retain representations for scalar and structured ASN.1 values'
  )
})

test('disabled Iroh builds compile service stubs without FFI symbols', () => {
  const irohService = readFile('src/runtime/core/services/iroh.cc')

  assert.match(
    irohService,
    /#if ORO_RUNTIME_HAS_IROH_FFI\s+struct Iroh::ConnectionTypeWatcher[\s\S]*#else\s+Iroh::Iroh[\s\S]*Iroh FFI unavailable[\s\S]*#endif/,
    'the complete Iroh service implementation should be excluded when its native library is unavailable'
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

test('quiet native builds preserve command failure diagnostics', () => {
  const functions = readFile('bin/functions.sh')
  const workflow = readFile('.github/workflows/ci.yml')
  const runtimeBuilder = readFile('bin/build-runtime-library.sh')
  const quietRunner = functions.match(
    /function quiet \(\) \{([\s\S]*?)\n\}/
  )?.[1]

  assert.ok(quietRunner, 'the quiet command runner should exist')
  assert.match(
    quietRunner,
    /quiet_output="\$\([\s\S]*2>&1\)" \|\| quiet_rc=\$\?[\s\S]*printf '%s\\n' "\$quiet_output" >&2[\s\S]*return "\$quiet_rc"/,
    'quiet commands should replay captured diagnostics and preserve failures'
  )
  assert.doesNotMatch(
    workflow,
    /VERBOSE: '1'|install\.ps1[^\n]*-verbose/i,
    'CI native builds should not stream successful compiler command output'
  )
  assert.match(
    runtimeBuilder,
    /runtime_compiler_launcher[\s\S]*compiler_output="\$\([\s\S]*2>&1[\s\S]*\)" \|\| compiler_rc=\$\?[\s\S]*printf '%s\\n' "\$compiler_output" >&2[\s\S]*return "\$compiler_rc"/,
    'cached Android and Windows compilers should also replay failures in quiet builds'
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
  assert.match(
    runtimeBuilder,
    /platform" = "android"[\s\S]*command -v ccache[\s\S]*runtime_compiler_launcher="ccache"[\s\S]*run_runtime_compiler/,
    'direct Android NDK runtime compilation should use the restored native compiler cache'
  )
  assert.match(
    runtimeBuilder,
    /host" = "Win32"[\s\S]*command -v sccache[\s\S]*runtime_compiler_launcher="sccache"[\s\S]*run_runtime_compiler/,
    'direct Windows runtime compilation should use the configured sccache service'
  )
})

test('mobile CI compiles only the host runtime surface used by its CLI', () => {
  const workflow = readFile('.github/workflows/ci.yml')
  const runtimeBuilder = readFile('bin/build-runtime-library.sh')

  assert.match(
    workflow,
    /RELEASE_SUPPORT" != "desktop"[\s\S]*ORO_RUNTIME_DESKTOP_CLI_ONLY=1/,
    'Android and Apple-mobile shards should request the reduced host CLI runtime'
  )
  assert.match(
    runtimeBuilder,
    /host" = "Linux" \|\| "\$host" = "Darwin"[\s\S]*platform" = "desktop"[\s\S]*ORO_RUNTIME_DESKTOP_CLI_ONLY[\s\S]*sources=\([\s\S]*runtime\/config\/config\.cc[\s\S]*runtime\/mcp\/tool\.cc[\s\S]*runtime\/process\/unix\.cc[\s\S]*runtime\/version\.cc[\s\S]*build\/sqlite\/sqlite3\.c[\s\S]*build\/llama\/src\/llama\.cpp/,
    'the reduced host archive should retain every translation unit consumed by mobile build CLIs'
  )
  assert.match(
    readFile('bin/install.sh'),
    /ORO_RUNTIME_DESKTOP_CLI_ONLY[\s\S]*runtime_target_count > 1[\s\S]*building mobile runtimes before the reduced host CLI[\s\S]*runtime_index = 1[\s\S]*ORO_RUNTIME_BUILD_JOBS="\$CPU_CORES"[\s\S]*runtime_arches\[0\]/,
    'mobile CI should give each runtime build the full CPU budget without oversubscription'
  )
  assert.doesNotMatch(
    runtimeBuilder.match(
      /if \[\[ "\$host" = "Linux" \|\| "\$host" = "Darwin" \]\] &&[\s\S]*?building the mobile CI host CLI runtime surface"\nfi/
    )?.[0] ?? '',
    /runtime\/window\/linux\.cc|runtime\/core\/services\.cc|runtime\/serviceworker/,
    'mobile packaging CLIs should not compile desktop UI and service backends'
  )
  assert.match(
    readFile('test/scripts/test-android.js'),
    /--platform=android'[\s\S]*'--allow-exec'/,
    'Android tests should explicitly authorize their fixture extension build'
  )
})

test('CI correctness builds avoid redundant native compile work', () => {
  const cflags = readFile('bin/cflags.sh')
  const workflow = readFile('.github/workflows/ci.yml')

  assert.equal(
    workflow.match(/ORO_CI_FAST_COMPILE: '1'/g)?.length,
    2,
    'Linux integration and cross-platform builds should both use fast correctness flags'
  )
  assert.match(
    cflags,
    /DEBUG[\s\S]*ORO_CI_FAST_COMPILE[\s\S]*-O0[\s\S]*ORO_RUNTIME_BUILD_DEBUG[\s\S]*ORO_CI_FAST_COMPILE[\s\S]*-O0[\s\S]*-Os/,
    'CI should omit debug symbols and optimization work without changing release flags'
  )
  assert.match(
    workflow,
    /archive build compiles the complete Windows source surface[\s\S]*ORO_RUNTIME_SOURCE_PREFLIGHT=0/,
    'Windows CI should not syntax-compile the full runtime immediately before compiling it again'
  )
  assert.match(
    workflow,
    /Reclaim Android build disk[\s\S]*cache_directories=\([\s\S]*sudo chown -R "\$USER":"\$android_group" "\$directory"[\s\S]*Restore Android build SDK cache/,
    'Android SDK cache targets should be writable before archive restoration'
  )
  assert.match(
    workflow,
    /id: smoke-test[\s\S]*Run desktop tests[\s\S]*always\(\) && steps\.smoke-test\.outcome == 'success'[\s\S]*Run child process integration tests[\s\S]*always\(\) && steps\.smoke-test\.outcome == 'success'[\s\S]*Run MCP integration tests[\s\S]*always\(\) && steps\.smoke-test\.outcome == 'success'[\s\S]*Run runtime-core tests/,
    'independent Linux integration suites should continue after an earlier suite fails'
  )
  assert.match(
    workflow,
    /id: validate-targets-unix[\s\S]*id: validate-targets-windows[\s\S]*id: smoke-test[\s\S]*always\(\)[\s\S]*Run desktop tests[\s\S]*steps\.smoke-test\.outcome == 'success'[\s\S]*Run child process integration tests[\s\S]*steps\.smoke-test\.outcome == 'success'[\s\S]*Boot iOS Simulator and run tests[\s\S]*steps\.smoke-test\.outcome == 'success'[\s\S]*Run Android emulator tests/,
    'cross-platform test suites should depend on build readiness rather than prior test outcomes'
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
    /macOS x64[\s\S]*release_support: desktop[\s\S]*exclude_ios: true[\s\S]*macOS \+ iOS arm64[\s\S]*apple_mobile_targets: arm64-iPhoneSimulator/,
    'per-commit CI should keep iOS simulation on the tested Apple Silicon shard and avoid duplicating it on Intel'
  )
  assert.match(
    workflow,
    /read -r -a required_ios_targets <<< "\$ORO_CI_APPLE_MOBILE_TARGETS"[\s\S]*Apple CI built an unassigned iOS target/,
    'CI should validate both missing and unexpectedly duplicated Apple targets'
  )
  assert.equal(
    workflow.match(/ccache --max-size 1536M/g)?.length,
    2,
    'native CI lanes should retain enough compiler output to avoid cache churn'
  )
  assert.match(
    workflow,
    /RUNNER_OS" == 'macOS'[\s\S]*ccache --max-size 3G/,
    'Apple host and simulator objects should fit in the native compiler cache without eviction churn'
  )
})

test('cross-platform CI skips redundant desktop networking archives', () => {
  const workflow = readFile('.github/workflows/ci.yml')

  assert.match(
    workflow,
    /Linux integration lane exercises the desktop networking FFI[\s\S]*ORO_SKIP_IROH=1[\s\S]*ORO_SKIP_LIBIPFS=1[\s\S]*BUILD_ANDROID" == "true"/,
    'platform shards should not repeat the networking FFI build covered by Linux integration'
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
  const zlibCompiler = installer.match(
    /function _compile_zlib \{([\s\S]*?)\n\}/
  )?.[1]
  assert.ok(llamaCompiler, 'the llama compiler function should exist')
  assert.ok(libuvCompiler, 'the libuv compiler function should exist')
  assert.ok(zlibCompiler, 'the zlib compiler function should exist')
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
    /cmake --build \. --config "\$config" --parallel "\$CPU_CORES"/,
    'Windows CMake builds should use detected CPU parallelism'
  )
  assert.match(
    zlibCompiler,
    /cmake --build \. --config "\$config" --parallel "\$CPU_CORES"/,
    'Windows zlib builds should use CMake parallelism instead of forwarding Unix flags to MSBuild'
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
    /Android x86_64[\s\S]*android_supported_abis: x86_64[\s\S]*test_android: true[\s\S]*Android arm64-v8a[\s\S]*android_supported_abis: arm64-v8a[\s\S]*test_android: false/,
    'CI should build Android ABIs in parallel while exercising the emulator-compatible shard'
  )
  assert.match(
    workflow,
    /android\)[\s\S]*required_android_abis[\s\S]*Android CI built an unassigned ABI target[\s\S]*ios\)[\s\S]*required_ios_targets[\s\S]*Apple CI is missing required iOS target/,
    'CI should fail if an assigned Android ABI or iOS target is missing or duplicated'
  )
  assert.match(
    workflow,
    /macOS x64[\s\S]*macOS \+ iOS arm64/,
    'CI should build both Apple host architectures and test iOS on Apple Silicon'
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
      /CCACHE_TEMPDIR: \$\{\{ github\.workspace \}\}\/\.cache\/ccache-tmp[\s\S]*mkdir -p "\$CCACHE_TEMPDIR"/,
      'volatile ccache temporary files should stay outside saved compiler caches'
    )
  }
  assert.match(
    workflow,
    /Build Oro Runtime CLI\n\s+id: build-runtime\n\s+timeout-minutes: 20[\s\S]*Build Oro Runtime CLI \(Unix\)\n\s+id: build-runtime-unix[\s\S]*timeout-minutes: 20[\s\S]*Build Oro Runtime CLI \(Windows\)\n\s+id: build-runtime-windows[\s\S]*timeout-minutes: 20/,
    'CI should stop native builds that exceed the twenty-minute cutoff'
  )
  for (const nativeWorkflow of [workflow, releaseWorkflow, publishWorkflow]) {
    assert.match(
      nativeWorkflow,
      /native-cache[\s\S]*github\.run_attempt[\s\S]*Save native compiler cache[\s\S]*always\(\)[\s\S]*native-cache\.outcome == 'success'/,
      'native compiler caches should retain partial failed or cancelled attempt work under an immutable attempt key'
    )
  }
  assert.match(
    workflow,
    /Quiesce native compiler cache[\s\S]*native_compiler_pids[\s\S]*native_signal in TERM KILL[\s\S]*ccache --cleanup[\s\S]*Save native compiler cache/,
    'cancelled platform builds should stop cache writers before preserving partial compiler output'
  )
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
  assert.equal(
    workflow.match(/Setup Unix Rust compiler cache/g)?.length,
    2,
    'Linux integration and Unix platform shards should configure Rust compiler caches'
  )
  assert.match(
    workflow,
    /Setup Unix Rust compiler cache[\s\S]*mozilla-actions\/sccache-action@[0-9a-f]{40} # v0\.0\.11[\s\S]*Configure Unix Rust compiler cache[\s\S]*SCCACHE_GHA_ENABLED=true[\s\S]*RUSTC_WRAPPER=sccache/,
    'Unix native builds should reuse Rust compiler outputs through sccache'
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
    readFile('bin/install.sh'),
    /host" == "Win32"[\s\S]*cgo_env\+=\("CC=clang"\)[\s\S]*"\$\{cgo_env\[@\]\}"[\s\S]*CGO_ENABLED=1/,
    'Windows libipfs builds should give cgo a compiler name without spaces'
  )
  assert.match(
    readFile('bin/install.sh'),
    /ORO_LIBIPFS_BUILD_ATTEMPTS:-3[\s\S]*while \(\( attempt <= max_attempts \)\)[\s\S]*retrying with the populated Go cache/,
    'libipfs builds should retry transient module downloads without discarding the Go cache'
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
    /Build Oro Runtime CLI \(Unix\)[\s\S]*Restore Android emulator cache[\s\S]*Grant Android emulator KVM access[\s\S]*chmod a\+rw \/dev\/kvm[\s\S]*Run Android emulator tests/,
    'the Android emulator should be restored after native outputs are pruned and receive KVM access before launch'
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
    /Linux arm64[\s\S]*install_tests: false[\s\S]*macOS x64[\s\S]*install_tests: false/,
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
    cli,
    /"init "[\s\S]*"--use-defaults "[\s\S]*"--overwrite"/,
    'Gradle initialization should be non-interactive and replace files in the prepared Android project directory'
  )
  assert.match(
    emulatorBootstrap,
    /ANDROID_USER_HOME[\s\S]*ANDROID_AVD_HOME[\s\S]*android_system_image_arch[\s\S]*OROAVD_API_[\s\S]*emulator_packages[\s\S]*system_image_dir[\s\S]*sdkmanager" "\$\{emulator_packages\[@\]\}"[\s\S]*ANDROID_AVD_HOME\/\$avd_name\.ini/,
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
  assert.match(
    resolver,
    /env\.ORO_HOME[\s\S]*path\.join\(env\.ORO_HOME, 'bin', executable\)[\s\S]*existsSync\(staged\)/,
    'test runners should use the staged CLI after mobile CI prunes the source build tree'
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
