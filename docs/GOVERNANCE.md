# Oro Runtime Governance Overview

This document explains how Oro Runtime is governed: who maintains the project, how decisions are made, and how to escalate when changes have broad impact (such as the Socket → Oro rebrand).

It complements the contribution guidelines in `CONTRIBUTING.md` and the rebrand policy documents in `docs/ORO_REBRAND.md`, `docs/ORO_REBRAND_TIMELINE.md`, and `docs/REBRAND_POLICY.md`.

## Roles

- **Maintainers**
  - Steer project direction and own final decisions on design, architecture, and releases.
  - Triage issues, prioritize work, and merge pull requests.
  - Ensure the project follows documented security, release, and compatibility policies.

- **Reviewers**
  - Provide code review, documentation review, and test coverage feedback.
  - Can approve most PRs; escalate controversial or cross‑cutting changes to maintainers.
  - May be maintainers in related repos (CLI, website, tooling) acting as liaisons.

- **Contributors**
  - Anyone opening issues, PRs, or participating in discussions.
  - Follow the contribution guide, code style, and security guidelines.
  - Can propose changes to policies, APIs, and governance via issues and discussions.

- **Release captain (per release)**
  - Coordinates tagging, changelogs, and release announcements.
  - Ensures the release checklist is followed (`RELEASE_CHECKLIST.md`, `docs/ORO_REBRAND_TIMELINE.md`).
  - Acts as the tie‑breaker when release‑blocking decisions need a prompt resolution.

- **Technical steering (TSC‑style group)**
  - Small group of maintainers responsible for long‑term direction and cross‑repo policy.
  - Handles escalations and approves changes to governance or rebrand‑affecting policy docs.

## Decision‑making model

Oro Runtime uses a **lazy consensus** model:

- For most changes, **one maintainer approval + passing checks** is sufficient once reasonable review time has passed.
- If someone has significant concerns, they should:
  - Comment directly on the PR or issue with clear, actionable feedback.
  - Propose concrete alternatives if they are blocking a change.
- If consensus cannot be reached in a reasonable time:
  - The discussion is escalated to the relevant maintainer or the TSC group.
  - The escalated decision and rationale are recorded in the issue or PR for future reference.

### When to open an RFC or discussion first

Use a GitHub Discussion or dedicated design issue when:

- A change affects **documented public APIs** (JS APIs, CLI commands, config fields).
- Backward compatibility guarantees in `docs/ORO_REBRAND.md` or `RUNTIME_ARCHITECTURE.md` may be impacted.
- Governance, security, or release policy documents need substantial edits.

In these cases:

- Capture the problem, constraints, and proposed approach concisely.
- Link related Linear tickets (e.g., Socket → Oro rebrand work items).
- Give stakeholders time to respond before landing implementation PRs, unless the change is clearly low‑risk and time‑sensitive.

## Review expectations

New contributors should have clear expectations about how reviews work:

- For most changes, one maintainer or reviewer approval plus passing checks is sufficient, provided there has been a reasonable opportunity for others to comment.
- Documentation-only or low-risk changes are typically merged after a single maintainer approval.
- Runtime, API, or policy changes that affect compatibility guarantees (for example, items documented in `docs/ORO_REBRAND.md` or `RUNTIME_ARCHITECTURE.md`) may require explicit sign-off from the relevant maintainers or the TSC group.
- Maintainers aim to provide an initial response to new issues and PRs within a few business days. If you have not received feedback, it is appropriate to ping the thread or ask in the Matrix/Discord `#oro-runtime` channels.
- Contributors are encouraged to keep PRs focused; large cross-cutting work should be broken into smaller pieces or preceded by a design/RFC discussion so reviews remain tractable.

## Escalation paths

If you disagree with a proposed or merged change, or if a decision feels blocked:

1. **Start with the PR/issue thread**
   - Ask clarifying questions.
   - Provide concrete examples or data where possible.
2. **Escalate to maintainers**
   - Mention a maintainer directly in the PR/issue.
   - Summarize the points of agreement and disagreement so far.
3. **Escalate to the TSC group**
   - Use a GitHub Discussion (e.g., “Request for decision: …”) when the change affects multiple repos or long‑term policy.
   - The TSC aims to respond promptly, with a clear written decision and follow‑up tasks as needed.

Security‑sensitive reports should always follow the security process in `SECURITY.md` rather than public escalation.

## Roadmap visibility and release cadence

The Oro Runtime roadmap and releases are visible through a few sources:

- Near-term technical priorities and status live in `STATUS.md` in this repository.
- Release notes and concrete version history live in `CHANGELOG.md` and under `docs/release/` (for example, `docs/release/oro-runtime-0.6.0.md`).
- Larger project arcs are tracked in Linear projects and via GitHub Projects/Discussions referenced from those tickets.

Oro Runtime does not mandate a strict calendar-based release cadence. Instead:

- Runtime releases are cut when they satisfy the documented quality criteria captured in `RELEASE_CHECKLIST.md`.
- Patch releases are issued as needed for critical bugs and security fixes.

## How to propose changes

To propose a change to Oro Runtime (code, APIs, docs, or policy):

1. **Open an issue**
   - Describe the problem, motivation, and rough proposal.
   - Link any relevant Linear tickets if you are working off a scoped project.
2. **Decide whether an RFC/discussion is needed**
   - For routine fixes or small features, a PR referencing the issue is usually enough.
   - For cross‑repo work, open a Discussion and link it from the issue.
3. **Open a PR**
   - Follow the coding and documentation guidelines in `AGENTS.md` and `CODE_STYLE.md`.
   - For APIs, ensure usage, parameters, return values, and examples are documented in JSDoc and the relevant docs.
4. **Request review**
   - Tag maintainers or reviewers when ready.
   - For large cross‑repo changes, call out which docs or policies you believe are impacted.

## Onboarding and where to start

New contributors should:

- Read `CONTRIBUTING.md` for setup instructions and contribution workflow.
- Skim `AGENTS.md` and `CODE_STYLE.md` for repo‑specific conventions.
- Use `docs/GOVERNANCE.md` (this document) as the reference for how decisions are made and how to get help when a change needs broader agreement.
