package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.rewriter.DexRewriter;
import org.jf.dexlib2.rewriter.Rewriter;
import org.jf.dexlib2.rewriter.RewriterModule;
import org.jf.dexlib2.rewriter.Rewriters;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Set;
import java.util.TreeMap;

/** Type-reference adaptation only; the original USB queue, session and UI remain intact. */
final class Api28UsbPlayerGuard {
    static final String NAME = "api28-usb-player";
    static final String PIN = "4bc4c964d939541f6e11f4634df3d07bca06fb5dae3490e7284edf95d4c15e3f";
    static final String ORIGINAL = "Lorg/ijkplayer/IjkMediaPlayer;",
            HELPER = "Lcom/voyemu/oem/media/AndroidUsbPlayer;";
    static final String LOG_FACTORY = "Lorg/apache/commons/logging/LogFactory;",
            LOG_HELPER = "Lcom/voyemu/oem/media/UsbLogFactory;";
    static final String PLAYER = "Lcom/qinggan/media/usbmusic/Player";
    static final Set<String> TARGETS =
            Set.of(
                    PLAYER + ";",
                    PLAYER + "$1;",
                    PLAYER + "$2;",
                    PLAYER + "$3;",
                    PLAYER + "$4;",
                    PLAYER + "$5;",
                    PLAYER + "$ActionHandler;");
    static final Set<String> SOURCES =
            Set.of(
                    "com/voyemu/oem/media/AndroidUsbPlayer.java",
                    "com/voyemu/oem/media/UsbLogFactory.java");

    private static void require(boolean value) {
        Failure.require(
                value,
                "USB_PLAYER_CONTRACT",
                "Exact Free API28 OEM USB player contract required",
                3);
    }

    private static byte[] originalDex(Path apk) throws Exception {
        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            var entry = zip.getEntry("classes3.dex");
            require(entry != null);

            return zip.getInputStream(entry).readAllBytes();
        }
    }

    private static void identity(int api, String abi, Path original) throws Exception {
        require(
                api == 28
                        && "arm64-v8a".equals(abi)
                        && original != null
                        && PIN.equals(FilesSupport.hash(original)));
    }

    static ImmutableDexFile rewrite(DexFile input, boolean inverse) throws Exception {
        final String before = inverse ? HELPER : ORIGINAL, after = inverse ? ORIGINAL : HELPER;
        var rewriter =
                new DexRewriter(
                        new RewriterModule() {
                            @Override
                            public Rewriter<String> getTypeRewriter(Rewriters rewriters) {
                                return value ->
                                        value.equals(before)
                                                ? after
                                                : value.equals(inverse ? LOG_HELPER : LOG_FACTORY)
                                                        ? (inverse ? LOG_FACTORY : LOG_HELPER)
                                                        : value;
                            }
                        });
        var output = new TreeMap<String, ClassDef>();
        int targets = 0, changed = 0;

        for (ClassDef type : input.getClasses()) {
            ClassDef next = type;

            if (TARGETS.contains(type.getType())) {
                next = rewriter.getClassDefRewriter().rewrite(type);
                targets++;

                if (!FrameworkDexMaterializer.semantic(type)
                        .equals(FrameworkDexMaterializer.semantic(next))) {
                    changed++;
                }
            }

            output.put(next.getType(), next);
        }

        require(targets == TARGETS.size() && changed > 0);

        return new ImmutableDexFile(Opcodes.forApi(28), output.values());
    }

    static void validateOriginal(String profile, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        identity(api, abi, apk);
        require(dex != null);
        rewrite(new DexBackedDexFile(Opcodes.forApi(28), dex), false);
    }

    static void validatePrepared(String profile, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        identity(api, abi, apk);
        byte[] expected = originalDex(apk);
        require(dex != null && NativeCore.hash(expected).equals(NativeCore.hash(dex)));
        rewrite(new DexBackedDexFile(Opcodes.forApi(28), dex), false);
    }

    static org.jf.dexlib2.iface.Method canonicalMethod(org.jf.dexlib2.iface.Method method) {
        var body = method.getImplementation();

        if (body == null) {
            return method;
        }

        // DexPool joins adjacent equal-handler regions. Normalize exactly that encoding rule,
        // retaining every instruction, handler order/type/address, debug item and other property.
        var canonical =
                new org.jf.dexlib2.immutable.ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        body.getInstructions(),
                        org.jf.dexlib2.writer.util.TryListBuilder.massageTryBlocks(
                                body.getTryBlocks()),
                        body.getDebugItems());

        return new org.jf.dexlib2.immutable.ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                canonical);
    }

    static ClassDef canonicalClass(ClassDef type) {
        var direct = new ArrayList<org.jf.dexlib2.iface.Method>();
        var virtual = new ArrayList<org.jf.dexlib2.iface.Method>();

        for (var method : type.getDirectMethods()) {
            direct.add(canonicalMethod(method));
        }

        for (var method : type.getVirtualMethods()) {
            virtual.add(canonicalMethod(method));
        }

        return new org.jf.dexlib2.immutable.ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                type.getStaticFields(),
                type.getInstanceFields(),
                direct,
                virtual);
    }

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        validatePrepared(profile, api, abi, apk, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(28), dex);
        byte[] patched = FrameworkDexMaterializer.serialize(rewrite(before, false));
        var restored = new TreeMap<String, ClassDef>();

        for (ClassDef type :
                rewrite(new DexBackedDexFile(Opcodes.forApi(28), patched), true).getClasses()) {
            restored.put(type.getType(), type);
        }

        require(restored.size() == before.getClasses().size());

        for (ClassDef type : before.getClasses()) {
            if (!FrameworkDexMaterializer.semantic(canonicalClass(type))
                    .equals(
                            FrameworkDexMaterializer.semantic(
                                    canonicalClass(restored.get(type.getType()))))) {
                throw new Failure(
                        "USB_PLAYER_INVERSE",
                        "USB inverse changed class "
                                + type.getType()
                                + " at "
                                + FrameworkDexMaterializer.difference(
                                        canonicalClass(type),
                                        canonicalClass(restored.get(type.getType()))),
                        3);
            }
        }

        return patched;
    }

    /** Replaces the target classes of the decoded classes; they must still be the original. */
    static void adaptClasses(String profile, int api, String abi, Path apk, DexClasses classes)
            throws Exception {
        byte[] original = originalDex(apk);

        validateOriginal(profile, api, abi, apk, original);

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), original).getClasses()) {
            if (TARGETS.contains(type.getType())) {
                require(
                        "classes3.dex".equals(classes.entry(type.getType()))
                                && FrameworkDexMaterializer.semantic(type)
                                        .equals(
                                                FrameworkDexMaterializer.semantic(
                                                        classes.get(type.getType()))));
            }
        }

        byte[] patched = apply(profile, api, abi, apk, original);

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(28), patched).getClasses()) {
            if (TARGETS.contains(type.getType())) {
                classes.replace(type);
            }
        }
    }

    static void validateSource(Path source) throws Exception {
        require(GuestJava.sources(source).equals(SOURCES));
    }

    static Path compileHelper(
            String profile, int api, String abi, Path apk, Path source, Path platform, Path work)
            throws Exception {
        identity(api, abi, apk);
        validateSource(source);
        require(!Files.exists(work));
        Path dex = GuestJava.dex(new GuestJava.Unit(source, api), platform, work.resolve("java"));
        require(FilesSupport.tree(dex).keySet().equals(Set.of("classes.dex")));

        for (ClassDef type :
                new DexBackedDexFile(
                                Opcodes.forApi(28), Files.readAllBytes(dex.resolve("classes.dex")))
                        .getClasses()) {
            require(
                    type.getType().equals(HELPER)
                            || type.getType()
                                    .startsWith(HELPER.substring(0, HELPER.length() - 1) + "$")
                            || type.getType().equals(LOG_HELPER));
        }

        return dex.resolve("classes.dex");
    }
}
