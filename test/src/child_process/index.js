import { spawn, exec, execFile } from 'oro:child_process'
import process from 'oro:process'
import test from 'oro:test'
import os from 'oro:os'

const isWindows = os.platform() === 'win32'
const listDirectory = isWindows ? 'dir /b' : 'ls -la'

test('child_process.spawn(command[,args[,options]])', async (t) => {
  const command = isWindows ? 'cmd.exe' : 'ls'
  const args = isWindows ? ['/d', '/c', 'dir', '/b'] : ['-la']
  const options = {}

  let hasDir = false

  const pending = []
  let signalTimeout
  let onSignal

  if (/linux|darwin/i.test(os.platform())) {
    pending.push(
      new Promise((resolve, reject) => {
        signalTimeout = setTimeout(
          () => reject(new Error('Timed out waiting for SIGCHLD signal')),
          10000
        )

        onSignal = () => {
          resolve()
          clearTimeout(signalTimeout)
        }
        process.once('SIGCHLD', onSignal)
      })
    )
  }

  const child = spawn(command, args, options)
  pending.push(
    new Promise((resolve, reject) => {
      child.stdout.on('data', (data) => {
        if (Buffer.from(data).toString().includes('child_process')) {
          hasDir = true
        }
      })

      child.on('close', resolve)
      child.on('error', reject)
    })
  )

  try {
    await Promise.all(pending)
  } finally {
    clearTimeout(signalTimeout)
    if (onSignal) process.off('SIGCHLD', onSignal)
  }

  t.ok(hasDir, 'the directory listing includes the child_process directory')
})

test('child_process.exec(command[,options],callback)', async (t) => {
  const pending = []

  pending.push(
    new Promise((resolve, reject) => {
      exec(listDirectory, (err, stdout) => {
        if (err) {
          return reject(err)
        }

        t.ok(stdout, 'there is stdout')
        resolve()
      })
    })
  )

  pending.push(
    new Promise((resolve, reject) => {
      const command = isWindows
        ? 'echo intentional-error 1>&2 & exit /b 1'
        : 'ls /not/a/directory'
      exec(command, (err, stdout, stderr) => {
        if (err) {
          return reject(err)
        }

        t.ok(!stdout, 'there is no stdout')
        t.ok(stderr, 'there is no stdout')
        resolve()
      })
    })
  )

  await Promise.all(pending)
})

test('await child_process.exec(command)', async (t) => {
  const { stdout } = await exec(listDirectory)
  t.ok(stdout && stdout.length, 'stdout from await exec() has output')
})

test('child_process.spawn with space-containing args', async (t) => {
  const os = (await import('oro:os')).default
  const { spawn } = await import('oro:child_process')
  let output = ''

  if (/win32/i.test(os.platform())) {
    // Use cmd.exe to echo an argument with a space; verifies quoting
    const child = spawn('cmd.exe', ['/c', 'echo', 'A B'])
    await new Promise((resolve, reject) => {
      child.stdout.on('data', (data) => {
        output += data.toString()
      })
      child.on('exit', resolve)
      child.on('error', reject)
    })
    // cmd.exe echo adds CRLF and may include surrounding quotes; normalize whitespace
    t.ok(
      /A\s+B/.test(output),
      `windows echo preserved space: ${JSON.stringify(output)}`
    )
  } else {
    // Use /bin/echo directly with an arg that contains a space
    const child = spawn('/bin/echo', ['A B'])
    await new Promise((resolve, reject) => {
      child.stdout.on('data', (data) => {
        output += data.toString()
      })
      child.on('exit', resolve)
      child.on('error', reject)
    })
    output = output.trim()
    t.equal(output, 'A B', 'posix echo preserved a single space in arg')
  }
})

test('child_process.spawn POSIX quoting: quotes and trailing backslash', async (t) => {
  const os = (await import('oro:os')).default
  if (/win32/i.test(os.platform())) return t.pass('skipped on windows')

  const { spawn } = await import('oro:child_process')
  const { default: fs } = await import('oro:fs')

  async function run (prog, args) {
    let out = ''
    await new Promise((resolve, reject) => {
      const child = spawn(prog, args)
      child.stdout.on('data', (d) => {
        out += d.toString()
      })
      child.on('exit', resolve)
      child.on('error', reject)
    })
    return out
  }

  async function available (p) {
    return await new Promise((resolve) => {
      fs.access(p, 0, (err) => resolve(!err))
    })
  }

  const argWithQuotes = 'A "B" C'
  const argWithTrailingBackslash = 'foo\\'

  let printfPath = null
  if (await available('/usr/bin/printf')) printfPath = '/usr/bin/printf'
  else if (await available('/bin/printf')) printfPath = '/bin/printf'

  if (printfPath) {
    const out1 = (await run(printfPath, ['%s\n', argWithQuotes])).trim()
    t.equal(out1, argWithQuotes, 'printf preserved quotes in argument')

    const out2 = (
      await run(printfPath, ['%s\n', argWithTrailingBackslash])
    ).trim()
    t.equal(
      out2,
      argWithTrailingBackslash,
      'printf preserved trailing backslash in argument'
    )

    const exact = await run(printfPath, [
      '[%s][%s][%s]',
      '',
      ' A ',
      'tail '
    ])
    t.equal(
      exact,
      '[][ A ][tail ]',
      'spawn preserved empty and edge-whitespace arguments'
    )
  } else {
    // Fallback to echo: not as strict but still checks preservation
    const out1 = (await run('/bin/echo', [argWithQuotes])).trim()
    t.equal(out1, argWithQuotes, 'echo preserved quotes in argument')

    const out2 = (await run('/bin/echo', [argWithTrailingBackslash])).trim()
    t.equal(
      out2,
      argWithTrailingBackslash,
      'echo preserved trailing backslash in argument'
    )
  }
})

test('child_process exec and execFile preserve exact output', async (t) => {
  if (/win32/i.test(os.platform())) return t.pass('skipped on windows')

  const shellResult = await exec("printf 'without-newline'")
  t.equal(
    shellResult.stdout,
    'without-newline',
    'exec preserved output without adding a newline'
  )

  const fileResult = await execFile('/usr/bin/printf', ['%s', 'A B'])
  t.equal(
    fileResult.stdout,
    'A B',
    'execFile invoked the executable directly with tokenized arguments'
  )

  const environmentResult = await execFile('/usr/bin/env', [], {
    env: { ORO_CHILD_PROCESS_ENV: 'isolated' }
  })
  t.equal(
    environmentResult.stdout,
    'ORO_CHILD_PROCESS_ENV=isolated\n',
    'an explicit environment replaces inherited values'
  )
})

test('child_process rejects unrepresentable argv characters', (t) => {
  t.throws(
    () => spawn('echo', ['invalid\u0001argument']),
    /U\+0001/,
    'the IPC argument delimiter is rejected'
  )
})

test('child_process preserves fast child output through close', async (t) => {
  const results = []
  for (let index = 0; index < 12; index++) {
    results.push(await new Promise((resolve, reject) => {
      const expectedStdout = `stdout-${index}`
      const expectedStderr = `stderr-${index}`
      const child = spawn('node', [
        '-e',
        `process.stdout.write(${JSON.stringify(expectedStdout)});` +
          `process.stderr.write(${JSON.stringify(expectedStderr)})`
      ])
      let stdout = ''
      let stderr = ''
      let sawExit = false
      const timeout = setTimeout(() => {
        try {
          child.kill()
        } catch {}
        reject(new Error(`fast child ${index} did not close`))
      }, 5000)

      child.stdout.on('data', (data) => {
        stdout += data.toString()
      })
      child.stderr.on('data', (data) => {
        stderr += data.toString()
      })
      child.once('exit', () => {
        sawExit = true
      })
      child.once('error', (error) => {
        clearTimeout(timeout)
        reject(error)
      })
      child.once('close', () => {
        clearTimeout(timeout)
        resolve({ expectedStdout, expectedStderr, stdout, stderr, sawExit })
      })
    }))
  }

  for (const result of results) {
    t.equal(result.stdout, result.expectedStdout, 'stdout is complete at close')
    t.equal(result.stderr, result.expectedStderr, 'stderr is complete at close')
    t.ok(result.sawExit, 'exit is emitted before close')
  }
})

test('child_process.spawn Windows quoting: trailing backslash in arg', async (t) => {
  const os = (await import('oro:os')).default
  if (!/win32/i.test(os.platform())) return t.pass('skipped on non-windows')

  const { spawn } = await import('oro:child_process')
  const arg = 'C:\\Program Files\\Foo\\'
  let output = ''
  const child = spawn('node', [
    '-e',
    'process.stdout.write(process.argv[1])',
    arg
  ])
  await new Promise((resolve, reject) => {
    child.stdout.on('data', (d) => {
      output += d.toString()
    })
    child.on('exit', resolve)
    child.on('error', reject)
  })
  output = output.replace(/\r\n$/, '').replace(/\n$/, '')
  t.equal(
    output,
    arg,
    'node received the exact argument with trailing backslash'
  )
})
