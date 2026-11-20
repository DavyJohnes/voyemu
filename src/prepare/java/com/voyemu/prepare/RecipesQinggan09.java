package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** API28 recipe handlers for the free A9 profiles. */
final class RecipesQinggan09 {
    static void shutdownRouter(
            String name,
            Map<String, Object> metadata,
            Map<String, Object> constants,
            Recipes.Target target)
            throws Exception {
        require(
                Json.str(constants, "INPUT_SHA256")
                                .equals(
                                        "602ca641728bbac947147ad8d7e41bbd481c7847a6bb838bf1c5dcbd37a11d8d")
                        && FilesSupport.hash(Recipes.output(target))
                                .equals(Json.str(constants, "INPUT_SHA256")),
                "Exact Android 28 ranchu init source required");
        Recipes.literalCalls(name, metadata, target);
    }

    static void phoneProvider(Recipes.Target target) throws Exception {
        require(
                target.originalSha256()
                        .equals("d5a7417be79e2e4bfd70e643df85ae76e1a8a15332a345f126ccc5781f5ba8a3"),
                "Exact A9 Phone APK required");
        target.addPhoneProvider();
    }

    static void qgimeNatives(Recipes.Target target) throws Exception {
        target.removeEntry("lib/armeabi-v7a/libjni_qgHandWrite.so");
        target.removeEntry("lib/armeabi-v7a/libjni_qgime.so");
        target.putEntry(
                Recipes.use(target, "component:qgime-handwrite"),
                "lib/" + target.abi() + "/libjni_qgHandWrite.so");
        target.putEntry(
                Recipes.use(target, "component:qgime-pinyin"),
                "lib/" + target.abi() + "/libjni_qgime.so");
    }

    static void launcherRouting(Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        target.addDex("src");

        for (Object item : Json.list(constants.get("ROUTING"))) {
            Recipes.method(target, DexEdits.MethodEdit.parse(Json.list(item)));
        }

        String bar = "Lcom/qinggan/mainlauncher/hiboard/HiboardBar;";
        DexClasses dex = target.dex();
        ClassDef type = DexEdits.addInterface(dex.get(bar), "Ljava/lang/Runnable;");
        type =
                DexEdits.field(
                        type,
                        "voyemuReleased",
                        "Z",
                        AccessFlags.PRIVATE.getValue() | AccessFlags.VOLATILE.getValue(),
                        true);
        type =
                DexEdits.field(
                        type,
                        "voyemuWirelessInitialized",
                        "Z",
                        AccessFlags.PRIVATE.getValue(),
                        true);
        type =
                DexEdits.field(
                        type,
                        "mCanBusManagerReady",
                        "Z",
                        AccessFlags.PRIVATE.getValue() | AccessFlags.VOLATILE.getValue(),
                        false);
        type =
                DexEdits.addMethod(
                        type,
                        "run()V",
                        AccessFlags.PUBLIC.getValue(),
                        2,
                        List.of(
                                "iget-boolean v0, p0, " + bar + "->voyemuReleased:Z",
                                "if-nez v0, :voyemu_wireless_done",
                                "iget-boolean v0, p0, " + bar + "->mCanBusManagerReady:Z",
                                "if-eqz v0, :voyemu_wireless_done",
                                "iget-boolean v0, p0, " + bar + "->voyemuWirelessInitialized:Z",
                                "if-nez v0, :voyemu_wireless_done",
                                "const/4 v0, 0x1",
                                "iput-boolean v0, p0, " + bar + "->voyemuWirelessInitialized:Z",
                                "invoke-direct {p0}, " + bar + "->setupWirelessCharging()V",
                                ":voyemu_wireless_done",
                                "return-void"));
        dex.replace(type);
        Recipes.method(
                target,
                bar + "->setupViews()V",
                DexEdits.Edit.replace(
                        List.of("invoke-direct {p0}, " + bar + "->setupWirelessCharging()V"),
                        List.of("nop")));
        Recipes.method(
                target,
                bar + "->release()V",
                DexEdits.Edit.before(
                        List.of("sget-object v0, " + bar + "->TAG:Ljava/lang/String;"),
                        List.of(
                                "const/4 v0, 0x1",
                                "iput-boolean v0, p0, " + bar + "->voyemuReleased:Z")));
        String listener = "Lcom/qinggan/mainlauncher/hiboard/HiboardBar$5;";
        Recipes.method(
                target,
                listener + "->onConnectStatusChange(Z)V",
                DexEdits.Edit.before(
                        List.of(
                                "iget-object p1, p0, " + listener + "->this$0:" + bar,
                                "invoke-static {p1}, "
                                        + bar
                                        + "->access$3300("
                                        + bar
                                        + ")Lcom/qinggan/canbus/CanBusListener;"),
                        List.of(
                                "iget-object v0, p0, " + listener + "->this$0:" + bar,
                                "invoke-static {v0}, "
                                        + bar
                                        + "->access$1300("
                                        + bar
                                        + ")Lcom/qinggan/launcher/base/thread/WorkerThread;",
                                "move-result-object v1",
                                "invoke-virtual {v1, v0},"
                                        + " Lcom/qinggan/launcher/base/thread/WorkerThread;"
                                        + "->postOnUiThread(Ljava/lang/Runnable;)V")));
    }

    static void mediaCardMusic(Recipes.Target target) throws Exception {
        String utils = "Lcom/qinggan/media/helper/app/MediaJumpUtils;";
        String wecar = utils + "->toWeCarFlowPage(Ljava/lang/String;Z)V",
                music = utils + "->toMusicPage(Ljava/lang/String;Z)V";

        Recipes.method(
                target,
                "Lcom/pateo/voyah/mediaCard/home/view/HomeBaseView;->toMediaHomePage()V",
                DexEdits.Edit.replace(
                        List.of("invoke-static {v1, v0}, " + wecar),
                        List.of("invoke-static {v1, v0}, " + music)));
        Recipes.method(
                target,
                "Lcom/qinggan/launcher/base/utils/AppLauncher;->openMusicPage(Ljava/lang/String;Z)V",
                DexEdits.Edit.replace(
                        List.of(
                                "invoke-static {p0, p1}, " + wecar,
                                "goto :goto_79",
                                "sget-object v1, Lcom/qinggan/media/helper/MediaEnum;"
                                        + "->USB_MUSIC:Lcom/qinggan/media/helper/MediaEnum;"),
                        List.of(
                                "invoke-static {p0, p1}, " + music,
                                "goto :goto_79",
                                "sget-object v1, Lcom/qinggan/media/helper/MediaEnum;"
                                        + "->USB_MUSIC:Lcom/qinggan/media/helper/MediaEnum;")),
                DexEdits.Edit.replace(
                        List.of(
                                "invoke-static {p0, p1}, " + wecar,
                                "goto :goto_79",
                                "invoke-static {p0, p1}, " + music),
                        List.of(
                                "invoke-static {p0, p1}, " + music,
                                "goto :goto_79",
                                "invoke-static {p0, p1}, " + music)));
        Recipes.method(
                target,
                utils
                        + "->toMediaHomePage(Landroid/content/Context;"
                        + "Lcom/qinggan/media/helper/MediaEnum;Z)V",
                DexEdits.Edit.replace(
                        List.of("invoke-static {v3, p2}, " + wecar),
                        List.of("invoke-static {v3, p2}, " + music)));
    }

    static void usercenterNetwork(Recipes.Target target) throws Exception {
        target.addDex("src");
        Recipes.network(target, "util", 1);
        Recipes.network(target, "tsp/util", 0);
        String add =
                "(Landroid/content/IntentFilter;I[Landroid/content/ComponentName;"
                        + "Landroid/content/ComponentName;)V";
        Recipes.method(
                target,
                "Lcom/qinggan/app/common/utils/AppUtils;->changeLauncherToDefault(Landroid/content/Context;)V",
                DexEdits.Edit.replace(
                        List.of(
                                "invoke-virtual {v0, v1, v4, v3, v2},"
                                    + " Landroid/content/pm/PackageManager;->addPreferredActivity"
                                        + add),
                        List.of(
                                "invoke-static {v0, v1, v4, v3, v2},"
                                        + " Lcom/voyemu/oem/HomePreference;->add("
                                        + "Landroid/content/pm/PackageManager;"
                                        + add.substring(1))));
    }

    static void selinuxHash(Recipes.Target target) throws Exception {
        boolean early = target.uses().containsKey("patched:can-early-policy-a9"),
                simulator = target.uses().containsKey("patched:can-simulator-policy-a9");

        if (early) {
            require(
                    target.uses().keySet().equals(Set.of("patched:can-early-policy-a9")),
                    "Exact A9 early policy dependency required");
        }

        if (simulator) {
            require(
                    target.uses().keySet().equals(Set.of("patched:can-simulator-policy-a9")),
                    "Exact A9 simulator policy dependency required");
        }

        Files.writeString(
                Recipes.output(target),
                FilesSupport.hash(
                                Recipes.use(
                                        target,
                                        early
                                                ? "patched:can-early-policy-a9"
                                                : simulator
                                                        ? "patched:can-simulator-policy-a9"
                                                        : "patched:selinux-canbus-a9"))
                        + "\n");
    }

    static void selinuxCanbus(Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        Path file = Recipes.output(target);
        String text = Files.readString(file);
        String marker = Json.str(constants, "MARKER");

        require(!text.contains(marker), "Canbus policy already applied");
        Files.writeString(
                file,
                Recipes.newline(text)
                        + "\n"
                        + String.join(
                                "\n",
                                List.of(
                                        marker,
                                        "(type voyemu_canbus_service)",
                                        "(type voyemu_oem_service)",
                                        "(typeattributeset service_manager_type"
                                                + " (voyemu_canbus_service"
                                                + " voyemu_oem_service))",
                                        "(allow system_app voyemu_canbus_service"
                                                + " (service_manager (add find)))",
                                        "(allow system_app voyemu_oem_service"
                                                + " (service_manager (add find)))",
                                        "(allow platform_app voyemu_canbus_service"
                                                + " (service_manager (find)))",
                                        "(allow platform_app voyemu_oem_service"
                                                + " (service_manager (find)))",
                                        "(allow priv_app voyemu_canbus_service"
                                                + " (service_manager (find)))",
                                        "")));
    }

    static void canEarlyPolicy(
            Map<String, Object> metadata, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.uses()
                                .keySet()
                                .equals(Set.of("original:PlatSepolicy", "original:VendorSepolicy"))
                        && constants.equals(Map.of("KIND", "can-early-policy"))
                        && Json.list(metadata.get("calls")).isEmpty(),
                "Exact A9 early policy recipe required");
        Path file = Recipes.output(target);
        String input = Files.readString(file);
        require(
                NativeCore.hash(input.getBytes(StandardCharsets.UTF_8))
                        .equals("ea017c277007b5ba8401ab214b2d607d72ddd39b7a303db05eb5281cb405fd22"),
                "Exact simulator policy required");
        String result =
                input
                        + "; candidate early input domain: compile and runtime validation"
                        + " required\n"
                        + "(type can_early_inputs)\n"
                        + "(type can_early_inputs_exec)\n"
                        + "(type can_early_input_file)\n"
                        + "(type can_early_data_file)\n"
                        + "(type can_early_token_dir)\n"
                        + "(type vdh_device)\n"
                        + "(type can_early_marker_file)\n"
                        + "(type can_early_ready_prop)\n"
                        + "(roletype object_r can_early_inputs)\n"
                        + "(roletype object_r can_early_inputs_exec)\n"
                        + "(roletype object_r can_early_input_file)\n"
                        + "(roletype object_r can_early_data_file)\n"
                        + "(roletype object_r can_early_token_dir)\n"
                        + "(roletype object_r vdh_device)\n"
                        + "(roletype object_r can_early_marker_file)\n"
                        + "(roletype object_r can_early_ready_prop)\n"
                        + "(typeattributeset domain (can_early_inputs))\n"
                        + "(typeattributeset coredomain (can_early_inputs))\n"
                        + "(typeattributeset file_type (can_early_inputs_exec"
                        + " can_early_input_file can_early_data_file can_early_marker_file"
                        + " can_early_token_dir))\n"
                        + "(typeattributeset data_file_type (can_early_data_file))\n"
                        + "(typeattributeset core_data_file_type (can_early_data_file))\n"
                        + "(typeattributeset exec_type (can_early_inputs_exec))\n"
                        + "(typeattributeset dev_type (vdh_device))\n"
                        + "(typeattributeset property_type (can_early_ready_prop))\n"
                        + "(typeattributeset extended_core_property_type"
                        + " (can_early_ready_prop))\n"
                        + "(allow init can_early_inputs_exec (file (read getattr map"
                        + " execute open)))\n"
                        + "(allow init can_early_inputs (process (transition siginh"
                        + " rlimitinh)))\n"
                        + "(typetransition init can_early_inputs_exec process"
                        + " can_early_inputs)\n"
                        + "(allow can_early_inputs can_early_inputs_exec (file (read"
                        + " getattr map execute entrypoint open)))\n"
                        + "(allow can_early_inputs self (capability (chown)))\n"
                        + "(allow can_early_inputs can_early_input_file (file (read getattr"
                        + " open)))\n"
                        + "(allow can_early_inputs system_app_data_file (dir (getattr"
                        + " search write add_name)))\n"
                        + "(typetransition can_early_inputs system_app_data_file dir"
                        + " \"files\" can_early_data_file)\n"
                        + "(typetransition can_early_inputs system_app_data_file dir"
                        + " \"databases\" can_early_data_file)\n"
                        + "(typetransition can_early_inputs can_early_data_file dir"
                        + " \"databases\" can_early_data_file)\n"
                        + "(allow can_early_inputs can_early_token_dir (dir (getattr"
                        + " search)))\n"
                        + "(allow can_early_inputs can_early_data_file (dir (create open"
                        + " read getattr setattr search write add_name remove_name)))\n"
                        + "(allow can_early_inputs can_early_data_file (file (create open"
                        + " read write getattr setattr link unlink)))\n"
                        + "(allow system_app can_early_data_file (dir (open read getattr"
                        + " search write add_name remove_name)))\n"
                        + "(allow system_app can_early_data_file (file (create open read"
                        + " write append map getattr setattr link unlink rename lock)))\n"
                        + "(allow can_early_inputs block_device (dir (open read getattr"
                        + " search write add_name remove_name)))\n"
                        + "(typetransition can_early_inputs block_device file"
                        + " \"voyemu-early.owner\" can_early_marker_file)\n"
                        + "(typetransition can_early_inputs block_device file"
                        + " \"voyemu-early.owner.new\" can_early_marker_file)\n"
                        + "(allow can_early_inputs loop_device (blk_file (open read getattr"
                        + " ioctl link)))\n"
                        + "(allowx can_early_inputs loop_device (ioctl blk_file (0x125e"
                        + " 0x4c00 0x4c05)))\n"
                        + "(allow can_early_inputs vdh_device (blk_file (open read getattr"
                        + " ioctl link)))\n"
                        + "(allow init loop_device (blk_file (relabelfrom)))\n"
                        + "(allow init vdh_device (blk_file (relabelto)))\n"
                        + "(allowx can_early_inputs vdh_device (ioctl blk_file (0x125e"
                        + " 0x4c00 0x4c05)))\n"
                        + "(allow can_early_inputs loop_control_device (chr_file (open read"
                        + " write getattr ioctl)))\n"
                        + "(allowx can_early_inputs loop_control_device (ioctl chr_file"
                        + " (0x4c82)))\n"
                        + "(allow can_early_inputs can_early_marker_file (file (create open"
                        + " read write getattr setattr link unlink)))\n"
                        + "(allow can_early_inputs property_socket (sock_file (write)))\n"
                        + "(allow can_early_inputs init (unix_stream_socket (connectto)))\n"
                        + "(allow can_early_inputs can_early_ready_prop (property_service"
                        + " (set)))\n"
                        + "(allow can_simulator can_early_ready_prop (file (read open"
                        + " getattr map)))\n"
                        + "(allow can_simulator exported3_system_prop (file (read open"
                        + " getattr map)))\n"
                        + "; Fixed init-created CAN sockets and read-only simulator"
                        + " inspection.\n"
                        + "(allow init can_simulator_socket (unix_stream_socket (create"
                        + " bind)))\n"
                        + "(allow init can_simulator_observe_socket (unix_stream_socket"
                        + " (create bind)))\n"
                        + "(allow can_simulator can_simulator_socket (unix_stream_socket"
                        + " (listen accept read write getattr getopt setopt shutdown)))\n"
                        + "(allow can_simulator can_simulator_observe_socket"
                        + " (unix_stream_socket (listen accept read write getattr getopt"
                        + " setopt shutdown)))\n"
                        + "(allow system_app can_simulator_socket (unix_stream_socket"
                        + " (connectto)))\n"
                        + "(allow su can_simulator_observe_socket (unix_stream_socket"
                        + " (connectto)))\n"
                        + "(allow su can_early_inputs_exec (file (read open getattr)))\n"
                        + "(allow su can_simulator_exec (file (read open getattr)))\n"
                        + "(allow su can_simulator_fixture (file (read open getattr)))\n"
                        + "(allow su can_early_input_file (file (read open getattr)))\n"
                        + "(allow su can_early_ready_prop (file (read open getattr map)))\n"
                        + "(allow su can_early_data_file (dir (getattr search)))\n"
                        + "(allow su can_early_data_file (file (read open getattr map)))\n"
                        + "(allow su vdh_device (blk_file (getattr)))\n"
                        + "(allow system_app can_early_token_dir (dir (getattr search)))\n"
                        + "(allow can_early_inputs can_early_ready_prop (file (read open"
                        + " getattr map)))\n"
                        + "(allow su can_device (chr_file (open read write getattr"
                        + " ioctl)))\n"
                        + "(allowx su can_device (ioctl chr_file (0x0001)))\n"
                        + "(allow system_app can_device (chr_file (open read write getattr"
                        + " ioctl)))\n"
                        + "(allowx system_app can_device (ioctl chr_file (0x0001)))\n"
                        + "(allow installd can_early_data_file (dir (getattr search open"
                        + " read)))\n"
                        + "(allow installd can_early_data_file (file (getattr)))\n";
        require(
                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8))
                        .equals("2879712e7a6a12f262ab346fbe61b7f5b992e652e10d93edf8eb44a6c7b84a77"),
                "Exact early policy output required");
        Files.writeString(file, result);
    }

    static void canEarlyFileContexts(
            Map<String, Object> metadata, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.uses().isEmpty()
                        && constants.equals(Map.of("KIND", "can-early-file-contexts"))
                        && Json.list(metadata.get("calls")).isEmpty(),
                "Exact A9 early file context recipe required");
        Path file = Recipes.output(target);
        String input = Files.readString(file);
        require(
                NativeCore.hash(input.getBytes(StandardCharsets.UTF_8))
                        .equals("b140f7f850f0279e8935053b948784708c9c62de4aeb59019a7c07ef0d9f5813"),
                "Exact simulator file contexts required");
        String result =
                input
                        + "/system/bin/voyemu-early-inputs u:object_r:can_early_inputs_exec:s0\n"
                        + "/system/etc/voyemu/qg-settings-guest\\.db"
                        + " u:object_r:can_early_input_file:s0\n"
                        + "/system/etc/voyemu/hybrid-nvram\\.img"
                        + " u:object_r:can_early_input_file:s0\n"
                        + "/data/user_de/0/com\\.qinggan\\.providers\\.settings/files/databases(/.*)?"
                        + " u:object_r:can_early_data_file:s0\n"
                        + "/dev/block/vdh u:object_r:vdh_device:s0\n"
                        + "/dev/block/voyemu-early\\.owner(\\.new)?"
                        + " u:object_r:can_early_marker_file:s0\n"
                        + "/private u:object_r:can_early_token_dir:s0\n"
                        + "/private/configs u:object_r:can_early_token_dir:s0\n"
                        + "/private/configs/token u:object_r:can_early_token_dir:s0\n";
        require(
                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8))
                        .equals("8bd8062c3b8108bdc34573f78d403691e747298a986a3464ace94721b02ad1d7"),
                "Exact early file contexts output required");
        Files.writeString(file, result);
    }

    static void canEarlyPropertyContexts(
            Map<String, Object> metadata, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.uses().isEmpty()
                        && constants.equals(Map.of("KIND", "can-early-property-contexts"))
                        && Json.list(metadata.get("calls")).isEmpty(),
                "Exact A9 early property recipe required");
        Path file = Recipes.output(target);
        String input = Files.readString(file);
        require(
                NativeCore.hash(input.getBytes(StandardCharsets.UTF_8))
                        .equals("734cfb54ff72018f7c198a4f558a59428836dc0abe20da6cc68cc77c4526101e"),
                "Exact A9 property contexts required");
        String result =
                input
                        + "sys.voyemu.early.ready u:object_r:can_early_ready_prop:s0 exact"
                        + " string\n"
                        + "sys.voyemu.early.probe u:object_r:exported3_system_prop:s0 exact"
                        + " string\n";
        require(
                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8))
                        .equals("bd2a3eafde0f3d98966d590dcc70047f9fa38e9812f3660a0326182e0f43540d"),
                "Exact early property output required");
        Files.writeString(file, result);
    }

    static void canSimulatorPolicy(
            Map<String, Object> metadata, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.uses()
                                .keySet()
                                .equals(Set.of("original:PlatSepolicy", "original:VendorSepolicy"))
                        && constants.equals(Map.of("KIND", "can-simulator-policy"))
                        && Json.list(metadata.get("calls")).isEmpty(),
                "Exact A9 simulator policy recipe required");
        Path file = Recipes.output(target);
        String input = Files.readString(file);
        require(
                NativeCore.hash(input.getBytes(StandardCharsets.UTF_8))
                        .equals("3df9140132bf65a7e6e59d8fec735595feee3ed3b32d57d7f6b4a10b82cb903e"),
                "Exact bounded A9 policy required");
        String result =
                input
                        + "; candidate CAN simulator policy: subject to secilc and AVC"
                        + " validation\n"
                        + "(type can_simulator)\n"
                        + "(type can_simulator_exec)\n"
                        + "(type can_simulator_fixture)\n"
                        + "(type can_device)\n"
                        + "(type can_simulator_device)\n"
                        + "(type can_simulator_socket)\n"
                        + "(type can_simulator_observe_socket)\n"
                        + "(roletype object_r can_simulator)\n"
                        + "(roletype object_r can_simulator_exec)\n"
                        + "(roletype object_r can_simulator_fixture)\n"
                        + "(roletype object_r can_device)\n"
                        + "(roletype object_r can_simulator_device)\n"
                        + "(roletype object_r can_simulator_socket)\n"
                        + "(roletype object_r can_simulator_observe_socket)\n"
                        + "(typeattributeset domain (can_simulator))\n"
                        + "(typeattributeset coredomain (can_simulator))\n"
                        + "(typeattributeset file_type (can_simulator_exec"
                        + " can_simulator_fixture can_simulator_socket"
                        + " can_simulator_observe_socket))\n"
                        + "(typeattributeset exec_type (can_simulator_exec))\n"
                        + "(typeattributeset dev_type (can_device can_simulator_device))\n"
                        + "(typeattributeset coredomain_socket (can_simulator_socket"
                        + " can_simulator_observe_socket))\n"
                        + "(allow init can_simulator_exec (file (read getattr map execute"
                        + " open)))\n"
                        + "(allow init can_simulator (process (transition siginh"
                        + " rlimitinh)))\n"
                        + "(typetransition init can_simulator_exec process can_simulator)\n"
                        + "(allow can_simulator can_simulator_exec (file (read getattr map"
                        + " execute entrypoint open)))\n"
                        + "(allow can_simulator can_simulator_fixture (file (read getattr"
                        + " open)))\n"
                        + "(allow can_simulator can_simulator_device (chr_file (getattr"
                        + " open read write ioctl)))\n"
                        + "(allowx can_simulator can_simulator_device (ioctl chr_file"
                        + " (0x0002 0x0003)))\n"
                        + "(allow can_simulator proc (dir (search)))\n"
                        + "(allow can_simulator proc_random (dir (search)))\n"
                        + "(allow can_simulator proc_random (file (read getattr open)))\n"
                        + "(allow can_simulator init (fd (use)))\n"
                        + "(allow can_simulator init (unix_stream_socket (read write"
                        + " getattr getopt setopt accept)))\n"
                        + "(allow init can_simulator_socket (sock_file (create setattr"
                        + " unlink write)))\n"
                        + "(allow init can_simulator_observe_socket (sock_file (create"
                        + " setattr unlink write)))\n"
                        + "(allow ueventd can_simulator_device (chr_file (create setattr"
                        + " getattr unlink)))\n"
                        + "(allow ueventd can_device (chr_file (create setattr getattr"
                        + " unlink)))\n"
                        + "(allow system_app can_simulator_socket (sock_file (write)))\n"
                        + "(allow su can_simulator_observe_socket (sock_file (write)))\n";
        require(
                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8))
                        .equals("ea017c277007b5ba8401ab214b2d607d72ddd39b7a303db05eb5281cb405fd22"),
                "Exact simulator policy output required");
        Files.writeString(file, result);
    }

    static void canSimulatorContexts(
            Map<String, Object> metadata, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.uses().isEmpty()
                        && constants.equals(Map.of("KIND", "can-simulator-file-contexts"))
                        && Json.list(metadata.get("calls")).isEmpty(),
                "Exact A9 simulator context recipe required");
        Path file = Recipes.output(target);
        String input = Files.readString(file);
        require(
                NativeCore.hash(input.getBytes(StandardCharsets.UTF_8))
                        .equals("88e8b4bf854406de5a5464ea37f0996487ae60e7e12ae1302fc9119e9b46456c"),
                "Exact A9 platform contexts required");
        String result =
                input
                        + "/system/bin/voyemu-can-model u:object_r:can_simulator_exec:s0\n"
                        + "/system/etc/voyemu/can-model\\.fixture"
                        + " u:object_r:can_simulator_fixture:s0\n"
                        + "/dev/cis_can u:object_r:can_device:s0\n"
                        + "/dev/cis_can_model u:object_r:can_simulator_device:s0\n"
                        + "/dev/socket/voyemu_can_model"
                        + " u:object_r:can_simulator_socket:s0\n"
                        + "/dev/socket/voyemu_can_observe"
                        + " u:object_r:can_simulator_observe_socket:s0\n";
        require(
                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8))
                        .equals("b140f7f850f0279e8935053b948784708c9c62de4aeb59019a7c07ef0d9f5813"),
                "Exact simulator contexts output required");
        Files.writeString(file, result);
    }

    static void settingNetworkGaps(String name, Map<String, Object> metadata, Recipes.Target target)
            throws Exception {
        Recipes.literalCalls(name, metadata, target);
        String manager = "Lcom/qinggan/setting/netlib/utils/NetworkMgr;->",
                wifi = "Landroid/net/wifi/WifiManager;->";
        Recipes.method(
                target,
                manager + "<clinit>()V",
                Recipes.guardedCall(
                        "invoke-virtual {v1, v3, v2}, "
                                + wifi
                                + "registerSoftApCallback(Landroid/net/wifi/WifiManager$SoftApCallback;"
                                + "Landroid/os/Handler;)V",
                        "register"));
        Recipes.method(
                target,
                manager + "destroy()V",
                Recipes.guardedCall(
                        "invoke-virtual {v0, v1}, "
                                + wifi
                                + "unregisterSoftApCallback(Landroid/net/wifi/WifiManager$SoftApCallback;)V",
                        "unregister"));
    }

    static void usbPlayer(
            Map<String, Object> metadata, Map<String, Object> constants, Recipes.Target target)
            throws Exception {
        require(
                target.api() == 28
                        && target.abi().equals("arm64-v8a")
                        && target.originalSha256().equals(Api28UsbPlayerGuard.PIN)
                        && constants.isEmpty()
                        && Json.list(metadata.get("calls")).isEmpty(),
                "Exact A9 USB decoder operation required");
        target.guardApi28UsbPlayer();
    }

    static void systemServiceFramework(Recipes.Target target) throws Exception {
        for (String lib : List.of("qgapi", "qgmod", "qgvehicle")) {
            Recipes.use(target, "component:framework-" + lib);
        }

        target.useLibraries(List.of("QGAPI", "QGMOD", "QGVehicle"));
    }

    private static void require(boolean condition, String message) {
        Recipes.require(condition, message);
    }
}
