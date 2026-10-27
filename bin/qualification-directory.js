import { mkdtempSync, realpathSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'

/**
 * Create a temporary qualification directory with a canonical filesystem path.
 * Models and language servers resolve source paths, so fixtures must use the
 * same spelling when the system temporary directory is a symlink or junction.
 * @param {string} prefix
 * @returns {string}
 */
export function createQualificationDirectory (prefix) {
  return realpathSync.native(mkdtempSync(path.join(tmpdir(), prefix)))
}
