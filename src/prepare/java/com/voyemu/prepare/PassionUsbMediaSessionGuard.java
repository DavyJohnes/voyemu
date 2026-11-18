package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/** Adds a lifecycle-bound callback to the exact OEM USB media browser service. */
final class PassionUsbMediaSessionGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            APK = "a3c788860d5df27277d53ffbf838daaefddb6ab098ad52b636671dd70b34765d",
            NAME = "passion-api30-usb-media-session";
    static final String OWNER = "Lcom/voyah/baselibrary/player/MultiMediaBrowserService;",
            HELPER = "Lcom/voyemu/media/PassionUsbMediaSession;",
            SESSION = "Landroid/support/v4/media/session/MediaSessionCompat;";
    static final Set<String> SOURCES =
            Set.of(
                    "com/voyemu/media/PassionUsbMediaSession.java",
                    "compile-only/com/voyah/baselibrary/player/MediaManager.java",
                    "compile-only/com/voyah/baselibrary/lifecycle/SafeMutableLiveData.java",
                    "compile-only/android/support/v4/media/session/PlaybackStateCompat.java",
                    "compile-only/android/support/v4/media/session/MediaSessionCompat.java");

    static void need(boolean value) {
        Failure.require(
                value, "PASSION_USB_MEDIA_CONTRACT", "Exact OEM USB media service required", 3);
    }

    static boolean create(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("onCreate")
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("V");
    }

    static boolean destroy(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("onDestroy")
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("V");
    }

    static ClassDef owner(DexFile dex) {
        ClassDef found = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(OWNER)) {
                need(found == null);
                found = c;
            }
        }

        need(found != null);

        return found;
    }

    static void validate(String profile, int api, Path apk, byte[] bytes) throws Exception {
        need(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(apk).equals(APK)
                        && bytes != null);
        var c = owner(new DexBackedDexFile(Opcodes.forApi(30), bytes));
        int count = 0;

        for (var m : c.getVirtualMethods()) {
            need(!destroy(m));

            if (create(m)) {
                need(m.getAccessFlags() == 1 && m.getImplementation().getRegisterCount() == 5);
                count++;
            }
        }

        need(count == 1);
    }

    static void validateOriginalChain(String profile, int api, Path apk, byte[] raw)
            throws Exception {
        var foreground = PassionMusicForegroundGuard.apply(profile, api, apk, raw);

        validate(profile, api, apk, PassionMusicInsetsGuard.apply(profile, api, apk, foreground));
    }

    static void validateSource(Path source) throws Exception {
        var tree = FilesSupport.tree(source);

        need(tree.keySet().equals(SOURCES));
    }

    static Path compileHelper(
            String profile, int api, Path apk, Path source, Path platform, Path work)
            throws Exception {
        need(PROFILE.equals(profile) && api == 30 && FilesSupport.hash(apk).equals(APK));
        validateSource(source);
        Path dex =
                GuestJava.dex(new GuestJava.Unit(source, api), platform, work)
                        .resolve("classes.dex");
        byte[] bytes = Files.readAllBytes(dex);
        PassionGeneratedHelperContract.verify(PassionGeneratedHelperContract.USB, bytes);

        return dex;
    }

    static Method route(Method m, boolean inverse) throws Exception {
        need(create(m));
        var b = new MutableMethodImplementation(m.getImplementation());
        var ins = b.getInstructions();
        int last = ins.size() - 1;
        need(last > 0 && ins.get(last).getOpcode() == Opcode.RETURN_VOID);

        if (inverse) {
            var call = ins.get(last - 1);
            need(
                    call.getOpcode() == Opcode.INVOKE_STATIC
                            && call instanceof FiveRegisterInstruction v
                            && v.getRegisterCount() == 2
                            && v.getRegisterC() == 4
                            && v.getRegisterD() == 0
                            && ((ReferenceInstruction) call)
                                    .getReference()
                                    .toString()
                                    .equals(
                                            HELPER
                                                    + "->install(Ljava/lang/Object;"
                                                    + SESSION
                                                    + ")V"));
            b.removeInstruction(last - 1);
        } else {
            b.addInstruction(
                    last,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            2,
                            4,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    HELPER,
                                    "install",
                                    List.of("Ljava/lang/Object;", SESSION),
                                    "V")));
        }

        var debug = new ArrayList<DebugItem>();
        var original = m.getImplementation().getDebugItems().iterator();

        for (var x : b.getDebugItems()) {
            need(original.hasNext());
            var old = original.next();
            need(old.getDebugItemType() == x.getDebugItemType());

            if (x instanceof EndLocal e && old instanceof EndLocal y) {
                debug.add(
                        new ImmutableEndLocal(
                                x.getCodeAddress(),
                                e.getRegister(),
                                y.getName(),
                                y.getType(),
                                y.getSignature()));
            } else if (x instanceof RestartLocal e && old instanceof RestartLocal y) {
                debug.add(
                        new ImmutableRestartLocal(
                                x.getCodeAddress(),
                                e.getRegister(),
                                y.getName(),
                                y.getType(),
                                y.getSignature()));
            } else {
                debug.add(x);
            }
        }

        need(!original.hasNext());

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        b.getRegisterCount(), b.getInstructions(), b.getTryBlocks(), debug));
    }

    static Method teardown() {
        var code =
                List.of(
                        new ImmutableInstruction35c(
                                Opcode.INVOKE_STATIC,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        HELPER, "dispose", List.of("Ljava/lang/Object;"), "V")),
                        new ImmutableInstruction35c(
                                Opcode.INVOKE_SUPER,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Landroidx/media/MediaBrowserServiceCompat;",
                                        "onDestroy",
                                        List.of(),
                                        "V")),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID));

        return new ImmutableMethod(
                OWNER,
                "onDestroy",
                List.of(),
                "V",
                1,
                Set.of(),
                Set.of(),
                new ImmutableMethodImplementation(1, code, List.of(), List.of()));
    }

    static ImmutableDexFile rewrite(DexFile dex, boolean inverse) throws Exception {
        var all = new ArrayList<ClassDef>();
        int count = 0;

        for (var c : dex.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                all.add(c);
                continue;
            }

            var methods = new ArrayList<Method>();
            boolean removed = false;

            for (var m : c.getVirtualMethods()) {
                if (create(m)) {
                    m = route(m, inverse);
                    count++;
                } else if (destroy(m)) {
                    need(
                            inverse
                                    && FrameworkDexMaterializer.semantic(m)
                                            .equals(FrameworkDexMaterializer.semantic(teardown())));
                    removed = true;
                    continue;
                }

                methods.add(m);
            }

            if (inverse) {
                need(removed);
            } else {
                methods.add(teardown());
            }

            all.add(
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            c.getDirectMethods(),
                            methods));
        }

        need(count == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), all);
    }

    static byte[] apply(String profile, int api, Path apk, byte[] bytes) throws Exception {
        validate(profile, api, apk, bytes);
        var before = new DexBackedDexFile(Opcodes.forApi(30), bytes);
        var expected = rewrite(before, false);
        var out = FrameworkDexMaterializer.serialize(expected);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        need(
                FrameworkDexMaterializer.semantic(expected.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(actual.getClasses())));
        need(
                FrameworkDexMaterializer.semantic(before.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        rewrite(actual, true).getClasses())));

        return out;
    }
}
