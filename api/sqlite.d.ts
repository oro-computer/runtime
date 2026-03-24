/**
 * Returns true when the runtime is able to open a SQLite database with the
 * cr-sqlite extension loaded and ready for use.
 *
 * This reflects the *current* process configuration. On platforms where the
 * runtime auto-loads cr-sqlite during database open (desktop, Android, and
 * configured iOS builds), any failure to load the extension will cause this
 * predicate to return false. On platforms where cr-sqlite is not auto-loaded
 * or cannot be loaded for the current process, this predicate returns false.
 *
 * The result is cached for the lifetime of the process.
 *
 * @returns {boolean}
 */
export function hasCRSQLite(): boolean;
export function open(path: any, options: any): Database;
export const OPEN_READONLY: number;
export const OPEN_READWRITE: number;
export const OPEN_CREATE: number;
export const OPEN_URI: number;
export const OPEN_MEMORY: number;
export const OPEN_NOMUTEX: number;
export const OPEN_FULLMUTEX: number;
export const OPEN_SHAREDCACHE: number;
export const OPEN_PRIVATECACHE: number;
export const OPEN_DEFAULT: number;
export class Statement {
    [x: number]: () => {
        args: any[];
        handle(held: any): void;
    };
    constructor(database: any, descriptor?: {});
    get id(): string;
    get closed(): boolean;
    get columns(): any[];
    get columnsMeta(): any[];
    bind(params?: any[]): this;
    step(options?: {}): {
        done: boolean;
        rows: any[];
        columns: string[];
        columnsMeta: {
            name: any;
            declType: any;
            type: any;
        }[];
        changes: number;
        lastInsertRowid: bigint;
        mode: string;
    };
    reset(): this;
    run(params?: any[], options?: {}): {
        rows: any[];
        columns: any[];
        columnsMeta: any[];
        changes: number;
        lastInsertRowid: bigint;
        mode: string;
    };
    all(params?: any[], options?: {}): any[];
    iterate(params?: any[], options?: {}): AsyncGenerator<any, void, unknown>;
    finalize(): void;
    _finalize(options?: {}): void;
    #private;
}
export class Database {
    [x: number]: () => {
        args: {
            id: any;
            closed: boolean;
        }[];
        handle(held: any): void;
    };
    constructor(path: any, options?: {});
    get id(): any;
    get path(): string;
    get flags(): number;
    get closed(): boolean;
    close(): void;
    exec(sql: any, options?: {}): {
        rows: any[];
        columns: string[];
        columnsMeta: {
            name: any;
            declType: any;
            type: any;
        }[];
        changes: number;
        lastInsertRowid: bigint;
        mode: string;
    };
    execAsync(sql: any, options?: {}): Promise<{
        rows: any[];
        columns: string[];
        columnsMeta: {
            name: any;
            declType: any;
            type: any;
        }[];
        changes: number;
        lastInsertRowid: bigint;
        mode: string;
    }>;
    query(sql: any, options?: {}): any[];
    queryAsync(sql: any, options?: {}): Promise<any[]>;
    prepare(sql: any): Statement;
    _trackStatement(statement: any): void;
    _releaseStatement(statement: any): void;
    _releaseStatementById(id: any): void;
    #private;
}
declare const _default: Readonly<{
    Database: typeof Database;
    Statement: typeof Statement;
    open: typeof open;
    hasCRSQLite: typeof hasCRSQLite;
    OPEN_DEFAULT: number;
    OPEN_READONLY: number;
    OPEN_READWRITE: number;
    OPEN_CREATE: number;
    OPEN_URI: number;
    OPEN_MEMORY: number;
    OPEN_NOMUTEX: number;
    OPEN_FULLMUTEX: number;
    OPEN_SHAREDCACHE: number;
    OPEN_PRIVATECACHE: number;
}>;
export default _default;
