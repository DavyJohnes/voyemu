package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class OriginalNaviHelperCompileTest {
    public static void main(String[] a) throws Exception {
        if (a.length != 2) {
            throw new AssertionError();
        }

        Path src =
                Path.of(
                        "profiles/free-11-chinese-20260731-7.3.1/patches/original-navi-libraries/src");
        Path dex =
                DexOperations.javaDex(
                        src,
                        null,
                        List.of(),
                        Path.of(a[0]),
                        Path.of(a[1]),
                        30,
                        src.resolve("compile-only"));
        var d =
                new DexBackedDexFile(
                        Opcodes.forApi(30), Files.readAllBytes(dex.resolve("classes.dex")));
        Set<String> classes = new HashSet<>();

        for (ClassDef c : d.getClasses()) {
            classes.add(c.getType());
        }

        if (!classes.equals(Set.of("Lcom/android/server/pm/parsing/library/QgLibraries;"))) {
            throw new AssertionError("compile-only declarations leaked: " + classes);
        }

        Path egl = Path.of("profiles/free-11-chinese-20260731-7.3.1/patches/original-navi-egl/src");
        Path edex =
                DexOperations.javaDex(
                        egl, null, List.of(), Path.of(a[0]), Path.of(a[1] + "-egl"), 30);
        var e =
                new DexBackedDexFile(
                        Opcodes.forApi(30), Files.readAllBytes(edex.resolve("classes.dex")));
        Set<String> ec = new HashSet<>();

        for (ClassDef c : e.getClasses()) {
            ec.add(c.getType());
        }

        if (!ec.contains("Lcom/voyemu/navi/OwnContext;")
                || !ec.contains("Lcom/voyemu/egl/ScopedEglHook;")) {
            throw new AssertionError("boot helper missing");
        }

        System.out.println(
                "PASS javaDex compile-only excludes declarations; EGL OwnContext included");
    }
}
