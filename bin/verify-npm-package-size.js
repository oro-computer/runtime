#!/usr/bin/env node

import { existsSync, readFileSync, realpathSync, statSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

/**
 * Repository budget for compressed npm packages, in bytes. npm uploads tarballs
 * inside base64 JSON attachments, so leave room for encoding and metadata.
 * This is a release budget, not a documented npm registry limit.
 * @type {number}
 */
export const NPM_PACKAGE_SIZE_LIMIT = 190_000_000

/**
 * @typedef {object} NpmPackageSize
 * @property {string} filename
 * @property {number} size Compressed tarball bytes.
 */

function verifySize (filename, size) {
  if (!Number.isSafeInteger(size) || size <= 0) {
    throw new Error(`${filename} must have a positive integer compressed size; received ${size}`)
  }
  if (size > NPM_PACKAGE_SIZE_LIMIT) {
    throw new Error(
      `${filename} is ${size} compressed bytes, exceeding the repository npm package budget of ` +
      `${NPM_PACKAGE_SIZE_LIMIT} bytes. Its base64 attachment alone requires ${4 * Math.ceil(size / 3)} bytes. ` +
      'Reduce the package contents before publishing.'
    )
  }
  return size
}

/**
 * Check an existing tarball before installing or publishing it.
 * @param {string} filename
 * @returns {number} Compressed tarball bytes.
 * @throws {Error} If the archive is missing, empty, not a file, or over budget.
 */
export function verifyNpmPackageSize (filename) {
  const archive = statSync(filename)
  if (!archive.isFile()) throw new Error(`npm package is not a regular file: ${filename}`)
  return verifySize(filename, archive.size)
}

/**
 * Check the compressed sizes computed by npm pack immediately after packing.
 * @param {string} output JSON emitted by npm pack --json.
 * @returns {NpmPackageSize[]}
 * @throws {Error} If npm's report is invalid or any package exceeds the budget.
 */
export function verifyNpmPackSizes (output) {
  const packages = JSON.parse(output)
  if (!Array.isArray(packages) || packages.length === 0) {
    throw new Error('npm pack must report at least one package')
  }
  return packages.map(entry => {
    if (!entry || typeof entry.filename !== 'string' || entry.filename.length === 0) {
      throw new Error('npm pack must report a filename for every package')
    }
    return { filename: entry.filename, size: verifySize(entry.filename, entry.size) }
  })
}

const entrypoint = process.argv[1] && existsSync(process.argv[1]) ? pathToFileURL(realpathSync(process.argv[1])).href : null
if (import.meta.url === entrypoint) {
  try {
    const args = process.argv.slice(2)
    let packages
    if (args.length === 1 && args[0] === '--pack-json') {
      packages = verifyNpmPackSizes(readFileSync(0, 'utf8'))
    } else {
      if (args.length === 0 || args.some(argument => argument.startsWith('--'))) {
        throw new Error('Usage: verify-npm-package-size.js <archive.tgz> [...] | --pack-json (npm pack JSON on stdin)')
      }
      packages = args.map(filename => ({ filename, size: verifyNpmPackageSize(filename) }))
    }
    for (const { filename, size } of packages) {
      console.log(`Verified npm package size: ${filename} (${size}/${NPM_PACKAGE_SIZE_LIMIT} compressed bytes)`)
    }
  } catch (error) {
    console.error(error.message)
    process.exitCode = 1
  }
}
