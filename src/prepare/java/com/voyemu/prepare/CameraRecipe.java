package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Exact API 28 virtual camera recipe; none preserves all OEM state. */
final class CameraRecipe {
    private static final String CAPABILITIES = "Lcom/voyemu/oem/CameraCapabilities;->";
    private static final String COMMON = "Lcom/qinggan/camera/common/";
    private static final String ACTIVITY = "Lcom/qinggan/camera/activity/RecordVideoActivity;";
    private static final String SETTING = "Lcom/qinggan/camera/common/CameraSetting;";

    /** Method bodies that delegate to the own capability provider. */
    private static final List<DexEdits.MethodEdit> BODIES =
            List.of(
                    body(
                            "SupportUtils;->isSupportDvr()Z",
                            "invoke-static {}, " + CAPABILITIES + "isSupportDvr()Z",
                            "move-result v0",
                            "return v0"),
                    body(
                            "CameraUtil;->getOutputMediaFileNew(I)Ljava/io/File;",
                            "invoke-static {p0}, "
                                    + CAPABILITIES
                                    + "getOutputMediaFileNew(I)Ljava/io/File;",
                            "move-result-object p0",
                            "return-object p0"),
                    body(
                            "CameraUtil;->getStatFsRatio(Ljava/lang/String;)F",
                            "invoke-static {p0}, "
                                    + CAPABILITIES
                                    + "getStatFsRatio(Ljava/lang/String;)F",
                            "move-result p0",
                            "return p0"),
                    body(
                            "CameraUtil;->setDmsInCameraAppUsing(I)V",
                            "invoke-static {p0}, " + CAPABILITIES + "setDmsInCameraAppUsing(I)V",
                            "return-void"),
                    body(
                            "DaoUtil;->getLatestSource()Ljava/io/File;",
                            "invoke-static {}, " + CAPABILITIES + "getLatestSource()Ljava/io/File;",
                            "move-result-object v0",
                            "return-object v0"));

    private static DexEdits.MethodEdit body(String method, String... lines) {
        return new DexEdits.MethodEdit(COMMON + method, 1, List.of(), List.of(lines));
    }

    static Map<String, Object> validate(Object input) {
        Map<String, Object> value;

        try {
            value = Json.object(input);
        } catch (IllegalArgumentException error) {
            throw new Failure("CAMERA_CONTRACT", "Object required", 5);
        }

        Object schema = value.get("schema");
        Failure.require(
                value.keySet()
                                .equals(
                                        Set.of(
                                                "schema",
                                                "profile",
                                                "backend",
                                                "physicalDms",
                                                "physicalRpm"))
                        && schema instanceof Number number
                        && number.doubleValue() == 1
                        && "free-09-chinese-20260106-6.0.3".equals(value.get("profile"))
                        && ("none".equals(value.get("backend"))
                                || "android-back-camera".equals(value.get("backend")))
                        && Boolean.FALSE.equals(value.get("physicalDms"))
                        && Boolean.FALSE.equals(value.get("physicalRpm")),
                "CAMERA_CONTRACT",
                "Explicit virtual backend required; no physical DMS/RPM",
                5);

        return value;
    }

    static void apply(Recipes.Target target) throws Exception {
        Failure.require(
                target.api() == 28,
                "CAMERA_CONTRACT",
                "Camera backend requires pinned Android 9",
                5);
        Path contract =
                Storage.confined(
                        target.stepRoot(), "camera-capabilities/voyemu-camera-capabilities.json");
        Failure.require(
                Files.size(contract) <= 4096, "CAMERA_CONTRACT", "Oversized camera contract", 5);
        byte[] bytes = Files.readAllBytes(contract);
        Failure.require(bytes.length <= 4096, "CAMERA_CONTRACT", "Oversized camera contract", 5);
        Map<String, Object> value =
                validate(Json.parse(new String(bytes, java.nio.charset.StandardCharsets.UTF_8)));

        if ("none".equals(value.get("backend"))) {
            return;
        }

        // Every OEM method edit applies before addDex/putEntry.
        String setting = SETTING + "->";
        target.dex().edit(BODIES);
        Recipes.method(
                target,
                ACTIVITY + "->initIntent(Landroid/content/Intent;)V",
                DexEdits.Edit.replace(
                        List.of("iput v5, p0, " + ACTIVITY + "->mInType:I"),
                        List.of(
                                "const/4 v7, 0x1",
                                "if-ne v5, v7, :voyemu_input_ready",
                                "const/4 v5, 0x0",
                                ":voyemu_input_ready",
                                "iput v5, p0, " + ACTIVITY + "->mInType:I")),
                DexEdits.Edit.replace(
                        List.of("const-string p1, \"cameraType is init, set self_front\""),
                        List.of("const-string p1, \"Voyemu virtual back camera default: dvr\"")),
                DexEdits.Edit.replace(
                        List.of(
                                "invoke-virtual {p1, v4}, " + setting + "setCameraType(I)V",
                                "invoke-static {}, " + setting + "getInstance()" + SETTING,
                                "move-result-object p1",
                                "invoke-virtual {p1}, " + setting + "getCameraPattern()I"),
                        List.of(
                                "const/4 v4, 0x2",
                                "invoke-virtual {p1, v4}, " + setting + "setCameraType(I)V",
                                "const/4 v4, 0x0",
                                "invoke-static {}, " + setting + "getInstance()" + SETTING,
                                "move-result-object p1",
                                "invoke-virtual {p1}, " + setting + "getCameraPattern()I")));
        Recipes.method(
                target,
                ACTIVITY + "->onClick(Landroid/view/View;)V",
                DexEdits.Edit.replace(
                        List.of(
                                "invoke-virtual {p0},"
                                    + " Lcom/qinggan/camera/activity/RecordVideoActivity;->getPackageManager()Landroid/content/pm/PackageManager;",
                                "move-result-object p1",
                                "const-string v0, \"com.qinggan.app.gallery\"",
                                "invoke-virtual {p1, v0},"
                                    + " Landroid/content/pm/PackageManager;->getLaunchIntentForPackage(Ljava/lang/String;)Landroid/content/Intent;",
                                "move-result-object p1",
                                "move-object v0, p0",
                                "check-cast v0, Landroid/content/Context;",
                                "invoke-static {v0},"
                                    + " Lcom/qinggan/os/ServiceManager;->getDisplay(Landroid/content/Context;)Landroid/view/Display;",
                                "move-result-object v0",
                                "invoke-virtual {v0}, Landroid/view/Display;->getDisplayId()I",
                                "move-result v0",
                                "invoke-static {},"
                                    + " Landroid/app/ActivityOptions;->makeBasic()Landroid/app/ActivityOptions;",
                                "move-result-object v1",
                                "invoke-virtual {v1, v0},"
                                    + " Landroid/app/ActivityOptions;->setLaunchDisplayId(I)Landroid/app/ActivityOptions;",
                                "move-result-object v0",
                                "if-nez p1, :cond_85",
                                "goto :goto_8a",
                                "const/high16 v1, 0x10000000",
                                "invoke-virtual {p1, v1},"
                                    + " Landroid/content/Intent;->setFlags(I)Landroid/content/Intent;",
                                "new-instance v1, Landroid/os/Bundle;",
                                "invoke-direct {v1}, Landroid/os/Bundle;-><init>()V",
                                "const-string v2, \"cameraIn\"",
                                "invoke-virtual {v1, v2, v8},"
                                        + " Landroid/os/Bundle;->putInt(Ljava/lang/String;I)V",
                                "if-nez p1, :cond_97",
                                "goto :goto_9a",
                                "invoke-virtual {p1, v1},"
                                    + " Landroid/content/Intent;->putExtras(Landroid/os/Bundle;)Landroid/content/Intent;",
                                "invoke-virtual {v0},"
                                    + " Landroid/app/ActivityOptions;->toBundle()Landroid/os/Bundle;",
                                "move-result-object v0",
                                "invoke-virtual {p0, p1, v0},"
                                    + " Lcom/qinggan/camera/activity/RecordVideoActivity;->startActivity(Landroid/content/Intent;Landroid/os/Bundle;)V"),
                        List.of(
                                "invoke-static {p0}, Lcom/voyemu/oem/CameraAlbum;"
                                        + "->open(Landroid/app/Activity;)V")));
        Path stage = Files.createTempDirectory(target.work(), ".voyemu-camera-contract-");

        try {
            Path asset = stage.resolve("voyemu-camera-capabilities.json");
            Files.write(asset, bytes, java.nio.file.StandardOpenOption.CREATE_NEW);
            target.addDex("camera-capabilities/src");
            target.putEntry(asset, "assets/voyemu-camera-capabilities.json");
        } finally {
            FilesSupport.removeOwnedTree(stage);
        }
    }
}
