#!/usr/bin/env node
'use strict'

const { main } = require('./tooling/main.cjs')

main(process.argv.slice(2)).catch(error => {
  process.stderr.write(`Oro Language Server: ${error.message}\n`)
  process.exitCode = 1
})
