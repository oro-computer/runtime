/**
 * This flag can be used with uv_fs_copyfile() to return an error if the
 * destination already exists.
 * @type {number}
 */
export const COPYFILE_EXCL: number;
/**
 * This flag can be used with uv_fs_copyfile() to attempt to create a reflink.
 * If copy-on-write is not supported, a fallback copy mechanism is used.
 * @type {number}
 */
export const COPYFILE_FICLONE: number;
/**
 * This flag can be used with uv_fs_copyfile() to attempt to create a reflink.
 * If copy-on-write is not supported, an error is returned.
 * @type {number}
 */
export const COPYFILE_FICLONE_FORCE: number;
/**
 * A constant representing a directory entry whose type is unknown.
 * It indicates that the type of the file or directory cannot be determined.
 * @type {number}
 */
export const UV_DIRENT_UNKNOWN: number;
/**
 * A constant representing a directory entry of type file.
 * It indicates that the entry is a regular file.
 * @type {number}
 */
export const UV_DIRENT_FILE: number;
/**
 * A constant epresenting a directory entry of type directory.
 * It indicates that the entry is a directory.
 * @type {number}
 */
export const UV_DIRENT_DIR: number;
/**
 * A constant representing a directory entry of type symbolic link.
 * @type {number}
 */
export const UV_DIRENT_LINK: number;
/**
 * A constant representing a directory entry of type FIFO (named pipe).
 * @type {number}
 */
export const UV_DIRENT_FIFO: number;
/**
 * A constant representing a directory entry of type socket.
 * @type {number}
 */
export const UV_DIRENT_SOCKET: number;
/**
 * A constant representing a directory entry of type character device
 * @type {number}
 */
export const UV_DIRENT_CHAR: number;
/**
 * A constant representing a directory entry of type block device.
 * @type {number}
 */
export const UV_DIRENT_BLOCK: number;
/**
 * A constant representing a symlink should target a directory.
 * @type {number}
 */
export const UV_FS_SYMLINK_DIR: number;
/**
 * A constant representing a symlink should be created as a Windows junction.
 * @type {number}
 */
export const UV_FS_SYMLINK_JUNCTION: number;
/**
 * A constant representing an opened file for memory mapping on Windows systems.
 * @type {number}
 */
export const UV_FS_O_FILEMAP: number;
/**
 * Opens a file for read-only access.
 * @type {number}
 */
export const O_RDONLY: number;
/**
 * Opens a file for write-only access.
 * @type {number}
 */
export const O_WRONLY: number;
/**
 * Opens a file for both reading and writing.
 * @type {number}
 */
export const O_RDWR: number;
/**
 * Appends data to the file instead of overwriting.
 * @type {number}
 */
export const O_APPEND: number;
/**
 * Enables asynchronous I/O notifications.
 * @type {number}
 */
export const O_ASYNC: number;
/**
 * Ensures file descriptors are closed on `exec()` calls.
 * @type {number}
 */
export const O_CLOEXEC: number;
/**
 * Creates a new file if it does not exist.
 * @type {number}
 */
export const O_CREAT: number;
/**
 * Minimizes caching effects for file I/O.
 * @type {number}
 */
export const O_DIRECT: number;
/**
 * Ensures the opened file is a directory.
 * @type {number}
 */
export const O_DIRECTORY: number;
/**
 * Writes file data synchronously.
 * @type {number}
 */
export const O_DSYNC: number;
/**
 * Fails the operation if the file already exists.
 * @type {number}
 */
export const O_EXCL: number;
/**
 * Enables handling of large files.
 * @type {number}
 */
export const O_LARGEFILE: number;
/**
 * Prevents updating the file's last access time.
 * @type {number}
 */
export const O_NOATIME: number;
/**
 * Prevents becoming the controlling terminal for the process.
 * @type {number}
 */
export const O_NOCTTY: number;
/**
 * Does not follow symbolic links.
 * @type {number}
 */
export const O_NOFOLLOW: number;
/**
 * Opens the file in non-blocking mode.
 * @type {number}
 */
export const O_NONBLOCK: number;
/**
 * Alias for `O_NONBLOCK` on some systems.
 * @type {number}
 */
export const O_NDELAY: number;
/**
 * Obtains a file descriptor for a file but does not open it.
 * @type {number}
 */
export const O_PATH: number;
/**
 * Writes both file data and metadata synchronously.
 * @type {number}
 */
export const O_SYNC: number;
/**
 * Creates a temporary file that is not linked to a directory.
 * @type {number}
 */
export const O_TMPFILE: number;
/**
 * Truncates the file to zero length if it exists.
 * @type {number}
 */
export const O_TRUNC: number;
/**
 * Bitmask for extracting the file type from a mode.
 * @type {number}
 */
export const S_IFMT: number;
/**
 * Indicates a regular file.
 * @type {number}
 */
export const S_IFREG: number;
/**
 * Indicates a directory.
 * @type {number}
 */
export const S_IFDIR: number;
/**
 * Indicates a character device.
 * @type {number}
 */
export const S_IFCHR: number;
/**
 * Indicates a block device.
 * @type {number}
 */
export const S_IFBLK: number;
/**
 * Indicates a FIFO (named pipe).
 * @type {number}
 */
export const S_IFIFO: number;
/**
 * Indicates a symbolic link.
 * @type {number}
 */
export const S_IFLNK: number;
/**
 * Indicates a socket.
 * @type {number}
 */
export const S_IFSOCK: number;
/**
 * Grants read, write, and execute permissions for the file owner.
 * @type {number}
 */
export const S_IRWXU: number;
/**
 * Grants read permission for the file owner.
 * @type {number}
 */
export const S_IRUSR: number;
/**
 * Grants write permission for the file owner.
 * @type {number}
 */
export const S_IWUSR: number;
/**
 * Grants execute permission for the file owner.
 * @type {number}
 */
export const S_IXUSR: number;
/**
 * Grants read, write, and execute permissions for the group.
 * @type {number}
 */
export const S_IRWXG: number;
/**
 * Grants read permission for the group.
 * @type {number}
 */
export const S_IRGRP: number;
/**
 * Grants write permission for the group.
 * @type {number}
 */
export const S_IWGRP: number;
/**
 * Grants execute permission for the group.
 * @type {number}
 */
export const S_IXGRP: number;
/**
 * Grants read, write, and execute permissions for others.
 * @type {number}
 */
export const S_IRWXO: number;
/**
 * Grants read permission for others.
 * @type {number}
 */
export const S_IROTH: number;
/**
 * Grants write permission for others.
 * @type {number}
 */
export const S_IWOTH: number;
/**
 * Grants execute permission for others.
 * @type {number}
 */
export const S_IXOTH: number;
/**
 * Checks for the existence of a file.
 * @type {number}
 */
export const F_OK: number;
/**
 * Checks for read permission on a file.
 * @type {number}
 */
export const R_OK: number;
/**
 * Checks for write permission on a file.
 * @type {number}
 */
export const W_OK: number;
/**
 * Checks for execute permission on a file.
 * @type {number}
 */
export const X_OK: number;
export default exports;
import * as exports from './constants.js';
