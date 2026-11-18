package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction31c;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableExceptionHandler;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.debug.ImmutableLineNumber;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.util.List;
import java.util.Set;

public final class HiboardNormalizationTest {
    static Method fixture(
            int branchOffset, int handlerAddress, int lineAddress, int lineNumber, boolean jumbo) {
        var code =
                List.of(
                        new ImmutableInstruction21c(
                                Opcode.CONST_STRING, 0, new ImmutableStringReference("fixture")),
                        new ImmutableInstruction21t(Opcode.IF_EQZ, 0, branchOffset),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID));
        var blocks =
                List.of(
                        new ImmutableTryBlock(
                                0,
                                2,
                                List.of(
                                        new ImmutableExceptionHandler(
                                                "Ljava/lang/Exception;", handlerAddress))));
        var debug = List.of(new ImmutableLineNumber(lineAddress, lineNumber));
        var source = new ImmutableMethodImplementation(1, code, blocks, debug);

        if (!jumbo) {
            return method(source);
        }

        var widened = new MutableMethodImplementation(source);
        widened.replaceInstruction(
                0,
                new BuilderInstruction31c(
                        Opcode.CONST_STRING_JUMBO, 0, new ImmutableStringReference("fixture")));

        return method(widened);
    }

    static Method method(org.jf.dexlib2.iface.MethodImplementation body) {
        return new ImmutableMethod(
                "Lfixture/Normalize;", "run", List.of(), "V", 9, Set.of(), Set.of(), body);
    }

    static String canonical(Method m) throws Exception {
        return FrameworkDexMaterializer.semantic(HiboardRouting.normalized(m));
    }

    static void different(String baseline, Method altered, String reason) throws Exception {
        if (baseline.equals(canonical(altered))) {
            throw new AssertionError(reason + " was ignored");
        }
    }

    public static void main(String[] args) throws Exception {
        String baseline = canonical(fixture(3, 5, 0, 17, false));

        if (!baseline.equals(canonical(fixture(3, 5, 0, 17, true)))) {
            throw new AssertionError("equivalent const-string/jumbo differ");
        }

        different(baseline, fixture(2, 5, 0, 17, false), "branch target");
        different(baseline, fixture(3, 4, 0, 17, false), "handler address");
        different(baseline, fixture(3, 5, 0, 18, false), "debug line value");
        different(baseline, fixture(3, 5, 4, 17, false), "debug address");
        System.out.println(
                "const-string/jumbo equivalent; branch, handler and debug value/address drift"
                        + " rejected");
    }
}
