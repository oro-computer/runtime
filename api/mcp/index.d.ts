/**
 * Register a tool that can be invoked by MCP clients.
 * @param {MCPRegisterToolOptions} tool
 * @returns {Promise<number|null>}
 */
export function registerTool(tool: MCPRegisterToolOptions): Promise<number | null>;
/**
 * Unregister a tool by name.
 * @param {string} name
 * @returns {Promise<boolean>} Whether a registered tool was removed.
 */
export function unregisterTool(name: string): Promise<boolean>;
/**
 * List the currently registered tool descriptors.
 * @returns {Promise<MCPToolDescriptor[]>}
 */
export function listTools(): Promise<MCPToolDescriptor[]>;
/**
 * Register a resource that can be read or subscribed to by MCP clients.
 * @param {MCPRegisterResourceOptions} resource
 * @returns {Promise<number|null>}
 */
export function registerResource(resource: MCPRegisterResourceOptions): Promise<number | null>;
/**
 * Unregister a resource by URI.
 * @param {string} uri
 * @returns {Promise<boolean>} Whether a registered resource was removed.
 */
export function unregisterResource(uri: string): Promise<boolean>;
/**
 * List the currently registered resource descriptors.
 * @returns {Promise<MCPResourceDescriptor[]>}
 */
export function listResources(): Promise<MCPResourceDescriptor[]>;
/**
 * Invoke a registered tool through the local MCP service.
 * @param {string} name
 * @param {Record<string, any>} [args]
 * @param {MCPInvocationOptions} [options]
 * @returns {Promise<boolean>} Whether the invocation was accepted.
 */
export function invokeTool(name: string, args?: Record<string, any>, options?: MCPInvocationOptions): Promise<boolean>;
/**
 * Publish an update to active subscriptions for a registered resource.
 * @param {string} uri
 * @param {MCPResourceHandlerResult} result
 * @param {MCPPublishResourceOptions} [options]
 * @returns {Promise<boolean>} Whether at least one matching stream received the update.
 */
export function publishResource(uri: string, result: MCPResourceHandlerResult, options?: MCPPublishResourceOptions): Promise<boolean>;
/**
 * Configure a runtime authorization handler for incoming MCP HTTP requests.
 * Pass a function to enable dynamic authorization or `null`/`undefined` to clear.
 * The handler can return a boolean or an {@link MCPAuthorizationDecision} object.
 *
 * @param {(request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision> | null | undefined} handler
 * @returns {Promise<void>}
 */
export function setAuthorizationHandler(handler: (request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision> | null | undefined): Promise<void>;
/**
 * Start the embedded MCP HTTP/SSE bridge.
 *
 * - Supplying `port: 0` binds to an ephemeral port and the resolved value is
 *   returned in the result.
 * - `authorize` registers a dynamic authorization handler for this server.
 *
 * @param {MCPStartServerOptions} [options]
 * @returns {Promise<MCPStartServerResult>}
 */
export function startServer(options?: MCPStartServerOptions): Promise<MCPStartServerResult>;
/**
 * Stop the embedded MCP server.
 * @returns {Promise<boolean>} Whether the server is stopped.
 */
export function stopServer(): Promise<boolean>;
/**
 * Report whether the embedded MCP server is running.
 * @returns {Promise<boolean>}
 */
export function serverStatus(): Promise<boolean>;
declare namespace _default {
    export { registerTool };
    export { unregisterTool };
    export { listTools };
    export { registerResource };
    export { unregisterResource };
    export { listResources };
    export { invokeTool };
    export { publishResource };
    export { setAuthorizationHandler };
    export { startServer };
    export { stopServer };
    export { serverStatus };
}
export default _default;
export type MCPToolInvocationContext = {
    /**
     * Unique invocation identifier provided by the runtime.
     */
    id: string;
    /**
     * Registered tool name.
     */
    name: string;
    /**
     * Identifier for the originating MCP session.
     */
    sessionId: string;
    /**
     * Parsed invocation arguments.
     */
    arguments: Record<string, any>;
};
export type MCPIcon = {
    /**
     * URI or data URI for the icon.
     */
    src: string;
    /**
     * MIME type of the icon.
     */
    mimeType?: string;
    /**
     * Available sizes, such as `48x48` or `any`.
     */
    sizes?: string[];
    /**
     * Optional color-scheme hint.
     */
    theme?: "light" | "dark";
};
export type MCPToolAnnotations = {
    title?: string;
    readOnlyHint?: boolean;
    destructiveHint?: boolean;
    idempotentHint?: boolean;
    openWorldHint?: boolean;
};
export type MCPAnnotations = {
    audience?: ("user" | "assistant")[];
    /**
     * Importance from 0 through 1.
     */
    priority?: number;
    /**
     * ISO 8601 last-modified timestamp.
     */
    lastModified?: string;
};
export type MCPTextContent = {
    type: "text";
    text: string;
    annotations?: MCPAnnotations;
    _meta?: Record<string, any>;
};
export type MCPBinaryContent = {
    type: "image" | "audio";
    /**
     * Base64-encoded content.
     */
    data: string;
    mimeType: string;
    annotations?: MCPAnnotations;
    _meta?: Record<string, any>;
};
export type MCPResourceLinkContent = {
    type: "resource_link";
    name: string;
    uri: string;
    title?: string;
    description?: string;
    mimeType?: string;
    size?: number;
    icons?: MCPIcon[];
    annotations?: MCPAnnotations;
    _meta?: Record<string, any>;
};
export type MCPResourceContents = {
    uri: string;
    text?: string;
    /**
     * Base64-encoded bytes.
     */
    blob?: string;
    mimeType?: string;
    _meta?: Record<string, any>;
};
export type MCPResourceContentInput = {
    /**
     * Defaults to the registered resource URI.
     */
    uri?: string;
    text?: string;
    /**
     * Base64 string or bytes.
     */
    blob?: string | Uint8Array;
    mimeType?: string;
    _meta?: Record<string, any>;
};
export type MCPEmbeddedResourceContent = {
    type: "resource";
    resource: MCPResourceContents;
    annotations?: MCPAnnotations;
    _meta?: Record<string, any>;
};
export type MCPContentBlock = MCPTextContent | MCPBinaryContent | MCPResourceLinkContent | MCPEmbeddedResourceContent;
export type MCPJSONValue = string | number | boolean | null | MCPJSONValue[] | {
    [key: string]: MCPJSONValue;
};
export type MCPToolResult = {
    content: MCPContentBlock[];
    structuredContent?: MCPJSONValue;
    isError?: boolean;
    _meta?: Record<string, any>;
};
/**
 * A handler may return a complete MCP result or any JSON value. Direct JSON
 * values become `structuredContent` and receive a serialized text content block.
 */
export type MCPToolHandlerResult = MCPToolResult | MCPJSONValue | undefined;
export type MCPResourceDescriptor = {
    /**
     * Unique resource URI.
     */
    uri: string;
    name?: string;
    title?: string;
    description?: string;
    mimeType?: string;
    icons?: MCPIcon[];
    annotations?: MCPAnnotations;
    /**
     * Size of the raw resource content in bytes.
     */
    size?: number;
    /**
     * Protocol and application metadata.
     */
    _meta?: Record<string, any>;
    subscribable?: boolean;
    /**
     * Deprecated alias for `_meta`.
     */
    metadata?: any;
};
export type MCPResourceContext = {
    /**
     * Server-provided identifier for the request/subscription.
     */
    id: string;
    /**
     * Resource URI.
     */
    uri: string;
    /**
     * Identifier for the originating MCP session.
     */
    sessionId: string;
    /**
     * Additional parameters supplied by the client.
     */
    params: Record<string, any>;
    /**
     * Last-known descriptor for the resource.
     */
    descriptor: MCPResourceDescriptor;
};
export type MCPAuthorizationRequest = {
    /**
     * Unique identifier for this authorization decision.
     */
    id: string;
    /**
     * HTTP method used by the client.
     */
    method: string;
    /**
     * Resolved path (including endpoint) for the request.
     */
    path: string;
    /**
     * Remote IP address observed by the runtime.
     */
    remoteAddress: string;
    /**
     * Remote port or `null` when unavailable.
     */
    remotePort: number | null;
    /**
     * Request headers keyed by name.
     */
    headers: Record<string, string | string[]>;
    /**
     * Query parameters keyed by name.
     */
    query: Record<string, string | string[]>;
    /**
     * Full `Authorization` header when present.
     */
    authorization?: string;
    /**
     * Raw request body when supplied.
     */
    body?: string;
};
export type MCPAuthorizationDecision = {
    /**
     * Whether to accept the request.
     */
    allow: boolean;
    /**
     * Optional HTTP status to return when `allow` is false.
     */
    status?: number;
    /**
     * Optional response body when `allow` is false.
     */
    message?: string;
};
export type MCPOAuthScreenOptions = {
    /**
     * Inline HTML for the authorization screen. Approval forms must submit `decision` and the `{{AUTHORIZATION_REQUEST}}` placeholder value as `authorization_request`.
     */
    html?: string;
    /**
     * Absolute path to an HTML authorization screen with the same one-time request handling as `html`.
     */
    file?: string;
};
export type MCPOAuthOptions = {
    /**
     * Enable the built-in OAuth flow (defaults to `true` when omitted).
     */
    enabled?: boolean;
    /**
     * Explicit authorization-server issuer URL reported in discovery metadata. Required when exposing a non-loopback server through a proxy.
     */
    issuer?: string;
    /**
     * Canonical public URI of the MCP endpoint. Required when its public URI differs from the bound host and endpoint.
     */
    resource?: string;
    /**
     * Override for the authorization endpoint path.
     */
    authorizePath?: string;
    /**
     * Override for the token endpoint path.
     */
    tokenPath?: string;
    /**
     * Override for the OAuth discovery metadata path.
     */
    metadataPath?: string;
    /**
     * Authorization code lifetime override (seconds).
     */
    codeLifetimeSeconds?: number;
    /**
     * Access token lifetime override (seconds).
     */
    tokenLifetimeSeconds?: number;
    /**
     * Pre-registered client identifier. Required when OAuth is enabled.
     */
    defaultClientId?: string;
    /**
     * Optional scope displayed on the default screen.
     */
    defaultScope?: string;
    /**
     * Exact pre-registered redirect URIs accepted for the client. At least one is required when OAuth is enabled.
     */
    redirectUris?: string[];
    /**
     * Custom authorization screen configuration.
     */
    screen?: MCPOAuthScreenOptions;
};
export type MCPRegisterToolOptions = {
    name: string;
    title?: string;
    description?: string;
    inputSchema?: Record<string, any>;
    /**
     * A valid JSON Schema. Defaults to dialect 2020-12 when `$schema` is omitted.
     */
    outputSchema?: Record<string, any>;
    icons?: MCPIcon[];
    annotations?: MCPToolAnnotations;
    /**
     * Protocol and application metadata.
     */
    _meta?: Record<string, any>;
    /**
     * Deprecated alias for `_meta`.
     */
    metadata?: any;
    handler?: (context: MCPToolInvocationContext) => MCPToolHandlerResult | Promise<MCPToolHandlerResult>;
};
export type MCPToolDescriptor = {
    name: string;
    title?: string;
    description?: string;
    inputSchema: Record<string, any>;
    outputSchema?: Record<string, any>;
    icons?: MCPIcon[];
    annotations?: MCPToolAnnotations;
    _meta?: Record<string, any>;
    /**
     * Deprecated alias for `_meta`.
     */
    metadata?: Record<string, any>;
};
export type MCPRegisterResourceOptions = {
    uri: string;
    name?: string;
    title?: string;
    description?: string;
    mimeType?: string;
    icons?: MCPIcon[];
    annotations?: MCPAnnotations;
    /**
     * Size of the raw resource content in bytes.
     */
    size?: number;
    subscribable?: boolean;
    /**
     * Protocol and application metadata.
     */
    _meta?: Record<string, any>;
    /**
     * Deprecated alias for `_meta`.
     */
    metadata?: any;
    handler?: (context: MCPResourceContext) => MCPResourceHandlerResult | Promise<MCPResourceHandlerResult>;
    onSubscribe?: (context: MCPResourceContext) => void | Promise<void>;
    onUnsubscribe?: (context: MCPResourceContext) => void | Promise<void>;
};
export type MCPResourceHandlerResultObject = {
    contents: MCPResourceContentInput[];
    _meta?: Record<string, any>;
};
export type MCPResourceHandlerResult = MCPResourceHandlerResultObject | MCPResourceContentInput | MCPResourceContentInput[] | string | Uint8Array | null | undefined;
export type MCPInvocationOptions = {
    sessionId?: string;
};
export type MCPPublishResourceOptions = {
    sessionId?: string;
    subscriptionId?: string;
};
export type MCPStartServerOptions = {
    host?: string;
    /**
     * TCP port from 0 through 65535. Use 0 to select an available port.
     */
    port?: number;
    endpoint?: string;
    sse?: string;
    message?: string;
    token?: string;
    /**
     * Positive 32-bit SSE retry interval in milliseconds.
     */
    retry?: number;
    /**
     * Seconds to retain an inactive MCP 2025 session.
     */
    sessionTtlSeconds?: number;
    /**
     * Maximum HTTP request body size.
     */
    maxRequestBytes?: number;
    /**
     * Maximum concurrent HTTP session contexts.
     */
    maxSessions?: number;
    /**
     * Maximum queued events per SSE stream.
     */
    maxQueuedEvents?: number;
    /**
     * Maximum queued event bytes per SSE stream.
     */
    maxQueuedBytes?: number;
    /**
     * Allow a reconnect to replace an existing MCP 2025 SSE stream for the same session.
     */
    replaceSseStreamOnReconnect?: boolean;
    authorize?: (request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision>;
    oauth?: MCPOAuthOptions | boolean;
};
export type MCPOAuthServerEndpoints = {
    authorizePath?: string | null;
    tokenPath?: string | null;
    metadataPath?: string | null;
    protectedResourceMetadataPath?: string | null;
};
export type MCPStartServerResult = {
    running: boolean;
    host: string;
    port: number;
    endpoint: string;
    oauth?: MCPOAuthServerEndpoints;
};
