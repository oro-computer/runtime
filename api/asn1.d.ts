/**
 * Parses an ASN.1 document provided as a string.
 * @param {string} source
 * @param {Asn1ParseOptions} [options]
 * @returns {Promise<{ modules: Asn1Module[], modulesCount: number, lexerDebug: boolean, maxDepth: number, sourceText?: string }>}
 */
export function parse(source: string, options?: Asn1ParseOptions): Promise<{
    modules: Asn1Module[];
    modulesCount: number;
    lexerDebug: boolean;
    maxDepth: number;
    sourceText?: string;
}>;
/**
 * Parses an ASN.1 document from a file path on disk.
 * @param {string} path
 * @param {Asn1ParseOptions} [options]
 * @returns {Promise<{ modules: Asn1Module[], modulesCount: number, lexerDebug: boolean, maxDepth: number, sourceText?: string }>}
 */
export function parseFile(path: string, options?: Asn1ParseOptions): Promise<{
    modules: Asn1Module[];
    modulesCount: number;
    lexerDebug: boolean;
    maxDepth: number;
    sourceText?: string;
}>;
declare const _default: Readonly<{
    parse: typeof parse;
    parseFile: typeof parseFile;
}>;
export default _default;
/**
 * Describes options when parsing ASN.1 inputs.
 */
export type Asn1ParseOptions = {
    /**
     * Emits lexer debug information inside the native parser.
     */
    lexerDebug?: boolean;
    /**
     * When true, the original input is echoed back in the response.
     */
    includeSourceText?: boolean;
    /**
     * Controls how deep nested type/value trees are traversed when building the JSON representation.
     */
    maxDepth?: number;
};
/**
 * Represents a parsed ASN.1 definition module.
 */
export type Asn1Module = {
    name?: string;
    oid?: string;
    flags?: Record<string, boolean>;
    imports?: Asn1Import[];
    exports?: Asn1Export[];
    members?: Asn1Expression[];
    sourceFile?: string;
};
export type Asn1Import = {
    module?: string;
    symbols?: Array<{
        identifier?: string;
        metaType: string;
        exprType: string;
    }>;
    kind?: string;
};
export type Asn1Export = {
    identifier?: string;
    metaType: string;
    exprType: string;
};
export type Asn1Expression = {
    metaType: string;
    exprType: string;
    identifier?: string;
    line?: number;
    unique?: boolean;
    reference?: string;
    value?: Asn1Value;
    constraints?: Asn1Constraint | Asn1Constraint[];
    members?: Asn1Expression[];
    markers?: string[];
    tag?: {
        description?: string;
        class?: string;
        mode?: string;
        value?: string;
    };
    truncated?: boolean;
};
export type Asn1Constraint = {
    type: string;
    presence: string;
    line?: number;
    value?: Asn1Value;
    range?: {
        start?: Asn1Value;
        stop?: Asn1Value;
    };
    elements?: Asn1Constraint[];
    truncated?: boolean;
};
export type Asn1Value = {
    type: string;
    repr?: string;
    integer?: string;
    real?: number;
    string?: string;
    reference?: string;
    bytes?: number[];
    sizeInBits?: number;
    valueSet?: Asn1Constraint | Asn1Constraint[];
};
