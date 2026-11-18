# Local roots

Everything Voyemu keeps locally, apart from machines, lives in four roots under `.local/` in the installation root. Each root has one kind of data and one deletion rule; the reasons are in [four `.local` roots](../why/local-roots.md).

| Root | Content | Deleting it |
| --- | --- | --- |
| `.local/profiles/` | OEM inputs per profile and shared immutable inputs | Requires importing the OEM inputs again with `fetch` |
| `.local/toolchains/` | Downloads, installed tools, the Android SDK, the QNX engine and `installed.json` | Requires `deps` and, for a cluster, a new QEMU and ANGLE binding |
| `.local/build/` | Build trees, temporary jobs and automation receipts | Safe when no build, preparation, session or automation uses it; outputs are rebuilt and saved receipts are lost |
| `.local/machines/<name>/` | Session state of one machine | Safe when the machine has no session; the next start recreates it and cold-boots Android |

## .local/profiles

`fetch` publishes the verified inputs of a profile into `.local/profiles/<profile>/`, together with the import receipts. Inputs shared by several profiles, such as the base Android system images, live in `.local/profiles/common/`. These files do not change after import.

## .local/toolchains

`deps` downloads pinned archives into `.local/toolchains/downloads/` and installs each tool into its own directory, for example `.local/toolchains/native-sdk/` for the Android SDK. There is one canonical copy of each pinned tool, shared by every machine. `installed.json` binds the installed QEMU and ANGLE to this host; machine preparation writes it.

## .local/build

Builds of Voyemu itself, preparation stages and temporary files go to `.local/build/`. The default build tree `.local/build/native/` places the launcher in the installation root. Automation keeps registered operations and their receipts in `.local/build/automation/`, so `operation.get` works only while they exist.

A successful `build` releases the outputs of its own preparation once the machine is published: the candidate Android disks, the artifacts it prepared and the QNX images it built and accepted. Receipts, logs, frames, signing keys and source patches of that preparation stay, as do shared inputs, outputs of a failed preparation and QNX inputs passed with `--reuse-qnx`. A file that no longer matches its receipt and a directory another preparation reads stay too; the `build_outputs` field of the result and an error-level log entry list each kept output.

## .local/machines

`.local/machines/<name>/` belongs to the machine directory with the same name. It holds the active session marker, one directory per session under `sessions/<id>/` with its logs and frame state, and the Android checkpoint. Machine disks with installed data are not here: they stay in `machines/<name>/`, see [profiles and machines](profiles-machines.md).

Never delete a root while a session, build, preparation or automation uses it. Stop its consumers first; the supervisor owns the files of an active session.
