# Security Policy — ZHMModSDK (fork)

## Documented residual risks (no fixed release reachable)

### Dependabot alert: `serde_with` 2.3.3 (KeyValueMap serialization panic)

- **Status:** open Dependabot alert, documented residual risk. Not dismissed, not muted.
- **Chain:** `zhmmodsdk_rs` → `quickentity-rs` (git rev `fc6d5fd`, pinned in
  `ZHMModSDK/Rust/Cargo.toml`) → `serde_with ^2.0.1` → locked 2.3.3
  (vulnerable range `>= 2.3.0, < 3.21.0`; panic in KeyValueMap serialization
  on empty sequence/map entries).
- **Why it cannot be fixed today (verified 2026-11-18):**
  - The fix is `serde_with 3.21.0` — a semver-major over the `^2.0.1`
    requirement declared inside the pinned `quickentity-rs` git dependency;
    cargo cannot resolve it, and a `[patch.crates-io]` override is rejected
    for the same version-requirement mismatch.
  - Upstream `atampy25/quickentity-rs` HEAD (commit `2e7a3563`, 2026-06-26)
    still declares `serde_with = "2.0.1"` — no upstream release or branch
    carries the 3.x migration. (Its sibling `hitman-commons` already uses
    3.21.0, so the migration is upstream's pending work, not ours.)
- **Exposure:** ZHMModSDK is an offline modding SDK; the affected
  KeyValueMap code path handles Hitman entity JSON conversion with
  SDK-local inputs. The defect is a panic (DoS-class), not memory corruption.
- **Resolution path:** when `quickentity-rs` publishes a commit/release with
  `serde_with >= 3.21.0`, bump the pinned rev and delete this entry.

## Reporting a Vulnerability

Use the upstream repository's security policy for upstream code; open a
private vulnerability report on this fork for fork-specific changes.
