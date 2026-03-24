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
export function isDirectSocketsAllowed () {
  try {
    const cfg = globalThis.__args?.config || {}
    if (typeof cfg.permissions_policy_direct_sockets !== 'undefined') {
      return !!cfg.permissions_policy_direct_sockets
    }
  } catch {}

  try {
    const env = globalThis.__args?.env || {}
    if (typeof env.DIRECT_SOCKETS_ALLOWED !== 'undefined') {
      const v = String(env.DIRECT_SOCKETS_ALLOWED).trim().toLowerCase()
      return v === '1' || v === 'true' || v === 'yes'
    }
  } catch {}

  return true
}

export default { isDirectSocketsAllowed }
