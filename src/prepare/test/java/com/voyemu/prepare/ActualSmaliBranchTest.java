package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

/** Bounded execution of OEM manager smali with fixtures for external calls. */
public final class ActualSmaliBranchTest {
    private static final String PHONE = "Lcom/qinggan/bluetoothphone/";
    private static final String MANAGER = PHONE + "logic/manager/DialStatusChangeManager;";
    private static final String UPDATE = "updateCallState";
    private static final int INSTRUCTION_LIMIT = 300;
    private static final Pattern INVOKE =
            Pattern.compile("\\{([^}]*)\\}, (L[^;]+;)->([^ (]+)\\(.*");
    private static final Pattern FIELD = Pattern.compile("L[^;]+;->([^:]+):.+");
    private static final String USAGE = "usage: ActualSmaliBranchTest {old,new} source";

    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    private static <T> T read(Map<String, T> map, String key) {
        check(map.containsKey(key), "missing interpreter value " + key);

        return map.get(key);
    }

    private static Object numberForBoolean(Object value) {
        return value instanceof Boolean flag ? (flag ? 1L : 0L) : value;
    }

    private static boolean truthy(Object value) {
        Object normalized = numberForBoolean(value);

        if (normalized instanceof Number number) {
            return number.longValue() != 0;
        }

        if (normalized instanceof CharSequence string) {
            return !string.isEmpty();
        }

        return normalized != null;
    }

    private static String fieldName(String reference) {
        var field = FIELD.matcher(reference);

        check(field.matches(), "unrecognized field " + reference);

        return field.group(1);
    }

    private record Fixture(Map<String, Object> fields) {
        Fixture {
            fields = new HashMap<>(fields);
        }

        Object get(String name) {
            return read(fields, name);
        }

        void set(String name, Object value) {
            fields.put(name, value);
        }
    }

    private record Options(
            boolean foreground, boolean weChat, boolean manual, boolean full, boolean shown) {
        private static final Options DEFAULT = new Options(false, false, false, true, false);
    }

    private static final class Execution {
        private final String text;
        private final Options options;
        private final Fixture manager;
        private final List<String> trace = new ArrayList<>();
        private int adds;

        Execution(String text, Options options) {
            this.text = text;
            this.options = options;
            manager =
                    new Fixture(
                            Map.of(
                                    "isManual", options.manual(),
                                    "isFullScreen", options.full(),
                                    "fullWindow",
                                            new Fixture(
                                                    Map.of(
                                                            "kind",
                                                            "full",
                                                            "shown",
                                                            options.shown())),
                                    "smallWindow", new Fixture(Map.of("kind", "small")),
                                    "mHandler", new Fixture(Map.of("kind", "handler"))));
        }

        private List<String> body(String name) {
            var method =
                    Pattern.compile(
                                    "(?ms)^\\.method[^\\n]* "
                                            + Pattern.quote(name)
                                            + "\\([^\\n]*\\n(.*?)^\\.end method")
                            .matcher(text);
            String message = "exact original method missing/ambiguous " + name;

            check(method.find(), message);
            String body = method.group(1);
            check(!method.find(), message);

            return body.lines()
                    .map(String::strip)
                    .filter(line ->
                            !line.isEmpty() && !line.startsWith(".") && !line.startsWith("#"))
                    .toList();
        }

        void run(String name, Object argument) {
            List<String> code = body(name);
            Map<String, Integer> labels = new HashMap<>();

            for (int i = 0; i < code.size(); i++) {
                if (code.get(i).startsWith(":")) {
                    labels.put(code.get(i), i);
                }
            }

            Map<String, Object> registers = new HashMap<>();
            registers.put("p0", manager);
            registers.put("p1", argument);
            Object last = null;
            int pc = 0;
            int steps = 0;

            while (pc < code.size()) {
                String instruction = code.get(pc++);
                check(++steps <= INSTRUCTION_LIMIT, "instruction bound");

                if (instruction.startsWith(":")) {
                    continue;
                }

                String[] parts = instruction.split("\\s+", 2);
                String op = parts[0];
                String tail = parts.length == 2 ? parts[1] : "";
                String[] operands = tail.split(", ", 3);

                switch (op) {
                    case "return-void" -> {
                        return;
                    }
                    case "if-eqz", "if-nez" -> {
                        boolean nonZero = truthy(read(registers, operands[0]));

                        if (nonZero == op.equals("if-nez")) {
                            pc = read(labels, operands[1]);
                        }
                    }
                    case "if-eq", "if-ne" -> {
                        Object left = numberForBoolean(read(registers, operands[0]));
                        Object right = numberForBoolean(read(registers, operands[1]));

                        if (Objects.equals(left, right) == op.equals("if-eq")) {
                            pc = read(labels, operands[2]);
                        }
                    }
                    case "sget-object" -> registers.put(operands[0], "TAG");
                    case "new-instance" -> {
                        // Interpreted append calls mutate this builder through the register file.
                        @SuppressWarnings("UnnecessaryStringBuilder")
                        StringBuilder builder = new StringBuilder();
                        registers.put(operands[0], builder);
                    }
                    case "move-result", "move-result-object" -> registers.put(tail, last);
                    case "move" -> registers.put(operands[0], read(registers, operands[1]));
                    case "check-cast" -> {}
                    default -> {
                        if (op.startsWith("goto")) {
                            pc = read(labels, tail);
                        } else if (op.startsWith("const-string")) {
                            registers.put(operands[0], Json.parse(tail.split(", ", 2)[1]));
                        } else if (op.startsWith("const")) {
                            registers.put(operands[0], Long.decode(operands[1]));
                        } else if (op.startsWith("iget")) {
                            Fixture object = (Fixture) read(registers, operands[1]);
                            registers.put(operands[0], object.get(fieldName(operands[2])));
                        } else if (op.startsWith("iput")) {
                            Fixture object = (Fixture) read(registers, operands[1]);
                            object.set(fieldName(operands[2]), read(registers, operands[0]));
                        } else if (op.startsWith("invoke-")) {
                            var invoke = INVOKE.matcher(tail);
                            check(invoke.matches(), "unrecognized invocation " + instruction);
                            List<Object> arguments = new ArrayList<>();

                            for (String register : invoke.group(1).split(", ", -1)) {
                                if (!register.isEmpty()) {
                                    arguments.add(read(registers, register));
                                }
                            }

                            last = call(invoke.group(2), invoke.group(3), arguments);
                        } else {
                            throw new AssertionError(
                                    "unsupported original instruction " + instruction);
                        }
                    }
                }
            }
        }

        private Object call(String owner, String method, List<Object> args) {
            if (owner.equals(MANAGER)) {
                trace.add(method);

                if (!method.equals("initUiControl") && !method.equals("uninitUiControl")) {
                    run(method, args.get(1));
                }

                return null;
            }

            if (owner.equals("Ljava/lang/StringBuilder;")) {
                switch (method) {
                    case "<init>" -> {
                        return null;
                    }
                    case "append" -> {
                        return ((StringBuilder) args.get(0)).append(args.get(1));
                    }
                    case "toString" -> {
                        return args.get(0).toString();
                    }
                    default -> {}
                }
            }

            if (owner.equals("Landroid/util/Log;")) {
                return 0L;
            }

            if (owner.endsWith("HfpEvents$CallStateEvent;")) {
                String field =
                        switch (method) {
                            case "getState" -> "state";
                            case "getId" -> "id";
                            case "getNumber" -> "number";
                            default ->
                                    throw new AssertionError(
                                            "unsupported call event boundary " + method);
                        };

                return ((Fixture) args.get(0)).get(field);
            }

            return callExternal(owner, method, args);
        }

        private Object callExternal(String owner, String method, List<Object> args) {
            if (owner.endsWith("/AppMain;")) {
                if (method.equals("getInstance")) {
                    return new Fixture(Map.of("kind", "app"));
                }

                if (method.equals("sendQGBusEvent")) {
                    trace.add("bus:" + args.get(1));

                    return null;
                }
            }

            if (owner.endsWith("/util/Util;") && method.equals("isAppOnForeground")) {
                return options.foreground();
            }

            if (owner.endsWith("/HeadSetProfileManager;") && method.equals("isWeChat")) {
                return options.weChat();
            }

            if (owner.equals(PHONE + "DialMaxWindow;") || owner.equals(PHONE + "DialMinWindow;")) {
                return callWindow(method, args);
            }

            if (owner.equals("Landroid/os/Handler;") && method.equals("sendEmptyMessageDelayed")) {
                trace.add("handler:delayed");

                return true;
            }

            throw new AssertionError("unsupported external boundary " + owner + "->" + method);
        }

        private Object callWindow(String method, List<Object> args) {
            if (method.equals("getDisplayId")) {
                return 0L;
            }

            Fixture window = (Fixture) args.get(0);
            trace.add(window.get("kind") + ":" + method);

            switch (method) {
                case "show" -> {
                    if (!truthy(window.fields().get("shown"))) {
                        window.set("shown", true);
                        adds++;
                    }
                }
                case "close" -> window.set("shown", false);
                case "updateView", "resetStatus" -> {}
                default -> throw new AssertionError("unsupported view boundary " + method);
            }

            return null;
        }
    }

    private static Fixture callEvent(long state) {
        return new Fixture(Map.of("state", state, "id", 1L, "number", "+70000000001"));
    }

    private static void checkOld(String text) {
        for (int state : new int[] {129, 130}) {
            Execution execution = new Execution(text, Options.DEFAULT);
            execution.run(UPDATE, callEvent(state));
            String trace =
                    execution.trace.stream()
                            .map(Json::write)
                            .collect(Collectors.joining(", ", "[", "]"));
            System.out.printf(
                    Locale.ROOT,
                    "{\"state\": %d, \"isFullScreen\": %s, \"full_adds\": %d, \"trace\": %s}%n",
                    state,
                    truthy(execution.manager.get("isFullScreen")),
                    execution.adds,
                    trace);
            check(
                    execution.adds == 1,
                    "actual original automatic full-screen branch only sets flag; existing"
                            + " DialMaxWindow.show is never invoked");
        }
    }

    private static void checkNew(String text) {
        for (int state : new int[] {128, 129, 130}) {
            Execution execution = new Execution(text, Options.DEFAULT);
            Fixture event = callEvent(state);
            execution.run(UPDATE, event);
            check(
                    execution.adds == 1
                            && execution.trace.contains("openMaxWindow")
                            && execution.trace.contains("full:show"),
                    "automatic full-window dispatch for state " + state);
            execution.run(UPDATE, event);
            check(execution.adds == 1, "modeled renderer isShow boundary must avoid a second add");
        }

        for (Options options :
                List.of(
                        new Options(true, false, false, true, false),
                        new Options(false, true, false, true, false),
                        new Options(false, false, true, false, false),
                        new Options(false, false, false, false, false))) {
            Execution execution = new Execution(text, options);
            execution.run(UPDATE, callEvent(130));
            check(
                    execution.adds == 0 && !execution.trace.contains("openMaxWindow"),
                    "full-window gate " + options);
        }

        Execution manual = new Execution(text, new Options(false, false, true, true, true));
        manual.run(UPDATE, callEvent(130));
        check(
                manual.adds == 0 && truthy(manual.manager.get("isManual")),
                "existing manual full window");

        Execution terminated = new Execution(text, Options.DEFAULT);
        terminated.run(UPDATE, callEvent(131));
        check(
                terminated.adds == 0 && terminated.trace.contains("uninitUiControl"),
                "terminated call");

        Execution missing = new Execution(text, Options.DEFAULT);
        missing.run(UPDATE, null);
        check(missing.trace.isEmpty(), "missing call event");
        System.out.println(
                "PASS actual manager smali dispatch: incoming/outgoing/active;"
                    + " foreground/WeChat/manual/minimized/terminated gates; reaches OEM show call"
                    + " with modeled renderer idempotence (no WindowManager execution)");
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && (args[0].equals("-h") || args[0].equals("--help"))) {
            System.out.println(USAGE);

            return;
        }

        if (args.length != 2 || !(args[0].equals("old") || args[0].equals("new"))) {
            System.err.println(USAGE);
            System.exit(2);

            return;
        }

        byte[] bytes = Files.readAllBytes(Path.of(args[1]));
        String text = StandardCharsets.UTF_8.newDecoder().decode(ByteBuffer.wrap(bytes)).toString();
        System.out.println(
                "input_sha256="
                        + HexFormat.of()
                                .formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)));

        if (args[0].equals("old")) {
            checkOld(text);
        } else {
            checkNew(text);
        }
    }
}
