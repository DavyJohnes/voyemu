# Security policy

English | [Русский](SECURITY.ru.md)

## Supported versions

Security fixes go into the latest release.

## Reporting a vulnerability

Report vulnerabilities privately by email to i@voyemu.com. Do not open a public issue for a vulnerability.

Describe the affected version, host operating system, profile, steps to reproduce and the observed impact. Do not attach OEM firmware files, prepared machine disks, access tokens or other secrets.

## Scope

Voyemu is a local tool. The QNX frame gRPC endpoint listens on `127.0.0.1` and requires the per-generation token from its discovery file. The Voyemu API runs as a local process through `voyemu test`. Guests have no outbound network access unless started with `--network=yes`.

Behavior of OEM applications inside a guest is outside the scope of this policy.
