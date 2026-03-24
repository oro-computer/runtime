# Latica Protocol: Migration Notes

This document summarizes recent protocol changes in the shipped Latica surface and provides guidance for mixed deployments and upgrades.

## Summary Hashing (Sync)

- Change: Cache summary hashing upgraded from SHA‑1 (20 bytes) to SHA‑256 (32 bytes).
- Compatibility: The decoder now accepts both 20‑byte (SHA‑1) and 32‑byte (SHA‑256) summary frames. New peers emit SHA‑256 by default.
- Impact: Mixed deployments continue to sync; however, migrating all peers to SHA‑256 is recommended for collision resistance.

## Control‑Plane Authentication (optional)

- New: Optional signing/verification for control‑plane frames (Ping, Pong, Intro, Join, Query).
- Enable by passing `controlPlaneAuth: 'sig'` and `signingKeys` to `network(options)` or the underlying Peer.
- Behavior:
  - When enabled, unsigned or invalid control frames are dropped early.
  - Publish/Stream (data plane) are unaffected by this flag.
- Suggested rollout: Enable on a canary set first. Once all peers are upgraded to a build that understands signatures, flip the flag cluster‑wide.

## Data‑Plane Verification Defaults

- Change: Event delivery now requires verification by default.
- To accept unverified payloads (not recommended), pass `allowUnsigned: true` in `network(options)`.

## Introduction Scoping

- Change: Peers only introduce members within the same `clusterId`. If a peer receives a JOIN for a cluster it does not belong to, it relays the message but does not introduce.
- Impact: Reduces cross‑cluster traffic and blast radius. Ensure peers join the intended cluster before expecting introductions.

## Upgrade Steps

1. Upgrade peers to a build that includes SHA‑256 summaries and tolerant decode.
2. Verify sync still operates in mixed environments (SHA‑1/SHA‑256).
3. Optionally enable `controlPlaneAuth: 'sig'` with valid `signingKeys`.
4. Confirm application logic does not rely on unsigned data events, or set `allowUnsigned: true` explicitly.
5. Ensure peers are members of the target cluster(s) for introductions.
