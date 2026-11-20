package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.zip.ZipFile;

public final class MusicPageGuardTest {

    static void check(boolean b, String message) {
        if (!b) {
            throw new AssertionError(message);
        }
    }

    static Method method(byte[] dex, String owner, String name) {
        for (var c : MusicPageGuard.classes(dex).values()) {
            if (c.getType().equals(owner)) {
                for (var m : c.getMethods()) {
                    if (m.getName().equals(name)) {
                        return m;
                    }
                }
            }
        }

        throw new AssertionError(name);
    }

    record Execution(Object value, int probes) {}

    // Execute the actual finite DEX branches; only external property/plugin/fragment factories are
    // controlled.
    static Execution execute(Method method, boolean enabled, boolean present, int position) {
        var code = new ArrayList<Instruction>();
        var addresses = new HashMap<Integer, Integer>();
        int address = 0;

        for (var i : method.getImplementation().getInstructions()) {
            addresses.put(address, code.size());
            code.add(i);
            address += i.getCodeUnits();
        }

        Object[] registers = new Object[method.getImplementation().getRegisterCount()];
        registers[registers.length - 1] = position;
        Object result = null;
        int pc = 0, probes = 0;

        for (int limit = 0; limit < 100; limit++) {
            var i = code.get(addresses.get(pc));
            int next = pc + i.getCodeUnits();
            int a = i instanceof OneRegisterInstruction r ? r.getRegisterA() : 0;

            switch (i.getOpcode()) {
                case CONST_4 -> registers[a] = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                case CONST_STRING ->
                        registers[a] = ((ReferenceInstruction) i).getReference().toString();
                case MOVE_RESULT, MOVE_RESULT_OBJECT -> registers[a] = result;
                case MOVE_EXCEPTION -> registers[a] = new ClassNotFoundException();
                case RETURN, RETURN_OBJECT -> {
                    return new Execution(registers[a], probes);
                }
                case SPUT -> result = registers[a];
                case RETURN_VOID -> {
                    return new Execution(result, probes);
                }
                case IF_EQZ, IF_NEZ -> {
                    boolean zero = registers[a] == null || registers[a].equals(0);

                    if (zero == (i.getOpcode() == Opcode.IF_EQZ)) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case IF_EQ -> {
                    if (Objects.equals(
                            registers[a], registers[((TwoRegisterInstruction) i).getRegisterB()])) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case GOTO -> next = pc + ((OffsetInstruction) i).getCodeOffset();
                case INVOKE_STATIC, INVOKE_VIRTUAL -> {
                    String ref = ((ReferenceInstruction) i).getReference().toString();

                    if (ref.contains("->isSupportKwMusic()")) {
                        result = enabled ? 1 : 0;
                    } else if (ref.contains("Ljava/lang/Class;->forName")) {
                        probes++;
                        check(
                                Objects.equals(
                                        registers[((FiveRegisterInstruction) i).getRegisterC()],
                                        MusicPageGuard.IMPLEMENTATION),
                                "exact OEM reflection name");

                        if (!present) {
                            boolean caught = false;

                            for (var block : method.getImplementation().getTryBlocks()) {
                                if (pc >= block.getStartCodeAddress()
                                        && pc
                                                < block.getStartCodeAddress()
                                                        + block.getCodeUnitCount()) {
                                    for (var handler : block.getExceptionHandlers()) {
                                        check(
                                                handler.getExceptionType()
                                                        .equals(
                                                                "Ljava/lang/ClassNotFoundException;"),
                                                "only missing implementation caught");
                                        next = handler.getHandlerCodeAddress();
                                        caught = true;
                                    }
                                }
                            }

                            check(caught, "missing class handled");
                        } else {
                            result = String.class;
                        }
                    } else if (ref.contains("KwAppManager;->getInstance")) {
                        result = "manager";
                    } else if (ref.contains("->getKwRootFragment")) {
                        result = present ? "KW" : null;
                    } else if (ref.contains("BtMusicFragment;->newInstance")) {
                        result = "BT";
                    } else if (ref.contains("UsbMusicFragment;->newInstance")) {
                        result = "USB";
                    } else {
                        throw new AssertionError(ref);
                    }
                }
                default -> throw new AssertionError(i.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("bounded execution");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        try {
            action.run();
            throw new AssertionError("contract accepted");
        } catch (Failure e) {
            check(e.code.equals("MUSIC_PAGE_CONTRACT"), "precise contract failure");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        Map<String, Object> constants =
                TestSteps.select(
                        root, "music_fullscreen_boundary", false, MusicFullscreenGuard.NAME);
        Path original = TestSteps.original(root, constants, "MusicConcate.apk");
        byte[] source;

        try (var zip = new ZipFile(original.toFile())) {
            source = zip.getInputStream(zip.getEntry("classes.dex")).readAllBytes();

            for (var entry : Collections.list(zip.entries())) {
                if (entry.getName().matches("classes[0-9]*.dex")) {
                    var types = MusicPageGuard.classes(zip.getInputStream(entry).readAllBytes());
                    check(
                            !types.containsKey("Lcom/qinggan/app/kwSdkMusic/KwApiImpl;"),
                            "actual missing optional implementation across every original DEX");
                }
            }
        }

        byte[] patched =
                args.length > 1 && args[1].equals("--original")
                        ? source
                        : MusicPageGuard.apply(constants, 30, original, source);
        var support = method(patched, MusicPageGuard.OWNER, "isSupportKwMusic");
        var factory =
                method(
                        source,
                        "Lcom/qinggan/app/music/activity/CustomFragmentPagerAdapter;",
                        "buildMusicFragment");
        check(
                execute(support, true, false, 0).value().equals(0),
                "missing Kuwo must select genuine OEM fallback");
        check(
                execute(support, false, true, 0).equals(new Execution(0, 0)),
                "disabled flag never probes implementation");
        check(
                execute(support, true, true, 0).equals(new Execution(1, 1)),
                "available enabled implementation retained");

        for (boolean enabled : List.of(false, true)) {
            for (boolean present : List.of(false, true)) {
                boolean supported = execute(support, enabled, present, 0).value().equals(1);
                int count =
                        (Integer)
                                execute(
                                                method(
                                                        source,
                                                        "Lcom/qinggan/app/music/activity/CustomFragmentPagerAdapter;",
                                                        "<clinit>"),
                                                supported,
                                                present,
                                                0)
                                        .value();

                for (int position = 0; position < count; position++) {
                    check(
                            execute(factory, supported, present, position).value() != null,
                            "every valid adapter position has real fragment");
                }

                if (!supported) {
                    check(
                            execute(factory, false, present, 0).value().equals("BT"),
                            "OEM BT fallback");
                    check(
                            execute(factory, false, present, 1).value().equals("USB"),
                            "OEM USB fallback");
                }
            }
        }

        reject(() ->
                MusicPageGuard.apply(
                        Map.of(StepConstants.PINS, List.of("0".repeat(64))), 30, original, source));
        reject(() -> MusicPageGuard.apply(constants, 28, original, source));
        reject(() -> MusicPageGuard.apply(constants, 30, original, patched));

        if (args.length > 1 && !args[1].equals("--original")) {
            try (var zip = new ZipFile(args[1])) {
                check(
                        Arrays.equals(
                                MusicFullscreenGuard.apply(constants, 30, original, patched),
                                zip.getInputStream(zip.getEntry("classes.dex")).readAllBytes()),
                        "normal producer includes exact page and fullscreen guards");
            }
        }

        System.out.println(
                "Free A11 7.3.1 Music page: present/absent/disabled, every valid BT/USB/KW"
                        + " position, exact pins, all-class inverse PASS; guest not started");
    }
}
