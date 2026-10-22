/* global File */
// @ts-check

/** @type {number} Maximum bytes copied to Android's temporary share cache. */
export const MAX_SHARE_BYTES = 8 * 1024 * 1024
/** Maximum number of Android share attachments per chooser request. */
export const MAX_SHARE_FILES = 16

/**
 * Validates public File objects before any data is read or native UI opens.
 * @param {File[]} files
 * @returns {number} Total byte count.
 */
export function validateFiles (files) {
  if (!Array.isArray(files) || files.length > MAX_SHARE_FILES) {
    throw new TypeError('Share files must be an array of at most 16 File objects')
  }
  let total = 0
  const names = new Set()
  for (const file of files) {
    if (typeof File !== 'function' || !(file instanceof File)) {
      throw new TypeError('Share files must contain File objects')
    }
    if (!file.name || file.name.length > 255 || (/[\\/]/.test(file.name) || [...file.name].some(c => c.charCodeAt(0) < 32 || c.charCodeAt(0) === 127)) ||
        file.name === '.' || file.name === '..' || file.name === '.oro-share-created') {
      throw new TypeError('Share file names must be safe base names of at most 255 UTF-8 bytes')
    }
    const encodedName = new TextEncoder().encode(file.name)
    if (encodedName.length > 255 || new TextDecoder('utf-8', { ignoreBOM: true }).decode(encodedName) !== file.name) {
      throw new TypeError('Share file names require valid Unicode and at most 255 UTF-8 bytes')
    }
    if (names.has(file.name)) throw new TypeError('Share file names must be distinct')
    names.add(file.name)
    if (file.type.length > 255 || (file.type && !/^[a-z0-9!#$&^_.+-]+\/[a-z0-9!#$&^_.+-]+$/i.test(file.type))) {
      throw new TypeError('Share file MIME type is invalid')
    }
    total += file.size
    if (!Number.isSafeInteger(total) || total > MAX_SHARE_BYTES) {
      throw new RangeError('Share files exceed the 8 MiB total limit')
    }
  }
  return total
}
