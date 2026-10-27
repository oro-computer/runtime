// Exercise the shipped metadata checker with real JVM classfiles.
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { execute } from '../../src/cli/tooling/process.cjs'
import { preflightKotlin } from '../../src/cli/tooling/kotlin.cjs'
import { resolveProgram } from '../../src/cli/tooling/servers.cjs'

const root = mkdtempSync(path.join(tmpdir(), 'oro-kotlin-compatibility-'))
const java = process.env.JAVA_HOME ? path.join(process.env.JAVA_HOME, 'bin', process.platform === 'win32' ? 'java.exe' : 'java') : 'java'
const javac = process.env.JAVA_HOME ? path.join(process.env.JAVA_HOME, 'bin', process.platform === 'win32' ? 'javac.exe' : 'javac') : 'javac'
const checker = fileURLToPath(new URL('../../src/cli/tooling/KotlinCompatibility.java', import.meta.url))
const annotation = path.join(root, 'Metadata.java')
writeFileSync(annotation, 'package kotlin; @java.lang.annotation.Retention(java.lang.annotation.RetentionPolicy.RUNTIME) public @interface Metadata { int[] mv(); int xi() default 0; }')
for (const [name, version, flags, unsupported] of [['Current', '2, 1, 0', 0, false], ['NextMinor', '2, 2, 0', 0, false], ['Strict', '2, 2, 0', 8, true], ['Future', '2, 3, 0', 0, true]]) {
  const source = path.join(root, `${name}.java`)
  const classes = path.join(root, name)
  mkdirSync(classes)
  writeFileSync(source, `@kotlin.Metadata(mv={${version}}, xi=${flags}) public class ${name} {}`)
  await execute(javac, ['-d', classes, annotation, source])
  const inventory = path.join(root, `${name}.txt`)
  writeFileSync(inventory, classes + '\n')
  const checked = execute(java, [checker, inventory])
  if (unsupported) await assert.rejects(checked, /unsupported Kotlin metadata/)
  else await checked
}
const server = resolveProgram(process.env.ORO_KOTLIN_LANGUAGE_SERVER || 'kotlin-language-server', process.env)
await assert.rejects(preflightKotlin(server, { ...process.env, ANDROID_HOME: path.join(root, 'missing-sdk'), ANDROID_SDK_ROOT: '' }), /Install the SDK.*ANDROID_HOME/)
const oldJava = process.env.ORO_LANGUAGE_TEST_OLD_JAVA_HOME || process.env.JAVA_HOME_17_X64
if (oldJava) {
  await assert.rejects(preflightKotlin(server, { ...process.env, JAVA_HOME: oldJava }), /JDK 21.*JAVA_HOME/)
} else process.stderr.write('Old-JDK rejection not exercised: ORO_LANGUAGE_TEST_OLD_JAVA_HOME/JAVA_HOME_17_X64 is unavailable\n')
console.log('Qualified Kotlin metadata limits and missing-SDK recovery')
