package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.lang.invoke.MethodType;
import java.lang.reflect.Modifier;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Set;
import java.util.TreeSet;

import javax.tools.ToolProvider;

/** The JNI fixture must declare every native BackLightNative method of the pinned OEM framework. */
public final class A9BacklightAbiTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);
        String profile = "free-09-chinese-20260106-6.0.3";
        var definition =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + profile + "/profile.json"))));
        var pins = Json.obj(Json.obj(Json.obj(definition, "original"), "OEMFramework"), "sha256");
        Path originals = root.resolve(".local/profiles/" + profile + "/originals/framework");
        byte[] oat =
                NativeCore.pinned(
                        originals.resolve("arm64/boot-QGAPI.oat"),
                        Json.str(pins, "arm64/boot-QGAPI.oat"));
        byte[] vdex =
                NativeCore.pinned(
                        originals.resolve("boot-QGAPI.vdex"), Json.str(pins, "boot-QGAPI.vdex"));
        Files.write(work.resolve("boot-QGAPI.oat"), oat);
        Files.write(work.resolve("boot-QGAPI.vdex"), BootVdex.decode(vdex));
        Set<String> expected = new TreeSet<>();
        var container =
                DexFileFactory.loadDexContainer(
                        work.resolve("boot-QGAPI.oat").toFile(), Opcodes.forApi(28));

        for (String entry : container.getDexEntryNames()) {
            for (var type : container.getEntry(entry).getDexFile().getClasses()) {
                if (!type.getType().equals("Lcom/qinggan/os/BackLightNative;")) {
                    continue;
                }

                for (var method : type.getMethods()) {
                    if (AccessFlags.NATIVE.isSet(method.getAccessFlags())) {
                        expected.add(
                                method.getName()
                                        + "("
                                        + String.join("", method.getParameterTypes())
                                        + ")"
                                        + method.getReturnType());
                    }
                }
            }
        }

        Path classes = work.resolve("classes");
        Files.createDirectories(classes);
        int code =
                ToolProvider.getSystemJavaCompiler()
                        .run(
                                null,
                                null,
                                null,
                                "--release",
                                "25",
                                "-d",
                                classes.toString(),
                                root.resolve(
                                                "src/prepare/test/qinggan09-backlight/src/compile-only/com/qinggan/os/BackLightNative.java")
                                        .toString());

        if (code != 0) {
            throw new AssertionError("JNI fixture did not compile");
        }

        Set<String> actual = new TreeSet<>();

        try (var loader = new URLClassLoader(new java.net.URL[] {classes.toUri().toURL()}, null)) {
            for (var method :
                    Class.forName("com.qinggan.os.BackLightNative", false, loader)
                            .getDeclaredMethods()) {
                if (Modifier.isNative(method.getModifiers())) {
                    actual.add(
                            method.getName()
                                    + MethodType.methodType(
                                                    method.getReturnType(),
                                                    method.getParameterTypes())
                                            .descriptorString());
                }
            }
        }

        if (expected.size() != 24 || !expected.equals(actual)) {
            throw new AssertionError(
                    "OEM JNI declaration mismatch: expected=" + expected + ", actual=" + actual);
        }

        System.out.println(
                "A9 BackLight JNI contract PASS: 24 fixture declarations equal the pinned OEM"
                        + " framework native signatures");
    }
}
