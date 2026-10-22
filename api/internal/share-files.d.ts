/**
 * Validates public File objects before any data is read or native UI opens.
 * @param {File[]} files
 * @returns {number} Total byte count.
 */
export function validateFiles(files: File[]): number;
/** @type {number} Maximum bytes copied to Android's temporary share cache. */
export const MAX_SHARE_BYTES: number;
/** Maximum number of Android share attachments per chooser request. */
export const MAX_SHARE_FILES: 16;
