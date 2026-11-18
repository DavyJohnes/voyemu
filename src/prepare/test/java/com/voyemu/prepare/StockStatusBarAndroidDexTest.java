package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.nio.file.Path;
import java.util.List;
import java.util.Set;

public class StockStatusBarAndroidDexTest {
    public static void main(String[] a) throws Exception {
        Path output =
                DexOperations.javaDex(
                        Path.of(a[0]), null, List.of(), Path.of(a[1]), Path.of(a[2]), 30);
        var names =
                NativeCore.classes(
                        DexFileFactory.loadDexFile(
                                output.resolve("classes.dex").toFile(), Opcodes.forApi(30)));

        if (!names.equals(Set.of(StockStatusBarGuard.ROUTER))) {
            throw new AssertionError("Only own singleton helper class allowed");
        }

        System.out.println("Actual Android classpath + D8: singleton own helper DEX PASS");
    }
}
