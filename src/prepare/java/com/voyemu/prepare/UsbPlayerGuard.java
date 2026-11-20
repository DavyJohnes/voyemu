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
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Type-reference adaptation only; the original USB queue, session and UI remain intact. */
final class UsbPlayerGuard {

    static final String NAME = "api30-usb-player";
    static final String ORIGINAL = "Lorg/ijkplayer/IjkMediaPlayer;",
            HELPER = "Lcom/voyemu/oem/media/AndroidUsbPlayer;";
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
    static final Set<String> SOURCES = Set.of("com/voyemu/oem/media/AndroidUsbPlayer.java");

    private static void require(boolean value) {
        Failure.require(
                value,
                "USB_PLAYER_CONTRACT",
                "Exact Free API30 OEM USB player contract required",
                3);
    }

    private static void identity(Map<String, Object> constants, int api, String abi, Path original)
            throws Exception {
        require(
                api == 30
                        && "arm64-v8a".equals(abi)
                        && original != null
                        && StepConstants.admits(constants, FilesSupport.hash(original)));
    }

    static ImmutableDexFile rewrite(DexFile input, boolean inverse) throws Exception {
        final String before = inverse ? HELPER : ORIGINAL, after = inverse ? ORIGINAL : HELPER;
        var rewriter =
                new DexRewriter(
                        new RewriterModule() {
                            @Override
                            public Rewriter<String> getTypeRewriter(Rewriters rewriters) {
                                return value -> value.equals(before) ? after : value;
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

        return new ImmutableDexFile(Opcodes.forApi(30), output.values());
    }

    static void validateOriginal(
            Map<String, Object> constants, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        identity(constants, api, abi, apk);
        require(dex != null);
        rewrite(new DexBackedDexFile(Opcodes.forApi(30), dex), false);
    }

    static void validatePrepared(
            Map<String, Object> constants, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        identity(constants, api, abi, apk);
        require(dex != null);
        rewrite(new DexBackedDexFile(Opcodes.forApi(30), dex), false);
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

    static byte[] apply(Map<String, Object> constants, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        validatePrepared(constants, api, abi, apk, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] patched = FrameworkDexMaterializer.serialize(rewrite(before, false));
        var restored = new TreeMap<String, ClassDef>();

        for (ClassDef type :
                rewrite(new DexBackedDexFile(Opcodes.forApi(30), patched), true).getClasses()) {
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

    static void validateSource(Path source) throws Exception {
        require(GuestJava.sources(source).equals(SOURCES));
    }

    static Path compileHelper(
            Map<String, Object> constants,
            int api,
            String abi,
            Path apk,
            Path source,
            Path platform,
            Path work)
            throws Exception {
        identity(constants, api, abi, apk);
        validateSource(source);
        require(!Files.exists(work));
        Path dex = GuestJava.dex(new GuestJava.Unit(source, api), platform, work.resolve("java"));
        require(FilesSupport.tree(dex).keySet().equals(Set.of("classes.dex")));

        for (ClassDef type :
                new DexBackedDexFile(
                                Opcodes.forApi(30), Files.readAllBytes(dex.resolve("classes.dex")))
                        .getClasses()) {
            require(
                    type.getType().equals(HELPER)
                            || type.getType()
                                    .startsWith(HELPER.substring(0, HELPER.length() - 1) + "$"));
        }

        return dex.resolve("classes.dex");
    }
}
