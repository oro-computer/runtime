import { spawn, exec } from 'oro:child_process'
import process from 'oro:process'
import test from 'oro:test'
import os from 'oro:os'

test('child_process.spawn(command[,args[,options]])', async (t) => {
  const command = 'ls'
  const args = ['-la']
  const options = {}

  let hasDir = false

  const pending = []
  const child = spawn(command, args, options)

  if (/linux|darwin/i.test(os.platform())) {
    pending.push(
      new Promise((resolve, reject) => {
        const timeout = setTimeout(
          () => reject(new Error('Timed out aiting for SIGCHLD signal')),
          1000
        )

        process.once('SIGCHLD', () => {
          resolve()
          clearTimeout(timeout)
        })
      })
    )
  }

  pending.push(
    new Promise((resolve, reject) => {
      child.stdout.on('data', (data) => {
        if (Buffer.from(data).toString().includes('child_process')) {
          hasDir = true
        }
      })

      child.on('exit', resolve)
      child.on('error', reject)
    })
  )

  await Promise.all(pending)

  t.ok(hasDir, 'the ls command ran and discovered the child_process directory')
})

test('child_process.exec(command[,options],callback)', async (t) => {
  const pending = []

  pending.push(
    new Promise((resolve, reject) => {
      exec('ls -la', (err, stdout) => {
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
      exec('ls /not/a/directory', (err, stdout, stderr) => {
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
  const { stdout } = await exec('ls -la')
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

test('child_process.spawn Windows quoting: trailing backslash in arg', async (t) => {
  const os = (await import('oro:os')).default
  if (!/win32/i.test(os.platform())) return t.pass('skipped on non-windows')

  const { spawn } = await import('oro:child_process')
  const arg = 'C:\\Program Files\\Foo\\'
  let output = ''
  // Use PowerShell to print the argument exactly via -Args
  const child = spawn('powershell.exe', [
    '-NoLogo',
    '-Command',
    'Param([String]$Arg) Write-Output $Arg',
    '-Args',
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
    'powershell received the exact argument with trailing backslash'
  )
})
