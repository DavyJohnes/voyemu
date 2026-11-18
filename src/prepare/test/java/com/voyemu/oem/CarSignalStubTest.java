package com.voyemu.oem;

import com.qinggan.carsignal.ICarSignalService;
import com.qinggan.carsignal.diagnostic.DiagnosticInfo;

import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Arrays;

/**
 * The shared qg.carsignal stub of Free A9 and Free A11 answers every method of the OEM interface as
 * a parked vehicle without MCU: zero, false, empty text and an empty vehicle configuration, the
 * clock in seconds, setters without effect and accepted callbacks that never fire.
 */
public final class CarSignalStubTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static Object argument(Class<?> type) {
        if (type == int.class) {
            return 7;
        }

        if (type == boolean.class) {
            return true;
        }

        if (type == int[].class) {
            return new int[] {1, 2};
        }

        if (type == String.class) {
            return "LVIN0000000000001";
        }

        if (type == DiagnosticInfo.class) {
            return new DiagnosticInfo();
        }

        return Proxy.newProxyInstance(
                type.getClassLoader(), new Class<?>[] {type}, (proxy, method, values) -> null);
    }

    /** Calls every interface method and checks the neutral answer for its return type. */
    private static int neutralAnswers(CarSignalStub stub) throws Exception {
        int methods = 0;

        for (Method method : ICarSignalService.class.getDeclaredMethods()) {
            String name = method.getName();
            Object[] values =
                    Arrays.stream(method.getParameterTypes())
                            .map(CarSignalStubTest::argument)
                            .toArray();
            long before = System.currentTimeMillis() / 1000;
            Object result = method.invoke(stub, values);
            long after = System.currentTimeMillis() / 1000;
            Class<?> type = method.getReturnType();
            methods++;

            if (name.equals("getMCUHardwareVersion")) {
                continue;
            }

            if (name.equals("getRTCTime")) {
                check((Long) result >= before && (Long) result <= after, "clock in seconds");
            } else if (type == void.class) {
                check(result == null, name);
            } else if (type == int.class) {
                check((Integer) result == 0, name + " answers 0");
            } else if (type == float.class) {
                check((Float) result == 0.0f, name + " answers 0");
            } else if (type == boolean.class) {
                check(
                        (Boolean) result
                                == (name.equals("registerCallback")
                                        || name.equals("unregisterCallback")),
                        name + " answer");
            } else if (type == String.class) {
                check("".equals(result), name + " answers empty text");
            } else {
                check(((int[]) result).length == 0, name + " answers an empty configuration");
            }
        }

        return methods;
    }

    public static void main(String[] args) throws Exception {
        CarSignalStub stub = new CarSignalStub();

        check(neutralAnswers(stub) == 76, "all 76 OEM methods answered");
        android.os.Build.VERSION.SDK_INT = 28;
        check("V3.0".equals(stub.getMCUHardwareVersion()), "Free A9 MCU hardware version");
        android.os.Build.VERSION.SDK_INT = 30;
        check("V1.0".equals(stub.getMCUHardwareVersion()), "Free A11 MCU hardware version");
        System.out.println("76 qg.carsignal methods answer a parked vehicle without MCU");
    }
}
