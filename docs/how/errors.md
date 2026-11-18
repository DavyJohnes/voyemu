# Errors

Every Voyemu failure has an uppercase code and a message, written as `CODE: message`. The code tells the client what failed and whether the operation could have taken effect. The rule behind it is in [error codes and the unknown outcome](../why/error-codes.md).

## Error object

A failed [Voyemu API](api.md) response has `ok: false` and an `error` object:

```json
{"schema":1,"request_id":"tap-1","ok":false,"error":{"code":"TEST_BUSY","message":"another automation writer or capture is active","retryable":true,"outcome":"not_started"}}
```

| Field | Meaning |
| --- | --- |
| `code` | The uppercase code before the colon |
| `message` | The text after the colon, at most 1024 bytes |
| `retryable` | `true` only where repeating the request is safe |
| `outcome` | `not_started` or `unknown` |
| `details` | Optional; input and machine identity failures add structured details |

## Outcome

A code that ends with `_UNKNOWN` means the effect of the operation is unknown: the guest may already have acted, for example after a reboot or a `flush` was sent. Exactly these failures carry `outcome: unknown`, and a client must check the guest state or read the receipt with `operation.get` instead of repeating the action. Every other failure carries `outcome: not_started`: nothing happened, and a corrected request can be sent.

## Retryable failures

`retryable` is set only for failures where a repeat cannot cause a second effect:

| Code | Situation |
| --- | --- |
| `TEST_BUSY` | Another automation writer or screenshot is active |
| `CLUSTER_UI_NOT_READY` | The cluster cannot be observed yet |
| `TREE_TIMEOUT`, `TREE_TIMEOUT_UNKNOWN`, `TREE_TRANSPORT_UNKNOWN` | A UI tree read timed out or lost its transport |
| `CAN_TIMEOUT`, `CAN_TIMEOUT_UNKNOWN`, `CAN_TRANSPORT_UNKNOWN` | A CAN read timed out or lost its transport |

## Common codes

| Code | Meaning |
| --- | --- |
| `TEST_SCHEMA` | The request is not an object with `schema: 1` |
| `TEST_ARGUMENT` | A field is missing, malformed or out of range; also malformed JSON |
| `TEST_UNSUPPORTED` | Unknown operation or a capability this machine does not have |
| `TEST_SESSION` | The session ID is invalid, or the session is unknown, ambiguous, stopped or changed |
| `TEST_OWNERSHIP` | The request changes a session without its ownership |
| `REQUEST_CONFLICT` | The `request_id` was already used with different content |
| `PROFILE_NOT_PREPARED` | The selected machine has no prepared launch configuration |
| `INTERNAL_ERROR` | An unexpected failure without a code |

Host code never hides a known failure behind `INTERNAL_ERROR`. Codes of other areas, such as `FETCH_MISSING` for missing OEM inputs or `CLI_COMMAND` for an unknown command, follow the same format.

## Command line

Commands other than `test` print a failure as text on `stderr`, `Voyemu: CODE: message`, and exit with code `4`. `test` always prints a JSON response on `stdout`, even when the request cannot be read; its exit codes are listed in [the Voyemu API](api.md).
