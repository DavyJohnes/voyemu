package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.StringReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * Silence the two proven foreground notifications, preserving all Binder, player and media
 * behavior.
 */
final class PassionMusicForegroundGuard {
    static final String NAME = "passion-api30-music-foreground";
    static final String PIN = "a3c788860d5df27277d53ffbf838daaefddb6ab098ad52b636671dd70b34765d";
    static final String OWNER = "Lcom/voyah/local/core/service/MusicService;",
            OLD_ID = "MusicService",
            QUIET_ID = "voyemu.passion.music.foreground.v1";
    static final String BRIDGE = "Lcom/voyah/multimedia/bridge/service/MultiMediaBridgeService;",
            BRIDGE_OLD_ID = "CHANNEL_ID",
            BRIDGE_QUIET_ID = "voyemu.passion.music.bridge.foreground.v1";
    private static final String SOUND =
            "Landroid/app/NotificationChannel;->setSound(Landroid/net/Uri;Landroid/media/AudioAttributes;)V";

    private static void require(boolean x) {
        Failure.require(
                x,
                "PASSION_MUSIC_FOREGROUND_CONTRACT",
                "Exact owner-pinned Passion Music foreground channel required",
                3);
    }

    static Method method(DexFile dex) {
        return method(dex, OWNER);
    }

    static Method method(DexFile dex, String owner) {
        Method found = null;

        for (ClassDef c : dex.getClasses()) {
            if (c.getType().equals(owner)) {
                for (Method m : c.getVirtualMethods()) {
                    if (m.getName().equals("onCreate")) {
                        require(
                                found == null
                                        && m.getParameterTypes().isEmpty()
                                        && m.getReturnType().equals("V")
                                        && m.getAccessFlags() == 1);
                        found = m;
                    }
                }
            }
        }

        require(found != null && found.getImplementation() != null);

        return found;
    }

    static void validateMethod(Method m) throws Exception {
        require(
                (OWNER.equals(m.getDefiningClass()) || BRIDGE.equals(m.getDefiningClass()))
                        && m.getName().equals("onCreate")
                        && m.getParameterTypes().isEmpty()
                        && m.getReturnType().equals("V"));
    }

    /** Exact rewritten channel: instruction addresses, registers and the expected end. */
    private record ChannelLayout(
            String oldId,
            String quietId,
            int branchAt,
            int branchOffset,
            int idAt,
            int idRegister,
            int muteAt,
            int muteRegister,
            int soundAt,
            int soundRegister,
            int createAt,
            int builderAt,
            int end) {}

    private static final ChannelLayout MUSIC_CHANNEL =
            new ChannelLayout(OLD_ID, QUIET_ID, 7, 44, 20, 3, 27, 2, 28, 1, 31, 40, 60);
    private static final ChannelLayout BRIDGE_CHANNEL =
            new ChannelLayout(
                    BRIDGE_OLD_ID, BRIDGE_QUIET_ID, 25, 55, 27, 0, 37, 3, 38, 2, 60, 69, 89);

    static void validateAfter(Method m) {
        if (BRIDGE.equals(m.getDefiningClass())) {
            validateBridgeAfter(m);

            return;
        }

        var b = m.getImplementation();
        require(b != null && b.getRegisterCount() == 6 && b.getTryBlocks().isEmpty());
        requireQuietChannel(b, MUSIC_CHANNEL);
    }

    static void validateBridgeAfter(Method m) {
        var b = m.getImplementation();

        require(
                BRIDGE.equals(m.getDefiningClass())
                        && b != null
                        && b.getRegisterCount() == 6
                        && b.getTryBlocks().isEmpty());
        requireQuietChannel(b, BRIDGE_CHANNEL);
    }

    private static void requireQuietChannel(MethodImplementation b, ChannelLayout l) {
        int at = 0, checked = 0, ids = 0, sounds = 0;

        for (var i : b.getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference() instanceof StringReference v) {
                require(!v.getString().equals(l.oldId));

                if (v.getString().equals(l.quietId)) {
                    ids++;
                }
            }

            if (i instanceof ReferenceInstruction r && r.getReference().toString().equals(SOUND)) {
                sounds++;
            }

            if (requireChannelInstruction(i, at, l)) {
                checked++;
            }

            at += i.getCodeUnits();
        }

        require(at == l.end && checked == 6 && ids == 1 && sounds == 1);
    }

    private static boolean requireChannelInstruction(Instruction i, int at, ChannelLayout l) {
        if (at == l.branchAt) {
            require(
                    i.getOpcode() == Opcode.IF_LT
                            && ((OffsetInstruction) i).getCodeOffset() == l.branchOffset);
        } else if (at == l.idAt) {
            require(
                    i.getOpcode() == Opcode.CONST_STRING
                            && ((OneRegisterInstruction) i).getRegisterA() == l.idRegister
                            && ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .equals(l.quietId));
        } else if (at == l.muteAt) {
            require(
                    i.getOpcode() == Opcode.CONST_4
                            && ((OneRegisterInstruction) i).getRegisterA() == l.muteRegister
                            && ((NarrowLiteralInstruction) i).getNarrowLiteral() == 0);
        } else if (at == l.soundAt) {
            require(
                    i.getOpcode() == Opcode.INVOKE_VIRTUAL
                            && ((ReferenceInstruction) i).getReference().toString().equals(SOUND));
            var r = (FiveRegisterInstruction) i;
            require(
                    r.getRegisterCount() == 3
                            && r.getRegisterC() == l.soundRegister
                            && r.getRegisterD() == l.muteRegister
                            && r.getRegisterE() == l.muteRegister);
        } else if (at == l.createAt) {
            require(
                    ((ReferenceInstruction) i)
                            .getReference()
                            .toString()
                            .equals(
                                    "Landroid/app/NotificationManager;->createNotificationChannel(Landroid/app/NotificationChannel;)V"));
        } else if (at == l.builderAt) {
            require(
                    ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Landroid/app/Notification$Builder;-><init>(Landroid/content/Context;Ljava/lang/String;)V")
                            && ((FiveRegisterInstruction) i).getRegisterE() == l.idRegister);
        } else {
            return false;
        }

        return true;
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN) && dex != null);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        validateMethod(method(source));
        validateMethod(method(source, BRIDGE));
    }

    private static ImmutableDexFile rewrite(DexFile source, boolean inverse) throws Exception {
        var classes = new TreeMap<String, ClassDef>();

        for (var c : source.getClasses()) {
            if (!c.getType().equals(OWNER) && !c.getType().equals(BRIDGE)) {
                classes.put(c.getType(), c);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (Method m : c.getVirtualMethods()) {
                if (m.getName().equals("onCreate")
                        && m.getParameterTypes().isEmpty()
                        && m.getReturnType().equals("V")) {
                    if (inverse) {
                        validateAfter(m);
                    } else {
                        validateMethod(m);
                    }

                    var body = new MutableMethodImplementation(m.getImplementation());

                    if (c.getType().equals(BRIDGE)) {
                        if (inverse) {
                            body.removeInstruction(18);
                            body.removeInstruction(17);
                            body.replaceInstruction(
                                    12,
                                    new BuilderInstruction21c(
                                            Opcode.CONST_STRING,
                                            0,
                                            new ImmutableStringReference(BRIDGE_OLD_ID)));
                        } else {
                            body.replaceInstruction(
                                    12,
                                    new BuilderInstruction21c(
                                            Opcode.CONST_STRING,
                                            0,
                                            new ImmutableStringReference(BRIDGE_QUIET_ID)));
                            body.addInstruction(
                                    17, new BuilderInstruction11n(Opcode.CONST_4, 3, 0));
                            body.addInstruction(
                                    18,
                                    new BuilderInstruction35c(
                                            Opcode.INVOKE_VIRTUAL,
                                            3,
                                            2,
                                            3,
                                            3,
                                            0,
                                            0,
                                            new ImmutableMethodReference(
                                                    "Landroid/app/NotificationChannel;",
                                                    "setSound",
                                                    List.of(
                                                            "Landroid/net/Uri;",
                                                            "Landroid/media/AudioAttributes;"),
                                                    "V")));
                        }
                    } else {
                        if (inverse) {
                            body.removeInstruction(14);
                            body.removeInstruction(13);
                            body.replaceInstruction(
                                    10,
                                    new BuilderInstruction21c(
                                            Opcode.CONST_STRING,
                                            3,
                                            new ImmutableStringReference(OLD_ID)));
                        } else {
                            body.replaceInstruction(
                                    10,
                                    new BuilderInstruction21c(
                                            Opcode.CONST_STRING,
                                            3,
                                            new ImmutableStringReference(QUIET_ID)));
                            body.addInstruction(
                                    13, new BuilderInstruction11n(Opcode.CONST_4, 2, 0));
                            body.addInstruction(
                                    14,
                                    new BuilderInstruction35c(
                                            Opcode.INVOKE_VIRTUAL,
                                            3,
                                            1,
                                            2,
                                            2,
                                            0,
                                            0,
                                            new ImmutableMethodReference(
                                                    "Landroid/app/NotificationChannel;",
                                                    "setSound",
                                                    List.of(
                                                            "Landroid/net/Uri;",
                                                            "Landroid/media/AudioAttributes;"),
                                                    "V")));
                        }
                    }

                    m =
                            new ImmutableMethod(
                                    m.getDefiningClass(),
                                    m.getName(),
                                    m.getParameters(),
                                    m.getReturnType(),
                                    m.getAccessFlags(),
                                    m.getAnnotations(),
                                    m.getHiddenApiRestrictions(),
                                    body);
                }

                methods.add(m);
            }

            classes.put(
                    c.getType(),
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

        return new ImmutableDexFile(Opcodes.forApi(30), classes.values());
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] out = FrameworkDexMaterializer.serialize(rewrite(source, false));
        var read = new DexBackedDexFile(Opcodes.forApi(30), out);
        validateAfter(method(read));
        validateBridgeAfter(method(read, BRIDGE));
        var restored = rewrite(read, true);
        var byName = new TreeMap<String, ClassDef>();

        for (var c : restored.getClasses()) {
            byName.put(c.getType(), c);
        }

        require(byName.size() == source.getClasses().size());

        for (var c : source.getClasses()) {
            require(
                    byName.containsKey(c.getType())
                            && FrameworkDexMaterializer.semantic(c)
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    byName.get(c.getType()))));
        }

        return out;
    }
}
