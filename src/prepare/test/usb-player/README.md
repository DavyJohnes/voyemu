# Android USB player lifecycle tests

Run with a full JDK 25 or newer from the checkout root:

```text
java src/prepare/test/usb-player/Run.java .
```

The Java launcher works on Windows and macOS; the final argument is the checkout path and may be absolute. On macOS, `sh src/prepare/test/usb-player/run.sh` also resolves the checkout from the script location.

The runner compiles the actual profile helper with private controlled Android MediaPlayer and OEM parent doubles, executes 43 lifecycle assertions, and requires four semantic mutations to fail. Compiler output uses an owned system temporary directory, removed after the run. No Android SDK, JUnit, Python, Node.js, or shared build outputs are used.

Checks cover prepared timing without automatic start, start/pause/resume, duration and position, deferred seek completion, invalid seek rejection, buffering/video callbacks, callback identity, completion and error propagation, checked callback exceptions, reset retaining OEM listeners, source errors, stop, release, repeated release and illegal-state propagation.

The doubles verify adapter delegation and listener semantics. They do not prove OEM binary compatibility, Android decoding, real guest playback, queue operation, or steering-wheel effects; those require independent compilation against OEM signatures and emulator acceptance.
