import { spawn } from 'oro:child_process'
import Buffer from 'oro:buffer'
import { Headers } from 'oro:fetch'

const clientSource = String.raw`
const request = JSON.parse(Buffer.from(process.argv[1], 'base64').toString())

Promise.resolve()
  .then(async () => {
    const response = await fetch(request.url, request.init)
    process.stdout.write(JSON.stringify({
      type: 'head',
      status: response.status,
      statusText: response.statusText,
      url: response.url,
      redirected: response.redirected,
      headers: Array.from(response.headers.entries())
    }) + '\n')

    if (response.body) {
      const reader = response.body.getReader()
      while (true) {
        const { done, value } = await reader.read()
        if (done) break
        process.stdout.write(JSON.stringify({
          type: 'data',
          data: Buffer.from(value).toString('base64')
        }) + '\n')
      }
    }

    process.stdout.write(JSON.stringify({ type: 'end' }) + '\n')
  })
  .catch((error) => {
    process.stderr.write(error?.stack || String(error))
    process.exitCode = 1
  })
`

function createResponse (head, body) {
  const response = {
    status: head.status,
    statusText: head.statusText,
    url: head.url,
    redirected: head.redirected,
    headers: new Headers(head.headers),
    body,
    get ok () {
      return this.status >= 200 && this.status < 300
    },
    async arrayBuffer () {
      const reader = body.getReader()
      const chunks = []
      while (true) {
        const { done, value } = await reader.read()
        if (done) break
        chunks.push(Buffer.from(value))
      }
      const buffer = Buffer.concat(chunks)
      return buffer.buffer.slice(
        buffer.byteOffset,
        buffer.byteOffset + buffer.byteLength
      )
    },
    async text () {
      return Buffer.from(await this.arrayBuffer()).toString()
    },
    async json () {
      return JSON.parse(await this.text())
    }
  }

  return response
}

/**
 * Makes an HTTP request from an external Node process so MCP integration tests
 * exercise the same loopback boundary as desktop MCP clients.
 * @param {string|URL} input
 * @param {object} [init]
 * @returns {Promise<object>}
 */
export function fetch (input, init = {}) {
  const serialized = Buffer.from(
    JSON.stringify({ url: String(input), init })
  ).toString('base64')

  return new Promise((resolve, reject) => {
    const child = spawn('node', ['-e', clientSource, serialized])
    let stdout = ''
    let stderr = ''
    let settled = false
    let ended = false
    let cancelled = false
    let controller = null
    const timeout = setTimeout(() => {
      try {
        if (!child.killed) child.kill()
      } catch {}
      fail(new Error(`MCP HTTP request timed out: ${String(input)}`))
    }, 10_000)

    const body = new ReadableStream({
      start (value) {
        controller = value
      },
      cancel () {
        cancelled = true
        clearTimeout(timeout)
        try {
          if (!child.killed) child.kill()
        } catch {}
      }
    })

    const fail = (error) => {
      clearTimeout(timeout)
      if (!settled) {
        settled = true
        reject(error)
      } else if (!ended && !cancelled) {
        ended = true
        controller.error(error)
      }
    }

    const processLine = (line) => {
      if (!line) return

      let message
      try {
        message = JSON.parse(line)
      } catch (error) {
        fail(error)
        return
      }

      if (message.type === 'head') {
        if (!settled) {
          settled = true
          resolve(createResponse(message, body))
        }
      } else if (message.type === 'data') {
        controller.enqueue(Buffer.from(message.data, 'base64'))
      } else if (message.type === 'end' && !ended) {
        ended = true
        clearTimeout(timeout)
        controller.close()
      }
    }

    child.stdout.on('data', (chunk) => {
      stdout += chunk.toString()
      while (true) {
        const newline = stdout.indexOf('\n')
        if (newline === -1) break
        const line = stdout.slice(0, newline)
        stdout = stdout.slice(newline + 1)
        processLine(line)
      }
    })

    child.stderr.on('data', (chunk) => {
      stderr += chunk.toString()
    })

    child.on('error', fail)
    child.on('close', (code) => {
      if (stdout.length > 0) processLine(stdout)
      if (cancelled || ended) {
        clearTimeout(timeout)
        return
      }
      fail(
        new Error(
          stderr || `MCP HTTP client exited before completing (code ${code})`
        )
      )
    })
  })
}

export default fetch
