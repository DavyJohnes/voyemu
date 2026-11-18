# Four `.local` roots

## Decision

Local state lives in exactly four roots under `.local/`: `profiles` for OEM inputs and shared immutable preparation inputs, `toolchains` for installed tools and downloads, `build` for reproducible outputs and temporary jobs, and `machines/<name>` for ephemeral per-machine state. Persistent guest disks and installed data stay in `machines/<name>/` outside `.local/`.

## Context

Voyemu handles data with different lifetimes and owners: firmware that the user imports, pinned tools, rebuildable outputs, launch state and guest disks with installed applications. Mixing them makes it unclear what can be deleted, shared or copied.

## Alternatives

**One cache directory for everything.** Deleting the cache to free space could remove OEM inputs or a session's live state.

**Per-tool directories as each tool chooses.** Tools, downloads and outputs scatter across the project and the user's home directory.

## Argument

Each root has one rule. `build` can be deleted and rebuilt. `toolchains` holds one canonical copy of each pinned tool, shared by every machine. `profiles` holds inputs that do not change after import. `.local/machines/<name>` is recreated when absent, so a machine copy does not carry stale session state.

## Consequences

A copy of `machines/<name>/` preserves installed data without `.local/machines/`; Voyemu recreates that state and cold-boots when checkpoints are absent. Separate jobs share immutable inputs and create only writable overlays instead of copying SDK or project trees. See [profiles and machines](profiles-machines.md) for what a machine embeds. [Local roots](../how/local-roots.md) lists what each root holds and when it is safe to delete.
