'use strict'

// Exercise the production model, launch, scope and supervisor without an SDK.
const fs = require('node:fs')
const path = require('node:path')
const { initializeClient } = require('../../../src/cli/tooling/lsp.cjs')
const { serve } = require('../../../src/cli/tooling/session.cjs')
const { swiftModel } = require('../../../src/cli/tooling/swift.cjs')
const { appleToolchain } = require('../../../src/cli/tooling/native.cjs')
const { modelFor, target, write, publish } = require('../../../src/cli/tooling/model.cjs')
const { resolveProgram, serverSettings, preflightSourceKit, stdioLaunch } = require('../../../src/cli/tooling/servers.cjs')

async function main () {
  const [language, root] = process.argv.slice(2)
  const configuration = path.join(root, 'fixture.json')
  const config = JSON.parse(fs.readFileSync(configuration, 'utf8'))
  const project = { root, configuration, config, env: process.env }
  const client = await initializeClient(process.stdin)
  try {
    await serve(client, async () => {
      const settings = serverSettings(config, language, root)
      settings.command = resolveProgram(settings.command, project.env)
      if (settings.adapter === 'sourcekit') await preflightSourceKit(settings.command, project.env)
      let model
      if (language === 'swift') model = await swiftModel({ prepare: true }, project)
      else {
        const file = fs.realpathSync.native(path.join(root, { c: 'main.c', cpp: 'main.cc', objc: 'main.m', objcpp: 'main.mm' }[language]))
        model = modelFor(project, {}, 'cpp', { originalSources: [file] })
        model.compileCommands = path.join(model.generation, 'compile_commands.json')
        model.artifacts = [model.compileCommands, file]
        let compiler = resolveProgram(['c', 'objc'].includes(language) ? 'clang' : 'clang++', project.env)
        const flags = ['-DORO_BACKEND_REQUIRED']
        if (['objc', 'objcpp'].includes(language)) {
          const toolchain = await appleToolchain(target({}, config, 'cpp'), project)
          compiler = toolchain.executable
          flags.push('-isysroot', toolchain.sysroot, '-target', toolchain.triple)
        }
        write(model.compileCommands, [{ directory: root, file, arguments: [compiler, ...flags, '-c', file] }])
        publish(model)
      }
      console.error(JSON.stringify({ backend: settings.command, compiler: model.toolchain?.compilerVersion, compileCommands: model.compileCommands, indexStorePath: model.indexStorePath }))
      return stdioLaunch(project, settings, model)
    })
  } finally { client.close() }
}

main().catch(error => { console.error(error); process.exitCode = 1 })
