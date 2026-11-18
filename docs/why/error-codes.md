# Error codes and the unknown outcome

## Decision

Host code reports a failure as `VoyemuError("CODE", "message")`, whose text is `CODE: message`. The API turns the uppercase `CODE` into `error.code`, and the CLI maps the response to its exit code. A code ends with `_UNKNOWN` only when the effect of the failed operation is unknown, and exactly those failures carry `outcome: unknown`; every other failure carries `outcome: not_started`. An exception without a code becomes `INTERNAL_ERROR`, and code must not hide a known failure behind it.

## Context

API clients drive long-running guests: a reboot, a tap or a `flush` can fail after the guest has already acted. A client has to decide whether to repeat a request, and a wrong decision either repeats an action or abandons one that never ran.

## Alternatives

**A separate outcome field chosen at each throw site.** The code and the outcome can disagree, and a reviewer must check both at every site.

**Numeric error codes.** They say nothing at the throw site and need a lookup table in every client.

**Mapping unexpected failures to one generic code.** The client loses the information it needs to decide on a retry.

## Argument

The suffix makes the outcome part of the name, so the throw site, the JSON response and the saved receipt cannot disagree. A reader of the code sees the classification where the failure happens. `not_started` tells a client that nothing happened; `unknown` forbids an automatic retry, and the client checks the guest state or reads the receipt with `operation.get`.

## Consequences

A failure path that may have reached the guest needs an `_UNKNOWN` code. The `retryable` flag is set separately and only for failures where a retry is safe, such as a busy session or the timeout of a read-only observation. Repeating the same request returns its saved receipt instead of executing it again, see the [Voyemu API](api.md). [Errors](../how/errors.md) lists the error fields and common codes.
