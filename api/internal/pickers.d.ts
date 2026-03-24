/**
 * @typedef {{
 *   description?: string,
 *   accept?: Record<string, string[]>
 * }} FilePickerAcceptType
 */
/**
 * @typedef {{
 *   id?: string,
 *   mode?: 'read' | 'readwrite',
 *   startIn?: FileSystemHandle | 'desktop' | 'documents' | 'downloads' | 'music' | 'pictures' | 'videos',
 * }} ShowDirectoryPickerOptions
 */
/**
 * Shows a directory picker which allows the user to select a directory.
 * @param {ShowDirectoryPickerOptions=} [options]
 * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Window/showDirectoryPicker}
 * @return {Promise<FileSystemDirectoryHandle[]>}
 */
export function showDirectoryPicker(options?: ShowDirectoryPickerOptions | undefined): Promise<FileSystemDirectoryHandle[]>;
/**
 * @typedef {{
 *   id?: string,
 *   excludeAcceptAllOption?: boolean,
 *   startIn?: FileSystemHandle | 'desktop' | 'documents' | 'downloads' | 'music' | 'pictures' | 'videos',
 *   types?: Array<FilePickerAcceptType>
 * }} ShowOpenFilePickerOptions
 */
/**
 * Shows a file picker that allows a user to select a file or multiple files
 * and returns a handle for each selected file.
 * @param {ShowOpenFilePickerOptions=} [options]
 * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Window/showOpenFilePicker}
 * @return {Promise<FileSystemFileHandle[]>}
 */
export function showOpenFilePicker(options?: ShowOpenFilePickerOptions | undefined): Promise<FileSystemFileHandle[]>;
/**
 * @typedef {{
 *   id?: string,
 *   excludeAcceptAllOption?: boolean,
 *   suggestedName?: string,
 *   startIn?: FileSystemHandle | 'desktop' | 'documents' | 'downloads' | 'music' | 'pictures' | 'videos',
 *   types?: Array<FilePickerAcceptType>
 * }} ShowSaveFilePickerOptions
 */
/**
 * Shows a file picker that allows a user to save a file by selecting an
 * existing file, or entering a name for a new file.
 * @param {ShowSaveFilePickerOptions=} [options]
 * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/Window/showSaveFilePicker}
 * @return {Promise<FileSystemHandle>}
 */
export function showSaveFilePicker(options?: ShowSaveFilePickerOptions | undefined): Promise<FileSystemHandle>;
/**
 * Key-value store for general usage by the file pickers"
 * @ignore
 */
export class Database {
    get(key: any): any;
    set(key: any, value: any): void;
}
/**
 * Internal database for pickers, such as mapping IDs to directory/file paths.
 * @ignore
 */
export const db: Database;
declare namespace _default {
    export { showDirectoryPicker };
    export { showOpenFilePicker };
    export { showSaveFilePicker };
}
export default _default;
export type FilePickerAcceptType = {
    description?: string;
    accept?: Record<string, string[]>;
};
export type ShowDirectoryPickerOptions = {
    id?: string;
    mode?: "read" | "readwrite";
    startIn?: FileSystemHandle | "desktop" | "documents" | "downloads" | "music" | "pictures" | "videos";
};
export type ShowOpenFilePickerOptions = {
    id?: string;
    excludeAcceptAllOption?: boolean;
    startIn?: FileSystemHandle | "desktop" | "documents" | "downloads" | "music" | "pictures" | "videos";
    types?: Array<FilePickerAcceptType>;
};
export type ShowSaveFilePickerOptions = {
    id?: string;
    excludeAcceptAllOption?: boolean;
    suggestedName?: string;
    startIn?: FileSystemHandle | "desktop" | "documents" | "downloads" | "music" | "pictures" | "videos";
    types?: Array<FilePickerAcceptType>;
};
