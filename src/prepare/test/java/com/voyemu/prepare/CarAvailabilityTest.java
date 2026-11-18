package com.voyemu.prepare;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import javax.tools.ToolProvider;

/** Recording Binder fixtures exercise the actual own component; this is not guest acceptance. */
public final class CarAvailabilityTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("ROOT");
        }

        Path root = Path.of(args[0]).toRealPath();
        Path component =
                root.resolve(
                        "profiles/passion-11-russian-20260403-2.1.4.80/components/car-availability");
        checkDeclaredContract(root, component);
        Path scratch =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "car-availability-test-");

        try {
            Map<String, String> fixtures =
                    Map.of(
                            "android/os/RemoteException.java",
                                    "package android.os; public class RemoteException extends"
                                            + " Exception {}",
                            "android/os/IBinder.java",
                                    "package android.os; public interface IBinder {int"
                                        + " FLAG_ONEWAY=1,INTERFACE_TRANSACTION=1598968902; boolean"
                                        + " transact(int c,Parcel d,Parcel r,int f)throws"
                                        + " RemoteException;}",
                            "android/os/Binder.java",
                                    "package android.os; public class Binder implements IBinder"
                                        + " {public void attachInterface(Object owner,String"
                                        + " descriptor){}protected boolean onTransact(int c,Parcel"
                                        + " d,Parcel r,int f)throws RemoteException{return false;}"
                                        + " public boolean transact(int c,Parcel d,Parcel r,int"
                                        + " f)throws RemoteException{return onTransact(c,d,r,f);}}",
                            "android/os/Parcel.java",
                                    "package android.os; public class Parcel {public String"
                                        + " token,name,text;public int trailing;public boolean"
                                        + " noException;public IBinder value;public void"
                                        + " enforceInterface(String"
                                        + " expected){if(!expected.equals(token))throw new"
                                        + " SecurityException(\"token\");}public String"
                                        + " readString(){return name;}public int dataAvail(){return"
                                        + " trailing;}public void writeString(String"
                                        + " s){text=s;}public void"
                                        + " writeNoException(){noException=true;}public void"
                                        + " writeStrongBinder(IBinder b){value=b;}}",
                            "android/os/ServiceManager.java",
                                    "package android.os; public class ServiceManager {public static"
                                        + " String name;public static IBinder value;public static"
                                        + " boolean fail;public static void addService(String"
                                        + " n,IBinder b){if(fail)throw new"
                                        + " SecurityException(\"denied\");name=n;value=b;}}",
                            "android/content/Intent.java",
                                    "package android.content; public class Intent {private final"
                                        + " String action;public Intent(String s){action=s;}public"
                                        + " String getAction(){return action;}}",
                            "android/app/Service.java",
                                    "package android.app; import android.os.IBinder;import"
                                        + " android.content.Intent;public abstract class Service"
                                        + " {public static final int START_NOT_STICKY=2;public void"
                                        + " onCreate(){}public abstract IBinder onBind(Intent"
                                        + " i);public int onStartCommand(Intent i,int f,int"
                                        + " s){return 0;}}");
            List<String> compiler =
                    new ArrayList<>(List.of("-d", scratch.resolve("classes").toString()));

            for (var entry : fixtures.entrySet()) {
                Path file = scratch.resolve(entry.getKey());
                Files.createDirectories(file.getParent());
                Files.writeString(file, entry.getValue());
                compiler.add(file.toString());
            }

            compiler.add(
                    component
                            .resolve("src/com/voyemu/oem/car/CarAvailabilityService.java")
                            .toString());
            check(
                    ToolProvider.getSystemJavaCompiler()
                                    .run(null, null, null, compiler.toArray(String[]::new))
                            == 0,
                    "actual own availability source compiles");

            try (var loader =
                    new URLClassLoader(
                            new java.net.URL[] {scratch.resolve("classes").toUri().toURL()},
                            null)) {
                checkServiceBinding(loader);
            }
        } finally {
            FilesSupport.removeOwnedTree(scratch);
        }

        System.out.println(
                "Own car availability Binder/token/lifecycle contract passed; guest_started=false");
    }

    private static void checkDeclaredContract(Path root, Path component) throws Exception {
        var config = Json.object(Json.parse(Files.readString(component.resolve("component.json"))));
        var contract = Json.obj(config, "contract");

        check(
                Json.integer(config, "minSdk") == 30
                        && Json.integer(config, "targetSdk") == 30
                        && Json.str(contract, "sha256")
                                .equals(
                                        "87adcf736e346261e6caabbd6aead28f9bd177fb6fc7cec1d8aa8b55f8a0ce15")
                        && Json.integer(contract, "getCarServiceTransaction") == 12,
                "exact API30 owner contract required");
        String manifest = Files.readString(component.resolve("AndroidManifest.xml"));
        check(
                manifest.contains("package=\"com.android.car\"")
                        && manifest.contains("com.voyemu.oem.car.CarAvailabilityService")
                        && manifest.contains("android.car.ICar")
                        && manifest.contains("android:directBootAware=\"true\""),
                "explicit compatibility binding and own namespace required");
        var profile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/passion-11-russian-20260403-2.1.4.80/profile.json"))));
        check(
                Json.str(profile, "status").equals("ready")
                        && "components/car-availability"
                                .equals(Json.obj(profile, "components").get("car-availability")),
                "Exact ready Passion availability component binding required");
        var installation =
                Map.of(
                        "artifact",
                        "component:car-availability",
                        "type",
                        "file",
                        "target",
                        "/system/priv-app/VoyemuCarAvailability/VoyemuCarAvailability.apk",
                        "mode",
                        "0644");
        var availability =
                Json.list(profile.get("install")).stream()
                        .map(Json::object)
                        .filter(item -> "component:car-availability".equals(item.get("artifact")))
                        .toList();
        check(
                availability.equals(List.of(installation)),
                "One exact privileged availability installation required");
        check(
                Json.str(
                                Json.obj(
                                        Json.obj(profile, "original"),
                                        Json.str(contract, "original")),
                                "sha256")
                        .equals(Json.str(contract, "sha256")),
                "component protocol contract matches the exact declared owner input");
    }

    private static void checkServiceBinding(ClassLoader loader) throws Exception {
        Class<?> service = loader.loadClass("com.voyemu.oem.car.CarAvailabilityService"),
                intent = loader.loadClass("android.content.Intent"),
                parcel = loader.loadClass("android.os.Parcel"),
                binder = loader.loadClass("android.os.IBinder"),
                manager = loader.loadClass("android.os.ServiceManager");
        Object live = service.getConstructor().newInstance();
        var bind = service.getMethod("onBind", intent);

        check(
                bind.invoke(live, new Object[] {null}) == null
                        && bind.invoke(
                                        live,
                                        intent.getConstructor(String.class).newInstance("other"))
                                == null,
                "unrelated/null binding rejected");
        Object transport =
                bind.invoke(
                        live, intent.getConstructor(String.class).newInstance("android.car.ICar"));
        check(transport != null, "exact binding supported");
        service.getMethod("onCreate").invoke(live);
        // The registered global Binder must be the bound object itself, not an equal copy.
        @SuppressWarnings("ReferenceEquality")
        boolean sameBinder = manager.getField("value").get(null) == transport;
        check(
                manager.getField("name").get(null).equals("car_service") && sameBinder,
                "global/bound Binder identity agrees");
        var transact = binder.getMethod("transact", int.class, parcel, parcel, int.class);
        Object data = parcel.getConstructor().newInstance(),
                reply = parcel.getConstructor().newInstance();
        parcel.getField("token").set(data, "android.car.ICar");
        parcel.getField("name").set(data, "power");
        check(
                (Boolean) transact.invoke(transport, 12, data, reply, 0)
                        && parcel.getField("noException").getBoolean(reply)
                        && parcel.getField("value").get(reply) == null,
                "power returns unsupported null with proper reply framing");
        check(
                !(Boolean)
                        transact.invoke(
                                transport, 999, data, parcel.getConstructor().newInstance(), 0),
                "unknown transaction rejected");
        check(
                !(Boolean) transact.invoke(transport, 12, data, reply, 1)
                        && !(Boolean) transact.invoke(transport, 12, data, null, 0),
                "oneway/missing reply rejected");
        Object descriptor = parcel.getConstructor().newInstance();
        check(
                (Boolean) transact.invoke(transport, 1598968902, data, descriptor, 0)
                        && parcel.getField("text").get(descriptor).equals("android.car.ICar"),
                "interface descriptor exact");

        for (boolean wrongToken : new boolean[] {true, false}) {
            Object bad = parcel.getConstructor().newInstance();
            parcel.getField("token").set(bad, wrongToken ? "other" : "android.car.ICar");
            parcel.getField("trailing").setInt(bad, wrongToken ? 0 : 4);
            boolean rejected = false;

            try {
                transact.invoke(transport, 12, bad, parcel.getConstructor().newInstance(), 0);
            } catch (java.lang.reflect.InvocationTargetException e) {
                rejected = e.getCause() instanceof SecurityException;
            }

            check(rejected, "wrong token/trailing data rejected");
        }

        check(
                ((Integer)
                                service.getMethod("onStartCommand", intent, int.class, int.class)
                                        .invoke(live, null, 0, 1))
                        == 2,
                "service never requests automatic sticky restart");
        manager.getField("fail").setBoolean(null, true);
        boolean denied = false;

        try {
            service.getMethod("onCreate").invoke(service.getConstructor().newInstance());
        } catch (java.lang.reflect.InvocationTargetException e) {
            denied = e.getCause() instanceof IllegalStateException;
        }

        check(denied, "publication failure cannot masquerade as availability");
    }
}
