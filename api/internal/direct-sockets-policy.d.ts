/**
 * Direct Sockets Permissions-Policy gating.
 *
 * This runtime does not consume HTTP Permissions-Policy headers directly,
 * but apps may configure an opt-in/opt-out toggle using either:
 * - __args.config.permissions_policy_direct_sockets (boolean)
 * - __args.env.DIRECT_SOCKETS_ALLOWED ("1" | "true" | "yes" | "0" | "false" | "no")
 *
 * Default behavior: allowed.
 */
export function isDirectSocketsAllowed(): boolean;
declare namespace _default {
    export { isDirectSocketsAllowed };
}
export default _default;
