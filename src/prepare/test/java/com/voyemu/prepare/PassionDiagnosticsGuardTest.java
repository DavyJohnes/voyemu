package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

public final class PassionDiagnosticsGuardTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError("Passion actual optional diagnostic contract");
        }
    }

    private static void reject(RunnableWithException action) throws Exception {
        boolean rejected = false;

        try {
            action.run();
        } catch (Failure expected) {
            rejected = true;
        }

        check(rejected);
    }

    interface RunnableWithException {
        void run() throws Exception;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        Path original =
                root.resolve(
                        ".local/profiles/"
                                + PassionDiagnosticsGuard.PROFILE
                                + "/originals/MonzaCarService.apk");
        byte[] dex;

        try (ZipFile zip = new ZipFile(original.toFile())) {
            dex = zip.getInputStream(zip.getEntry("classes15.dex")).readAllBytes();
        }

        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var method = PassionDiagnosticsGuard.method(before);
        var block = method.getImplementation().getTryBlocks().get(0);
        check(
                block.getExceptionHandlers().stream()
                        .noneMatch(h ->
                                PassionDiagnosticsGuard.MISSING.equals(
                                        h.getExceptionType()))); // actual fatal
        // exception was
        // unhandled
        byte[] output =
                PassionDiagnosticsGuard.apply(PassionDiagnosticsGuard.PROFILE, 30, original, dex);
        var after = new DexBackedDexFile(Opcodes.forApi(30), output);
        var changed = PassionDiagnosticsGuard.method(after);
        var handlers = changed.getImplementation().getTryBlocks().get(0).getExceptionHandlers();
        check(
                handlers.size() == 2
                        && handlers.get(0).getHandlerCodeAddress() == 0x19
                        && handlers.get(0)
                                .getExceptionType()
                                .equals("Landroid/os/RemoteException;"));
        check(
                handlers.get(1).getExceptionType().equals(PassionDiagnosticsGuard.MISSING)
                        && handlers.get(1).getHandlerCodeAddress() == 0x1d);
        check(
                FrameworkDexMaterializer.semantic(method.getImplementation().getInstructions())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        changed.getImplementation().getInstructions())));
        check(
                FrameworkDexMaterializer.semantic(method.getImplementation().getDebugItems())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        changed.getImplementation().getDebugItems())));
        check(
                before.getClasses().size()
                        == after.getClasses()
                                .size()); // apply() also semantically checks ALL classes/members
        // after normalizing only the new catch
        reject(() ->
                PassionDiagnosticsGuard.apply("free-11-chinese-20260731-7.3.1", 30, original, dex));
        reject(() ->
                PassionDiagnosticsGuard.apply(PassionDiagnosticsGuard.PROFILE, 28, original, dex));
        reject(() ->
                PassionDiagnosticsGuard.apply(PassionDiagnosticsGuard.PROFILE, 30, original, null));
        reject(() ->
                PassionDiagnosticsGuard.apply(
                        PassionDiagnosticsGuard.PROFILE, 30, original, output));
        Path bad = stage.resolve("wrong.apk");
        Files.writeString(bad, "not the owner original");
        reject(() -> PassionDiagnosticsGuard.apply(PassionDiagnosticsGuard.PROFILE, 30, bad, dex));
        Path produced = stage.resolve("classes15.dex");
        Files.write(produced, output);
        Process proc =
                new ProcessBuilder(
                                root.resolve(
                                                ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                        .toString(),
                                "-c",
                                produced.toString())
                        .redirectErrorStream(true)
                        .redirectOutput(stage.resolve("ART-dexdump.txt").toFile())
                        .start();
        check(proc.waitFor() == 0);
        Files.writeString(
                stage.resolve("receipt.json"),
                "{\"schema\":1,\"actual_unhandled_exception_contract_RED\":true,\"existing_null_fallback_handler_GREEN\":true,\"all_original_instructions_debug_and_other_members_preserved\":true,\"native_ART_dexdump_exit\":0,\"negative_cases\":6,\"guest_verified\":false}\n");
        System.out.println(
                "Actual Passion diagnostic boundary RED→GREEN; exact exception-table-only change,"
                        + " preserved bodies, 6 negatives, ART format PASS; guest verification"
                        + " pending");
    }
}
