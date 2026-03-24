import xterm from 'npm:@xterm/xterm'
import fitAddon from 'npm:@xterm/addon-fit'
import canvasAddon from 'npm:@xterm/addon-canvas'
import searchAddon from 'npm:@xterm/addon-search'
import attachAddon from 'npm:@xterm/addon-attach'

import fs from 'oro:fs/promises'
import { spawn } from 'oro:child_process'
import process from 'oro:process'
import path, { posix as posixPath } from 'oro:path'

const { Terminal } = xterm
const { FitAddon } = fitAddon
const { CanvasAddon } = canvasAddon
const { SearchAddon } = searchAddon
const { AttachAddon } = attachAddon

const posix = posixPath
const decoder = new TextDecoder()
const WebSocketCtor = globalThis.WebSocket

const DEFAULT_USER = 'visitor'
const PROMPT_COLOR = '\x1b[38;5;214m'
const PATH_COLOR = '\x1b[38;5;39m'
const ERROR_COLOR = '\x1b[38;5;203m'
const RESET = '\x1b[0m'

const FHS_DIRECTORIES = [
  'bin',
  'boot',
  'dev',
  'etc',
  'home',
  `home/${DEFAULT_USER}`,
  'lib',
  'lib64',
  'mnt',
  'opt',
  'proc',
  'root',
  'run',
  'sbin',
  'srv',
  'tmp',
  'usr',
  'usr/bin',
  'usr/lib',
  'usr/share',
  'var',
  'var/log',
  'var/tmp',
  'var/spool'
]

async function pathExists (target) {
  try {
    await fs.access(target)
    return true
  } catch {
    return false
  }
}

function toSystemPath (root, relPath) {
  if (!relPath || relPath === '.' || relPath === '/') {
    return root
  }
  const segments = relPath.split('/').filter(Boolean)
  return path.join(root, ...segments)
}

async function ensureFile (root, relPath, contents) {
  const filePath = toSystemPath(root, relPath)
  if (await pathExists(filePath)) return
  await fs.mkdir(path.dirname(filePath), { recursive: true })
  await fs.writeFile(filePath, contents, 'utf8')
}

async function initFileSystem () {
  const writableBase = path.DATA ?? path.HOME ?? path.resolve('.')
  const root = path.join(writableBase, 'orosh')
  await fs.mkdir(root, { recursive: true })
  for (const rel of FHS_DIRECTORIES) {
    await fs.mkdir(toSystemPath(root, rel), { recursive: true })
  }
  await ensureFile(
    root,
    'etc/orosh-release',
    `NAME=orosh\nVERSION=1\nHOME=/home/${DEFAULT_USER}\n`
  )
  await ensureFile(
    root,
    'etc/motd',
    `Welcome to orosh!\n  data root : ${root}\n  home      : /home/${DEFAULT_USER}\nType 'help' to list the supported commands.\n`
  )
  await ensureFile(
    root,
    `home/${DEFAULT_USER}/readme.txt`,
    'Each orosh session is sandboxed to this tree.\nFeel free to create directories, run commands, and try pipelines!\n'
  )
  return root
}

function formatBytes (bytes) {
  if (bytes < 1024) return `${bytes} B`
  const units = ['KB', 'MB', 'GB', 'TB']
  let value = bytes
  let unit = 'KB'
  for (const candidate of units) {
    unit = candidate
    value /= 1024
    if (value < 1024) break
  }
  return `${value.toFixed(2)} ${unit}`
}

class LoopbackSocket extends EventTarget {
  constructor () {
    super()
    this.readyState = WebSocketCtor?.OPEN ?? 1
    this.binaryType = 'arraybuffer'
  }

  send (data) {
    const payload = typeof data === 'string' ? data : decoder.decode(data)
    this.dispatchEvent(new MessageEvent('message', { data: payload }))
  }

  close () {
    if (this.readyState === (WebSocketCtor?.CLOSED ?? 3)) return
    this.readyState = WebSocketCtor?.CLOSED ?? 3
    const CloseCtor = globalThis.CloseEvent ?? MessageEvent
    this.dispatchEvent(new CloseCtor('close'))
  }
}

function tokenize (input) {
  const tokens = []
  let current = ''
  let quote = null
  let escape = false

  const commit = () => {
    if (!current.length) return
    tokens.push({ type: 'word', value: current })
    current = ''
  }

  for (let i = 0; i < input.length; i++) {
    const char = input[i]
    if (escape) {
      current += char
      escape = false
      continue
    }
    if (char === '\\') {
      escape = true
      continue
    }
    if (quote) {
      if (char === quote) {
        quote = null
      } else {
        current += char
      }
      continue
    }
    if (char === "'" || char === '"') {
      quote = char
      continue
    }
    if (char === '|') {
      commit()
      tokens.push({ type: 'pipe', value: '|' })
      continue
    }
    if (char === '>') {
      commit()
      let op = '>'
      if (input[i + 1] === '>') {
        op = '>>'
        i += 1
      }
      tokens.push({ type: 'redir', value: op })
      continue
    }
    if (char === '<') {
      commit()
      tokens.push({ type: 'redir', value: '<' })
      continue
    }
    if (/\s/.test(char)) {
      commit()
      continue
    }
    current += char
  }
  commit()
  return tokens
}

function parsePipeline (input) {
  const tokens = tokenize(input)
  const commands = []
  let current = { args: [], input: null, output: null }
  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i]
    if (token.type === 'word') {
      current.args.push(token.value)
      continue
    }
    if (token.type === 'pipe') {
      if (!current.args.length && !current.input && !current.output) {
        throw new Error('Unexpected pipe with empty command')
      }
      commands.push(current)
      current = { args: [], input: null, output: null }
      continue
    }
    if (token.type === 'redir') {
      const next = tokens[++i]
      if (!next || next.type !== 'word') {
        throw new Error(`Redirection ${token.value} requires a target`)
      }
      if (token.value === '<') {
        current.input = next.value
      } else {
        current.output = { path: next.value, append: token.value === '>>' }
      }
    }
  }
  commands.push(current)
  return commands.filter((cmd) => cmd.args.length || cmd.input || cmd.output)
}

function parseAliasAssignment (value) {
  const eqIndex = value.indexOf('=')
  if (eqIndex === -1) {
    return null
  }
  const name = value.slice(0, eqIndex)
  let body = value.slice(eqIndex + 1)
  if (
    (body.startsWith('"') && body.endsWith('"')) ||
    (body.startsWith("'") && body.endsWith("'"))
  ) {
    body = body.slice(1, -1)
  }
  if (!name) return null
  return { name, body }
}

class OroshShell {
  constructor ({ term, socket, searchAddon }) {
    this.term = term
    this.socket = socket
    this.searchAddon = searchAddon
    this.fsRoot = null
    this.cwd = `/home/${DEFAULT_USER}`
    this.home = this.cwd
    this.input = ''
    this.history = []
    this.historyIndex = -1
    this.busy = false
    this.aliases = new Map()
    this.currentChild = null
    this.interrupted = false
    this.env = {
      ...process.env,
      HOME: this.home,
      PWD: this.cwd,
      SHELL: 'orosh'
    }
    this.commandQueue = Promise.resolve()
    this.builtins = {
      help: this.cmdHelp.bind(this),
      pwd: this.cmdPwd.bind(this),
      ls: this.cmdLs.bind(this),
      cd: this.cmdCd.bind(this),
      cat: this.cmdCat.bind(this),
      touch: this.cmdTouch.bind(this),
      mkdir: this.cmdMkdir.bind(this),
      rm: this.cmdRm.bind(this),
      rmdir: this.cmdRmdir.bind(this),
      mv: this.cmdMv.bind(this),
      cp: this.cmdCp.bind(this),
      stat: this.cmdStat.bind(this),
      du: this.cmdDu.bind(this),
      echo: this.cmdEcho.bind(this),
      printf: this.cmdPrintf.bind(this),
      env: this.cmdEnv.bind(this),
      whoami: this.cmdWhoami.bind(this),
      alias: this.cmdAlias.bind(this),
      unalias: this.cmdUnalias.bind(this),
      curl: this.cmdCurl.bind(this),
      wget: this.cmdWget.bind(this),
      clear: this.cmdClear.bind(this),
      search: this.cmdSearch.bind(this)
    }
  }

  async start ({ beforePrompt } = {}) {
    this.fsRoot = await initFileSystem()
    await this.printMotd()
    if (beforePrompt) {
      const message = beforePrompt.endsWith('\n')
        ? beforePrompt
        : `${beforePrompt}\n`
      this.output(message.replace(/\n/g, '\r\n'))
    }
    this.prompt()
    this.term.onData((data) => this.handleInput(data))
  }

  async printMotd () {
    const motdPath = toSystemPath(this.fsRoot, 'etc/motd')
    if (await pathExists(motdPath)) {
      const motd = await fs.readFile(motdPath, 'utf8')
      this.output(`\n${motd.replace(/\n/g, '\r\n')}\r\n`)
    }
  }

  output (text) {
    if (!text) return
    this.socket.send(text)
  }

  prompt () {
    this.term.write(
      `${PROMPT_COLOR}orosh${RESET} ${PATH_COLOR}${this.cwd}${RESET} $ `
    )
  }

  async handleInput (data) {
    if (this.busy) {
      if (data === '\u0003') {
        this.term.write('^C\r\n')
        this.interrupted = true
        const child = this.currentChild
        if (child && !child.killed) {
          try {
            child.kill('SIGINT')
          } catch {}
        }
      }
      return
    }

    if (data === '\r') {
      const line = this.input
      this.term.write('\r\n')
      this.input = ''
      if (line.trim()) {
        this.history.push(line)
        this.historyIndex = this.history.length
      }
      await this.run(line)
      return
    }

    if (data === '\u0003') {
      this.term.write('^C\r\n')
      this.input = ''
      this.interrupted = false
      this.prompt()
      return
    }

    if (data === '\u007F') {
      if (!this.input.length) return
      this.input = this.input.slice(0, -1)
      this.term.write('\b \b')
      return
    }

    if (data === '\x1b[A') {
      this.navigateHistory(-1)
      return
    }

    if (data === '\x1b[B') {
      this.navigateHistory(1)
      return
    }

    if (data === '\t') {
      this.term.write('  ')
      this.input += '  '
      return
    }

    this.input += data
    this.term.write(data)
  }

  navigateHistory (delta) {
    if (!this.history.length) return
    this.historyIndex = Math.min(
      this.history.length - 1,
      Math.max(0, this.historyIndex + delta)
    )
    const nextValue = this.history[this.historyIndex] ?? ''
    this.replaceInput(nextValue)
  }

  replaceInput (value) {
    const backspaces = '\b \b'.repeat(this.input.length)
    this.term.write(backspaces)
    this.input = value
    if (value) {
      this.term.write(value)
    }
  }

  async run (line) {
    if (!line.trim()) {
      this.prompt()
      return
    }

    this.busy = true
    this.interrupted = false
    try {
      const commands = parsePipeline(line)
      let piped = ''
      let lastResult = null
      for (const command of commands) {
        const input = command.input
          ? await this.readFileForCommand(command.input)
          : piped
        const expanded = this.expandAlias(command.args)
        command.args = expanded
        const result = await this.executeCommand(command, input)
        piped = result.stdout ?? ''
        lastResult = result
        if (command.output) {
          await this.writeFileForCommand(command.output, piped)
        }
        if (result.stderr) {
          this.output(
            `${ERROR_COLOR}${result.stderr.replace(/\n/g, '\r\n')}${RESET}\r\n`
          )
        }
        if (this.interrupted) {
          break
        }
      }
      if (
        !this.interrupted &&
        lastResult &&
        lastResult.stdout &&
        !commands[commands.length - 1].output
      ) {
        const normalized = lastResult.stdout.endsWith('\n')
          ? lastResult.stdout
          : `${lastResult.stdout}\n`
        this.output(normalized.replace(/\n/g, '\r\n'))
      }
    } catch (err) {
      this.output(`${ERROR_COLOR}${err.message}${RESET}\r\n`)
    } finally {
      this.busy = false
      this.interrupted = false
      this.prompt()
    }
  }

  expandAlias (args) {
    if (!args.length) return args
    const seen = new Set()
    let expanded = [...args]
    while (expanded.length) {
      const target = expanded[0]
      const aliasValue = this.aliases.get(target)
      if (!aliasValue || seen.has(target)) break
      seen.add(target)
      const injected = tokenize(aliasValue)
        .filter((token) => token.type === 'word')
        .map((token) => token.value)
      expanded = [...injected, ...expanded.slice(1)]
    }
    return expanded
  }

  async executeCommand (command, stdin) {
    const [name, ...args] = command.args
    if (!name) {
      return { stdout: stdin, stderr: '', code: 0 }
    }
    const handler = this.builtins[name]
    if (handler) {
      return handler({ args, stdin })
    }
    return this.spawnProcess(name, args, stdin)
  }

  async spawnProcess (cmd, args, stdin) {
    const cwdTarget = this.resolveVirtual('.')
    return await new Promise((resolve) => {
      let stdout = ''
      let stderr = ''
      const child = spawn(cmd, args, { cwd: cwdTarget.real, env: this.env })
      this.currentChild = child
      const clearCurrentChild = () => {
        if (this.currentChild === child) {
          this.currentChild = null
        }
      }
      if (stdin && child.stdin) {
        child.stdin.write(stdin)
        child.stdin.end()
      }
      child.stdout?.on('data', (chunk) => {
        stdout += chunk.toString()
      })
      child.stderr?.on('data', (chunk) => {
        stderr += chunk.toString()
      })
      child.on('error', (err) => {
        clearCurrentChild()
        resolve({ stdout: '', stderr: `${cmd}: ${err.message}\n`, code: 127 })
      })
      child.on('exit', (code) => {
        clearCurrentChild()
        resolve({ stdout, stderr, code: code ?? 0 })
      })
    })
  }

  resolveVirtual (target = '.') {
    const tilde = target.startsWith('~')
      ? posix.join(this.home, target.slice(1))
      : target
    const absolute = tilde.startsWith('/') ? tilde : posix.join(this.cwd, tilde)
    let normalized = posix.normalize(absolute)
    if (!normalized.startsWith('/')) {
      normalized = `/${normalized}`
    }
    const relative = normalized === '/' ? '' : normalized.slice(1)
    const segments = relative ? relative.split('/').filter(Boolean) : []
    const realPath = path.resolve(this.fsRoot, ...segments)
    const relativeCheck = path.relative(this.fsRoot, realPath)
    if (relativeCheck.startsWith('..')) {
      throw new Error(`Access outside virtual root: ${target}`)
    }
    return { virtual: normalized || '/', real: realPath }
  }

  async readFileForCommand (virtualPath) {
    const target = this.resolveVirtual(virtualPath)
    return await fs.readFile(target.real, 'utf8')
  }

  async writeFileForCommand (descriptor, content) {
    const target = this.resolveVirtual(descriptor.path)
    await fs.mkdir(path.dirname(target.real), { recursive: true })
    if (descriptor.append) {
      await fs.appendFile(target.real, content)
    } else {
      await fs.writeFile(target.real, content)
    }
  }

  async cmdHelp () {
    const sections = [
      'orosh builtins:',
      '  help, pwd, ls, cd, cat, touch, mkdir, rm, rmdir, mv, cp',
      '  stat, du, env, whoami, alias, unalias, echo, printf, curl, wget, clear, search',
      'Features:',
      '  • Virtual POSIX filesystem rooted at path.DATA',
      '  • Pipelines via the | operator',
      '  • I/O redirection with >, >>, and <',
      '  • External commands via child_process',
      '  • fetch-powered curl/wget and full xterm addon suite'
    ]
    return { stdout: `${sections.join('\n')}\n`, stderr: '', code: 0 }
  }

  async cmdPwd () {
    return { stdout: `${this.cwd}\n`, stderr: '', code: 0 }
  }

  async cmdLs ({ args }) {
    const flags = new Set()
    const targets = []
    for (const arg of args) {
      if (arg.startsWith('-')) flags.add(arg)
      else targets.push(arg)
    }
    const entries = targets.length ? targets : ['.']
    let output = ''
    for (const entry of entries) {
      const target = this.resolveVirtual(entry)
      const stats = await fs.stat(target.real)
      if (!stats.isDirectory()) {
        output += `${entry}\n`
        continue
      }
      const dirents = await fs.readdir(target.real, { withFileTypes: true })
      const names = dirents
        .filter((dirent) => flags.has('-a') || !dirent.name.startsWith('.'))
        .map((dirent) =>
          dirent.isDirectory() ? `${dirent.name}/` : dirent.name
        )
      const sorted = names.sort()
      output += `${target.virtual}:\n${sorted.join('  ')}\n`
    }
    return { stdout: output, stderr: '', code: 0 }
  }

  async cmdCd ({ args }) {
    const dir = args[0] ?? this.home
    const target = this.resolveVirtual(dir)
    const stats = await fs.stat(target.real)
    if (!stats.isDirectory()) {
      return { stdout: '', stderr: `cd: ${dir}: Not a directory\n`, code: 1 }
    }
    this.cwd = target.virtual
    this.env.PWD = this.cwd
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdCat ({ args, stdin }) {
    if (!args.length) {
      return { stdout: stdin, stderr: '', code: 0 }
    }
    let output = ''
    for (const file of args) {
      const target = this.resolveVirtual(file)
      output += await fs.readFile(target.real, 'utf8')
    }
    return { stdout: output, stderr: '', code: 0 }
  }

  async cmdTouch ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'touch: file required\n', code: 1 }
    }
    const now = new Date()
    for (const file of args) {
      const target = this.resolveVirtual(file)
      await fs.mkdir(path.dirname(target.real), { recursive: true })
      await fs.writeFile(target.real, '', { flag: 'a' })
      try {
        await fs.utimes(target.real, now, now)
      } catch {}
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdMkdir ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'mkdir: path required\n', code: 1 }
    }
    for (const dir of args) {
      const target = this.resolveVirtual(dir)
      await fs.mkdir(target.real, { recursive: true })
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdRm ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'rm: path required\n', code: 1 }
    }
    const recursive =
      args.includes('-r') || args.includes('-rf') || args.includes('-fr')
    for (const entry of args) {
      if (entry.startsWith('-')) continue
      const target = this.resolveVirtual(entry)
      await fs.rm(target.real, { recursive, force: recursive })
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdRmdir ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'rmdir: path required\n', code: 1 }
    }
    for (const dir of args) {
      const target = this.resolveVirtual(dir)
      await fs.rm(target.real, { recursive: false })
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdMv ({ args }) {
    if (args.length < 2) {
      return {
        stdout: '',
        stderr: 'mv: source and destination required\n',
        code: 1
      }
    }
    const sources = args.slice(0, -1)
    const destination = this.resolveVirtual(args.at(-1))
    const destStats = (await pathExists(destination.real))
      ? await fs.stat(destination.real)
      : null
    for (const src of sources) {
      const source = this.resolveVirtual(src)
      const targetPath =
        destStats && destStats.isDirectory()
          ? path.join(destination.real, path.basename(source.real))
          : destination.real
      await fs.rename(source.real, targetPath)
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdCp ({ args }) {
    if (args.length < 2) {
      return {
        stdout: '',
        stderr: 'cp: source and destination required\n',
        code: 1
      }
    }
    const sources = args.slice(0, -1)
    const destination = this.resolveVirtual(args.at(-1))
    const destStats = (await pathExists(destination.real))
      ? await fs.stat(destination.real)
      : null
    for (const src of sources) {
      const source = this.resolveVirtual(src)
      const targetPath =
        destStats && destStats.isDirectory()
          ? path.join(destination.real, path.basename(source.real))
          : destination.real
      await fs.cp(source.real, targetPath, { recursive: true })
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdStat ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'stat: path required\n', code: 1 }
    }
    const lines = []
    for (const entry of args) {
      const target = this.resolveVirtual(entry)
      const stats = await fs.stat(target.real)
      lines.push(`${target.virtual}:`)
      lines.push(`  size : ${stats.size}`)
      lines.push(`  mode : ${stats.mode.toString(8)}`)
      lines.push(`  mtime: ${stats.mtime.toISOString()}`)
      lines.push(
        `  type : ${stats.isDirectory() ? 'directory' : stats.isFile() ? 'file' : 'other'}`
      )
    }
    return { stdout: `${lines.join('\n')}\n`, stderr: '', code: 0 }
  }

  async cmdDu ({ args }) {
    const targets = args.length ? args : ['.']
    const lines = []
    for (const entry of targets) {
      const target = this.resolveVirtual(entry)
      const size = await this.walkSize(target.real)
      lines.push(`${formatBytes(size)}\t${target.virtual}`)
    }
    return { stdout: `${lines.join('\n')}\n`, stderr: '', code: 0 }
  }

  async walkSize (realPath) {
    const stats = await fs.stat(realPath)
    if (!stats.isDirectory()) {
      return stats.size
    }
    const dirents = await fs.readdir(realPath, { withFileTypes: true })
    let total = stats.size
    for (const dirent of dirents) {
      const nextPath = path.join(realPath, dirent.name)
      total += await this.walkSize(nextPath)
    }
    return total
  }

  async cmdEcho ({ args, stdin }) {
    const message = args.length ? args.join(' ') : stdin
    return { stdout: `${message}\n`, stderr: '', code: 0 }
  }

  async cmdPrintf ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'printf: format string required\n', code: 1 }
    }
    const [format, ...values] = args
    let index = 0
    const rendered = format.replace(/%[sd]/g, (token) => {
      const value = values[index++] ?? ''
      if (token === '%d') {
        const num = Number(value)
        return Number.isFinite(num) ? String(num) : '0'
      }
      return value
    })
    return { stdout: rendered, stderr: '', code: 0 }
  }

  async cmdEnv () {
    const entries = Object.entries(this.env).sort(([a], [b]) =>
      a.localeCompare(b)
    )
    const lines = entries.map(([key, value]) => `${key}=${value}`)
    return { stdout: `${lines.join('\n')}\n`, stderr: '', code: 0 }
  }

  async cmdWhoami () {
    const user = this.env.USER || this.env.LOGNAME || DEFAULT_USER
    return { stdout: `${user}\n`, stderr: '', code: 0 }
  }

  async cmdAlias ({ args }) {
    if (!args.length) {
      const list = [...this.aliases.entries()]
        .map(([name, body]) => `alias ${name}='${body}'`)
        .join('\n')
      return { stdout: list ? `${list}\n` : '', stderr: '', code: 0 }
    }
    for (const entry of args) {
      const parsed = parseAliasAssignment(entry)
      if (!parsed) {
        return {
          stdout: '',
          stderr: `alias: invalid assignment ${entry}\n`,
          code: 1
        }
      }
      this.aliases.set(parsed.name, parsed.body)
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdUnalias ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'unalias: name required\n', code: 1 }
    }
    for (const name of args) {
      this.aliases.delete(name)
    }
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdCurl ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'curl: URL required\n', code: 1 }
    }
    let destination = null
    const files = []
    for (let i = 0; i < args.length; i++) {
      if (args[i] === '-o') {
        destination = args[i + 1]
        i += 1
      } else {
        files.push(args[i])
      }
    }
    const url = files[0]
    const response = await fetch(url)
    const body = await response.arrayBuffer()
    if (destination) {
      const target = this.resolveVirtual(destination)
      await fs.mkdir(path.dirname(target.real), { recursive: true })
      await fs.writeFile(target.real, new Uint8Array(body))
      return { stdout: `saved ${destination}\n`, stderr: '', code: 0 }
    }
    return { stdout: decoder.decode(body), stderr: '', code: 0 }
  }

  async cmdWget ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'wget: URL required\n', code: 1 }
    }
    const url = args[0]
    const parsed = this.safeURL(url)
    const defaultName =
      parsed?.pathname?.split('/').filter(Boolean).pop() ?? 'index.html'
    const dest = this.resolveVirtual(args[1] ?? defaultName)
    const response = await fetch(url)
    const body = new Uint8Array(await response.arrayBuffer())
    await fs.mkdir(path.dirname(dest.real), { recursive: true })
    await fs.writeFile(dest.real, body)
    return { stdout: `downloaded ${dest.virtual}\n`, stderr: '', code: 0 }
  }

  safeURL (value) {
    try {
      return new URL(value)
    } catch {
      return null
    }
  }

  async cmdClear () {
    this.term.write('\u001bc')
    return { stdout: '', stderr: '', code: 0 }
  }

  async cmdSearch ({ args }) {
    if (!args.length) {
      return { stdout: '', stderr: 'search: pattern required\n', code: 1 }
    }
    const pattern = args.join(' ')
    const found = this.searchAddon.findNext(pattern, { incremental: true })
    if (!found) {
      return { stdout: '', stderr: `search: '${pattern}' not found\n`, code: 1 }
    }
    return { stdout: '', stderr: '', code: 0 }
  }
}

async function bootstrap () {
  const container = document.getElementById('terminal')
  const term = new Terminal({
    convertEol: true,
    cols: 120,
    rows: 32,
    fontSize: 14,
    theme: {
      background: '#050505',
      foreground: '#fefefe',
      cursor: '#ff8a3c'
    }
  })
  const fitAddon = new FitAddon()
  const canvasAddon = new CanvasAddon()
  const searchAddon = new SearchAddon()
  const loopback = new LoopbackSocket()
  const attachAddon = new AttachAddon(loopback, { bidirectional: false })

  term.loadAddon(fitAddon)
  term.loadAddon(canvasAddon)
  term.loadAddon(searchAddon)
  term.loadAddon(attachAddon)

  term.open(container)
  fitAddon.fit()
  window.addEventListener('resize', () => fitAddon.fit())

  const shell = new OroshShell({ term, socket: loopback, searchAddon })
  await shell.start({
    beforePrompt: 'mounted xterm addons: fit, canvas, search, attach'
  })
}

bootstrap().catch((err) => {
  console.error('[orosh] failed to boot shell', err)
})
