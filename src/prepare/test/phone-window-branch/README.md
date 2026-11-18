# Phone window branch test

Run from the repository root with JDK 25. Replace the smali paths with the disassembled OEM and guard-produced `DialStatusChangeManager.smali` files. `--release 25` matches the class-file level of the CTest and Java helper builds.

```sh
classes=.local/build/jobs/phone-window-branch/classes
mkdir -p "$classes"
javac --release 25 -d "$classes" src/prepare/java/com/voyemu/prepare/Json.java src/prepare/test/java/com/voyemu/prepare/ActualSmaliBranchTest.java
java -cp "$classes" com.voyemu.prepare.ActualSmaliBranchTest old /path/to/original/DialStatusChangeManager.smali
java -cp "$classes" com.voyemu.prepare.ActualSmaliBranchTest new /path/to/prepared/DialStatusChangeManager.smali
```

The `old` mode prints the original dispatch trace and fails when the original branch skips the full-window show call. The `new` mode checks the guard-produced dispatch, gates, and modeled renderer idempotence. Both execute the input manager instructions and model external calls.
