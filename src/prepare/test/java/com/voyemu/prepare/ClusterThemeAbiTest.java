package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.value.IntEncodedValue;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The OEM cluster Binder ABI of QGAPI that ClusterThemeBinder implements: the theme transactions
 * and the Parcel layouts and enum ordinals of the head-unit information transactions.
 */
public final class ClusterThemeAbiTest {
    private static final String PACKAGE = "Lcom/qinggan/cluster/";

    static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static String text(Method method) {
        var code = method.getImplementation();

        return String.join(
                "\n",
                DexEdits.render(
                        code,
                        new DexText.Frame(code.getRegisterCount(), DexEdits.parameters(method))));
    }

    /** The instructions of the first method with the name, one smali line each. */
    static String method(ClassDef type, String name) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name) && method.getImplementation() != null) {
                return text(method);
            }
        }

        throw new AssertionError("method absent: " + type.getType() + "->" + name);
    }

    /** Each group 1 of the pattern in the instructions of a method, in order. */
    static List<String> matches(String text, String pattern) {
        List<String> result = new ArrayList<>();
        Matcher matcher = Pattern.compile(pattern).matcher(text);

        while (matcher.find()) {
            result.add(matcher.group(1));
        }

        return result;
    }

    static List<String> writes(Map<String, ClassDef> classes, String type) {
        return matches(
                method(classes.get(PACKAGE + type + ";"), "writeToParcel"),
                "Landroid/os/Parcel;->(write\\w+)\\(");
    }

    static List<String> constants(Map<String, ClassDef> classes, String type) {
        return matches(
                method(classes.get(PACKAGE + "info/" + type + ";"), "<clinit>"),
                "const-string v\\d+, \"(\\w+)\"");
    }

    static List<String> repeat(String... names) {
        List<String> result = new ArrayList<>();

        for (String name : names) {
            result.add("write" + name);
        }

        return result;
    }

    /** Transactions 3..5 and 8..12, setPresentationState and the info Parcel layouts. */
    static void information(Map<String, ClassDef> classes, Map<String, Integer> transactions) {
        for (var expected :
                Map.of(
                                "TRANSACTION_syncReadyState", 3,
                                "TRANSACTION_requestPresentation", 4,
                                "TRANSACTION_notifyReadyToShow", 5,
                                "TRANSACTION_updateMediaSrcInfo", 8,
                                "TRANSACTION_updatePhoneInfo", 9,
                                "TRANSACTION_updateCallListInfo", 10,
                                "TRANSACTION_updateNaviInfo", 11,
                                "TRANSACTION_updateNaviLaneInfo", 12)
                        .entrySet()) {
            check(
                    expected.getValue().equals(transactions.get(expected.getKey())),
                    "transaction code " + expected.getKey());
        }

        boolean presentation = false;

        for (var field :
                classes.get(PACKAGE + "IInstrumentClusterManagerCallback$Stub;").getFields()) {
            presentation |=
                    field.getName().equals("TRANSACTION_setPresentationState")
                            && field.getInitialValue() instanceof IntEncodedValue value
                            && value.getValue() == 3;
        }

        check(presentation, "setPresentationState code");
        check(
                writes(classes, "info/MediaSrcInfo")
                        .equals(
                                repeat(
                                        "Parcelable",
                                        "Parcelable",
                                        "Float",
                                        "Parcelable",
                                        "String",
                                        "String",
                                        "String")),
                "MediaSrcInfo parcel");
        check(
                writes(classes, "info/PhoneInfo")
                        .equals(
                                repeat(
                                        "String",
                                        "String",
                                        "Int",
                                        "Parcelable",
                                        "String",
                                        "Int",
                                        "Int",
                                        "Int",
                                        "Int",
                                        "Int")),
                "PhoneInfo parcel");
        check(
                writes(classes, "info/NaviInfo")
                        .equals(
                                repeat(
                                        "Parcelable",
                                        "Parcelable",
                                        "Parcelable",
                                        "Int",
                                        "Int",
                                        "Int",
                                        "Int",
                                        "Int",
                                        "Int",
                                        "String",
                                        "String",
                                        "Int",
                                        "String",
                                        "Int",
                                        "Int",
                                        "String",
                                        "Int",
                                        "Int")),
                "NaviInfo parcel");
        check(writes(classes, "info/MediaSrcType").equals(repeat("Int", "Int")), "source parcel");
        check(writes(classes, "info/GuideState").equals(repeat("Int")), "guide parcel");
        check(
                constants(classes, "MediaSrcType")
                        .equals(
                                List.of(
                                        "OFF",
                                        "FM",
                                        "AM",
                                        "Disc",
                                        "TV",
                                        "Navi",
                                        "Phone",
                                        "IPod",
                                        "AUX",
                                        "USB",
                                        "SD",
                                        "DVBT",
                                        "A2DP",
                                        "Other",
                                        "CDC",
                                        "Network",
                                        "QQ",
                                        "IPDALink",
                                        "NetworkMusic",
                                        "NetworkRadio",
                                        "USB2",
                                        "USB1_DISMISS",
                                        "USB2_DISMISS",
                                        "A2DP_DISMISS",
                                        "XMLY")),
                "MediaSrcType ordinals");
        check(
                constants(classes, "MediaState").equals(List.of("CLEAN", "PLAY", "PAUSE", "STOP")),
                "MediaState ordinals");
        check(
                constants(classes, "PhoneState")
                        .equals(
                                List.of(
                                        "NOTHING",
                                        "COMING_CALL",
                                        "HOLD",
                                        "HANG_UP",
                                        "GOING_CALL",
                                        "CONNECTED")),
                "PhoneState ordinals");
        check(
                constants(classes, "GuideState")
                        .equals(
                                List.of(
                                        "START",
                                        "AUTO_STOP",
                                        "MAN_STOP",
                                        "YAW_GUIDE",
                                        "OVER_PASS",
                                        "GUIDING",
                                        "SIMULATING",
                                        "ROAM",
                                        "UNKNOWN")),
                "GuideState ordinals");
    }

    public static void main(String[] args) throws Exception {
        Map<String, ClassDef> classes = new TreeMap<>();

        for (ClassDef type :
                DexFileFactory.loadDexFile(Path.of(args[0]).toFile(), Opcodes.forApi(30))
                        .getClasses()) {
            classes.put(type.getType(), type);
        }

        Map<String, Integer> transactions = new TreeMap<>();

        for (var field :
                classes.get(PACKAGE + "IInstrumentClusterManagerService$Stub;").getFields()) {
            if (field.getName().startsWith("TRANSACTION_")
                    && field.getInitialValue() instanceof IntEncodedValue value) {
                transactions.put(field.getName(), value.getValue());
            }
        }

        for (var expected :
                Map.of(
                                "TRANSACTION_getClusterState", 7,
                                "TRANSACTION_setClusterState", 6,
                                "TRANSACTION_registerCallback", 0x13,
                                "TRANSACTION_unregisterCallback", 0x14)
                        .entrySet()) {
            check(
                    expected.getValue().equals(transactions.get(expected.getKey())),
                    "transaction code " + expected.getKey());
        }

        check(
                method(classes.get(PACKAGE + "InstrumentClusterManager;"), "getClusterState")
                        .contains("const/4 v0, -0x1"),
                "manager getClusterState default");
        ClassDef service = classes.get(PACKAGE + "IInstrumentClusterManagerService$Stub$Proxy;");
        String set = method(service, "setClusterState");
        check(
                set.indexOf("writeInterfaceToken") < set.indexOf("writeToParcel")
                        && set.indexOf("writeToParcel")
                                < set.indexOf("{v0, p2}, Landroid/os/Parcel;->writeInt")
                        && set.contains("const/4 v4, 0x6"),
                "setClusterState parcel order");
        String get = method(service, "getClusterState");
        check(
                get.contains("writeToParcel")
                        && get.contains("const/4 v4, 0x7")
                        && get.indexOf("readException") < get.indexOf("->readInt()"),
                "getClusterState parcel order");
        String register = method(service, "registerCallback");
        check(
                register.contains("writeStrongBinder") && register.contains("const/16 v3, 0x13"),
                "registerCallback transaction");
        String callback =
                method(
                        classes.get(PACKAGE + "IInstrumentClusterManagerCallback$Stub$Proxy;"),
                        "setClusterStateChanged");
        check(
                callback.indexOf("writeString") < callback.indexOf("writeToParcel")
                        && callback.indexOf("writeToParcel") < callback.indexOf("->transact")
                        && callback.contains("const/4 v3, 0x4")
                        && callback.contains("v0, v4, v1"),
                "callback parcel order");
        StringBuilder state = new StringBuilder();

        for (Method method : classes.get(PACKAGE + "ClusterState;").getMethods()) {
            if (method.getImplementation() != null) {
                state.append(text(method));
            }
        }

        check(
                state.toString().contains("->ordinal()I")
                        && state.toString().contains("->writeInt(I)V"),
                "ClusterState parcel");
        information(classes, transactions);
        System.out.println("ClusterThemeAbiTest PASS");
    }
}
