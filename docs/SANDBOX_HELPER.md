# Sandbox Helper Implementation Plan

## Goals & Scope

- Allow Oro Runtime apps to access USB and other privileged resources without running as root.
- Provide a reusable, least-privilege service that works across all desktop targets (Linux, macOS, Windows).
- Mirror Chromium’s security posture: central privileged broker + sandboxed child processes with per-user permissions.
- Support multiple apps concurrently, with policy controls that can restrict per-app capabilities when needed.
- Ship with tooling that keeps developer ergonomics: once installed, `oroc build -r` (and similar flows) just work.

## High-Level Architecture

1. **Privileged Helper Binary (`oro-helper`)**
   - Installed system-wide, owned by root (or platform equivalent).
   - Responsible for opening privileged resources, managing policy, and spawning sandboxed child processes.
2. **Zygote Process**
   - A long-lived child of the helper that preloads runtime code and listens for spawn requests from user-facing tools.
   - Forks lightweight sandboxed processes for each launched app.
3. **IPC Layer**
   - UNIX domain sockets (Linux/macOS) and named pipes (Windows) for communication between CLI/runtime and helper.
   - Uses authenticated, versioned messages (CBOR/JSON/Protocol Buffers) plus descriptor passing where available.
4. **Policy & Capability Engine**
   - Defines which device classes, VID/PIDs, network ports, or filesystem paths are permitted.
   - Future-ready for per-app manifests or signed capability tokens.
5. **Logging & Observability**
   - Helper writes to system log (journald, Unified Logging, Windows Event Log).
   - Optional structured audit trail for privileged operations.

## Client Entry Points & Authentication

- Shared bootstrap shim is linked into the runtime so `oroc build -r`, `oroc run`, and directly-invoked packaged apps all follow the same handshake before touching privileged APIs.
- Helper publishes one IPC endpoint per user/session. Filesystem ACLs gate access, and the helper verifies peer identity (`SO_PEERCRED` on Linux, `audit_token_t` on macOS, `ImpersonateNamedPipeClient` or `GetNamedPipeClientProcessId` on Windows) before servicing requests.
- Clients send an app identifier (bundle ID or executable hash) during handshake so per-app policy can be enforced independent of launch path.
- Developer mode (opt-in via local policy) relaxes signing checks for unsigned builds while still enforcing UID/SID verification.

## Shared Implementation Phases

1. **Research & Prototype (Milestone A)**
   - Verify descriptor passing for libusb on each platform.
   - Spike a minimal helper that opens a USB node and echoes data from an unprivileged client.
2. **Helper Core (Milestone B)**
   - Implement helper binary with argument parsing, privilege drop, policy hooks, logging skeleton.
   - Define IPC protocol (`OpenDevice`, `SpawnProcess`, `ReleaseHandle`, `Quit`).
3. **Zygote & Sandbox (Milestone C)**
   - Embed runtime bootstrap: helper forks zygote, zygote preloads runtime libs, forks per-app processes.
   - Apply sandboxing (namespaces/seccomp, posix_spawnattr, Windows Job Objects).
4. **Platform Services Integration (Milestone D)**
   - Linux: setuid helper packaging, udev rule install, `oro-helper --ensure` bootstrap integration.
   - macOS: launchd plist, SMJobBless installer, entitlement management.
   - Windows: service registration, installer scripts (MSI/Inno), service control shim.
5. **CLI & Runtime Adaptation (Milestone E)**
   - Extend `oroc` CLI to detect helper status, auto-start services, and request capabilities.
   - Update runtime IPC layer to request USB descriptors via helper before initializing libusb.
6. **Security Review & Hardening (Milestone F)**
   - Threat modeling, code audit, fuzzing IPC parser, privilege-dropping verification.
   - Document escalation paths and incident response.
7. **Developer Experience & Testing (Milestone G)**
   - Automated integration tests for helper lifecycle.
   - End-to-end tests for USB enumeration via helper on each platform.
   - Docs, troubleshooting guides, and logging instructions.

Subsequent phases can iteratively add other privileged capabilities (low-number ports, system config writes, etc.) once USB support is stable.

## Descriptor Passing Fallback Strategy

- Prefer native descriptor/handle passing (`libusb_wrap_sys_device`, macOS IOKit connections, Windows `DuplicateHandle`).
- If descriptor passing fails, transparently downgrade to a brokered transfer API where the helper executes `ControlTransfer`, `BulkTransfer`, `InterruptTransfer`, and hotplug notifications on behalf of the client, using correlation IDs to pair responses.
- Handshake records which mode is active; clients emit telemetry so we can track fallback usage.
- Integration tests should exercise both code paths to prevent regressions.

## Linux Plan

### Permissions Model

- Ship a udev rule (e.g., `/lib/udev/rules.d/80-oro-usb.rules`) tagging supported device classes:
  ```
  SUBSYSTEM=="usb", MODE="0660", TAG+="uaccess", TAG+="seat", GROUP="orousb"
  ```
- `TAG+="uaccess"` allows systemd-logind to grant per-session ACLs automatically.
- Optional group (`orousb`) provides compatibility for non-systemd environments; helper’s installer creates the group and adds users on request.

### Helper Deployment

- Install helper binary at `/usr/lib/oro/oro-helper` with mode `4755` (setuid root).
- Installation script:
  - Copies helper, bootstrap shim, and supporting configs.
  - Creates runtime directories (`/run/oro-helper`), noting they live on tmpfs and may be recreated at boot.
  - Installs udev rule, reloads via `udevadm control --reload`.
  - Optionally adds user to `orousb` group for non-logind hosts.
- No systemd unit is required; the helper is invoked on demand by CLI or app bootstrap. A lightweight `oro-helper --ensure` command can daemonize a broker if we want it resident.

### Helper Runtime

On invocation, helper:

1. Verifies it is running as setuid root and performs self-integrity checks (hash/signature).
2. Ensures `/run/oro-helper` exists (recreating it on tmpfs as needed) and that a per-user broker process is running; if not, forks a small root-owned parent (`oro-helperd`) that stays resident and opens privileged resources (udev monitor, libusb context).
3. Parent drops all ambient capabilities except the minimal set required for device I/O (target: `CAP_DAC_READ_SEARCH` + `CAP_DAC_OVERRIDE`, pending validation during Milestone A/B) and sets `PR_SET_NO_NEW_PRIVS`.
4. Parent spawns/refreshes a per-user zygote process (running as the calling user) and creates `/run/oro-helper/<uid>.sock` (mode `0600`, owned by user).
5. Subsequent invocations short-circuit to a control message (`oro-helper --ping`) that confirms broker health.

Descriptor passing:

- Root broker opens `/dev/bus/usb/<bus>/<dev>` with `O_RDWR`.
- Uses `sendmsg` + `SCM_RIGHTS` to hand the fd to the requesting child process through the per-user socket.
- Child wraps the fd in libusb via `libusb_wrap_sys_device`.

### Sandbox & Process Lifecycle

- Use user and mount namespaces, PID namespace when possible.
- Apply seccomp filter allowing required syscalls (dup, read, write, ioctl, etc.).
- Child processes run under original user UID/GID, not helper UID.
- Broker tracks all outstanding descriptors; on disconnect, closes fd and notifies clients.

### CLI Integration

- `oroc build -r`:
  1. Executes `oro-helper --ensure` (via bootstrap shim) to spawn/refresh the broker and zygote for the current user.
  2. Connects to `/run/oro-helper/<uid>.sock`; if handshake fails, prints actionable remediation.
  3. Uses IPC `SpawnProcess` to launch or attach to the app’s runtime process after the bootstrap shim connects.
- `oroc helper status`:
  - Invokes `oro-helper --status` to query broker version, uptime, loaded policy.
- Standalone apps ship with the same shim and automatically call `oro-helper --ensure` on startup.

### Testing & Diagnostics

- Unit tests for IPC message parsing and policy evaluation (run as part of `npm run test:runtime-core`).
- Integration tests may use `systemd-run --user` (when available) or `setsid`/custom launchers to simulate clean sessions.
- Troubleshooting doc: helper logs to syslog/journald with the `oro-helperd` ident, so `journalctl -t oro-helperd` (or `/var/log/oro-helperd.log` if rsyslog writes there); additionally `getfacl /dev/bus/usb/*`, `oroc helper doctor`.

## macOS Plan

### Helper Deployment

- Use `SMJobBless` to install a privileged helper at `/Library/PrivilegedHelperTools/com.oro.helper`.
- Launchd plist (`/Library/LaunchDaemons/com.oro.helper.plist`) starts the helper on demand.
- CLI `oroc helper install` triggers `SMJobBless`, requiring an admin consent prompt (standard macOS UX).

### Helper Runtime

- Helper listens on launchd-managed UNIX socket (`/var/run/oro-helper/<uid>.sock`).
- After accepting a connection:
  - Reads the client `audit_token_t` to confirm the caller UID matches the launch session.
  - Validates code signature (`SecCodeCopyGuestWithAttributes`) against the Oro Runtime maintainer certificates; developer mode (toggled via local policy) allows unsigned builds while still checking the caller UID.
  - Uses `AuthorizationExternalForm` to ensure the caller is the logged-in user.
- Zygote uses `posix_spawn` with `posix_spawnattr_setflags` to enter sandbox profiles (Seatbelt). Profiles live under `/Library/Application Support/Oro/runtime-sandbox.sb`.

### Testing & Diagnostics

- Integration tests using `launchctl kickstart -k system/com.oro.helper`.
- Debug command: `log show --predicate 'subsystem == \"com.oro.helper\"' --last 1h`.

## Windows Plan

### Permissions Model

- Install a Windows Service (`OroHelperService`) running as `LocalSystem`.
- Service keeps device DACLs locked down and relies on brokered handles so only approved processes can touch devices.
- Maintain per-session named pipe endpoints (`\\\\.\\pipe\\oro-helper-<SessionId>`).

### Helper Deployment

- Provide MSI/Inno installer performing:
  - Copy of helper binaries to `%ProgramFiles%\\Oro\\helper`.
  - Service registration (`sc create OroHelperService binPath= ... start= auto`).
  - Installation of WinUSB drivers if required (`pnputil /add-driver`).
  - Firewall rule adjustments if helper exposes network diagnostics.
- CLI `oroc helper install` uses `Start-Process -Verb RunAs` to trigger installer or leverages `elevate.exe`.

### Logging & Diagnostics

- Service logs to Windows Event Log under `Application` source `OroHelper`.
- Provide `oroc helper status` that queries service state with `QueryServiceStatusEx`.
- Troubleshooting: `Get-WinEvent -LogName Application | where ProviderName -eq 'OroHelper'`.

## Policy & Configuration

- Policy file (`/etc/oro/policy.json`, `%ProgramData%\\Oro\\policy.json`, `/Library/Application Support/Oro/policy.json`).
- Schema:
  ```json
  {
    "version": 1,
    "capabilities": {
      "usb": {
        "allow": [{ "vid": "0x1234", "pid": "*" }, { "class": "cdc" }]
      },
      "ports": { "allow": [22, 443] },
      "fs": { "allow": ["/var/lib/oro/runtime"] }
    }
  }
  ```
- Helper loads policy at startup, reloads on SIGHUP / platform equivalent.
- CLI commands to manage policy (`oroc helper policy add-usb --vid 0x1234 --pid 0x0001`).

## Developer Workflow

- Prerequisite: run `sudo oroc helper install` on Linux, `oroc helper install` with admin prompt (macOS/Windows).
- During development:
  - `oroc build -r` connects to helper, spawns sandboxed process, and requests USB descriptors.
  - Logs accessible via `oroc helper logs --follow` (wraps platform log readers).
  - For hot reload, zygote keeps runtime libs warm; rebuilds only restart child process.
- Packaged apps launched directly (outside the CLI) rely on the same bootstrap shim: on startup the binary finds the user IPC endpoint, performs the handshake, and either requests a fresh sandboxed process or binds to an existing one as dictated by policy.

## Open Questions & Follow-Ups

- Confirm libusb descriptor wrapping works on macOS/Windows helper → child handoff, or plan for proxying calls.
- Determine per-app trust model (signed manifests vs. user prompts).
- Evaluate need for GUI prompts when new devices are accessed.
- Assess integration with mobile runtimes (Android/iOS) for parity.

This plan should be revised after Milestone A prototypes validate descriptor passing and sandbox mechanics on each platform.
