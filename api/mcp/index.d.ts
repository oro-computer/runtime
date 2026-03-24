/**
 * Register a tool that can be invoked by MCP clients.
 * @param {MCPRegisterToolOptions} tool
 * @returns {Promise<number|null>}
 */
export function registerTool(tool: MCPRegisterToolOptions): Promise<number | null>;
export function unregisterTool(name: any): Promise<boolean>;
export function listTools(): Promise<any>;
/**
 * Register a resource that can be read or subscribed to by MCP clients.
 * @param {MCPRegisterResourceOptions} resource
 * @returns {Promise<number|null>}
 */
export function registerResource(resource: MCPRegisterResourceOptions): Promise<number | null>;
export function unregisterResource(uri: any): Promise<boolean>;
export function listResources(): Promise<any>;
export function invokeTool(name: any, args?: {}, options?: any): Promise<boolean>;
export function publishResource(uri: any, result: any, options?: any): Promise<boolean>;
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
 * @returns {Promise<{ running: boolean, host: string, port: number, endpoint: string, oauth?: { authorizePath?: string | null, tokenPath?: string | null, metadataPath?: string | null } }>}
 */
export function startServer(options?: MCPStartServerOptions): Promise<{
    running: boolean;
    host: string;
    port: number;
    endpoint: string;
    oauth?: {
        authorizePath?: string | null;
        tokenPath?: string | null;
        metadataPath?: string | null;
    };
}>;
export function stopServer(): Promise<boolean>;
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
export type MCPResourceDescriptor = {
    /**
     * Unique resource URI.
     */
    uri: string;
    name?: string;
    description?: string;
    mimeType?: string;
    subscribable?: boolean;
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
     * Inline HTML string for the authorization screen.
     */
    html?: string;
    /**
     * Absolute path to a HTML file used for the authorization screen.
     */
    file?: string;
};
export type MCPOAuthOptions = {
    /**
     * Enable the built-in OAuth flow (defaults to `true` when omitted).
     */
    enabled?: boolean;
    /**
     * Explicit issuer URL reported in discovery metadata.
     */
    issuer?: string;
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
     * Optional client identifier shown on the default screen.
     */
    defaultClientId?: string;
    /**
     * Optional scope displayed on the default screen.
     */
    defaultScope?: string;
    /**
     * Custom authorization screen configuration.
     */
    screen?: MCPOAuthScreenOptions;
};
export type MCPRegisterToolOptions = {
    name: string;
    description?: string;
    metadata?: any;
    inputSchema?: Record<string, any>;
    handler?: (context: MCPToolInvocationContext) => any | Promise<any>;
};
export type MCPRegisterResourceOptions = {
    uri: string;
    name?: string;
    description?: string;
    mimeType?: string;
    subscribable?: boolean;
    metadata?: any;
    handler?: (context: MCPResourceContext) => any | Promise<any>;
    onSubscribe?: (context: MCPResourceContext) => void | Promise<void>;
    onUnsubscribe?: (context: MCPResourceContext) => void | Promise<void>;
};
export type MCPStartServerOptions = {
    host?: string;
    port?: number;
    endpoint?: string;
    sse?: string;
    message?: string;
    token?: string;
    retry?: number;
    authorize?: (request: MCPAuthorizationRequest) => boolean | MCPAuthorizationDecision | Promise<boolean | MCPAuthorizationDecision>;
    oauth?: MCPOAuthOptions | boolean;
};
