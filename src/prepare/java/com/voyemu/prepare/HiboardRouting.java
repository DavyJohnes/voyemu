package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.EpilogueBegin;
import org.jf.dexlib2.iface.debug.LineNumber;
import org.jf.dexlib2.iface.debug.PrologueEnd;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.debug.SetSourceFile;
import org.jf.dexlib2.iface.debug.StartLocal;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// The original panel, edge delegate and dim lifecycle calls retain their gestures and parameters.
final class HiboardRouting {
    static final String NAME = "api30-hiboard-routing";
    static final String MANAGER = "Landroid/view/WindowManager;",
            ROUTER = "Lcom/voyemu/output/HiboardRouter;";

    record Site(int address, String name, int count, int c, int d, int e, int f, int g) {
        boolean layout() {
            return name.equals("addView") || name.equals("updateViewLayout");
        }

        List<String> parameters(boolean routed) {
            var out = new ArrayList<String>();

            if (routed) {
                out.add(MANAGER);
            }

            out.add("Landroid/view/View;");

            if (layout()) {
                out.add("Landroid/view/ViewGroup$LayoutParams;");
            }

            return out;
        }

        String reference(boolean routed) {
            return (routed ? ROUTER : MANAGER)
                    + "->"
                    + name
                    + "("
                    + String.join("", parameters(routed))
                    + ")V";
        }
    }

    record Body(String method, int access, int registers, List<Site> sites) {}

    /** Profile data; jumbo marks the input whose serialization widens const-string. */
    record Variant(String pin, List<Body> bodies, boolean jumbo) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant(
                            "648c91cccacae25b6810f93b44bfa2b7aed71cadee44353a964d09658cb8f858",
                            List.of(
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow$3;->onAnimationEnd(Landroid/animation/Animator;)V",
                                            1,
                                            5,
                                            List.of(new Site(45, "removeView", 2, 4, 0, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->moveWindow(ZI)V",
                                            2,
                                            8,
                                            List.of(new Site(283, "removeView", 2, 6, 7, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->updatePosition(I)V",
                                            2,
                                            5,
                                            List.of(
                                                    new Site(57, "addView", 3, 4, 0, 1, 0, 0),
                                                    new Site(
                                                            67,
                                                            "updateViewLayout",
                                                            3,
                                                            4,
                                                            0,
                                                            1,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->onStartTrackingTouch()V",
                                            0,
                                            4,
                                            List.of(new Site(31, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->update()V",
                                            1,
                                            4,
                                            List.of(
                                                    new Site(14, "addView", 3, 0, 1, 2, 0, 0),
                                                    new Site(
                                                            24,
                                                            "updateViewLayout",
                                                            3,
                                                            0,
                                                            1,
                                                            2,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->update(Lcom/qinggan/app/hiboard/BlindsWindow$LayoutParams;)V",
                                            1,
                                            4,
                                            List.of(
                                                    new Site(12, "addView", 3, 0, 1, 3, 0, 0),
                                                    new Site(
                                                            20,
                                                            "updateViewLayout",
                                                            3,
                                                            0,
                                                            1,
                                                            3,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar$10;->run()V",
                                            1,
                                            3,
                                            List.of(
                                                    new Site(8, "removeView", 2, 0, 1, 0, 0, 0),
                                                    new Site(19, "removeView", 2, 0, 1, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar$11;->run()V",
                                            1,
                                            3,
                                            List.of(
                                                    new Site(4, "removeView", 2, 0, 1, 0, 0, 0),
                                                    new Site(11, "removeView", 2, 0, 1, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar$12;->run()V",
                                            1,
                                            3,
                                            List.of(
                                                    new Site(
                                                            44,
                                                            "removeViewImmediate",
                                                            2,
                                                            0,
                                                            1,
                                                            0,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel$1;->onDimUpdate(IZ)V",
                                            1,
                                            6,
                                            List.of(
                                                    new Site(81, "removeView", 2, 5, 0, 0, 0, 0),
                                                    new Site(115, "addView", 3, 5, 0, 1, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel$2;->onDimUpdate(IZ)V",
                                            1,
                                            6,
                                            List.of(
                                                    new Site(59, "removeView", 2, 5, 0, 0, 0, 0),
                                                    new Site(93, "addView", 3, 5, 0, 1, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel;->removeDimBackground()V",
                                            2,
                                            3,
                                            List.of(new Site(7, "removeView", 2, 1, 0, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel;->removeSecondDimBackground()V",
                                            2,
                                            3,
                                            List.of(new Site(7, "removeView", 2, 1, 0, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->amendThemeIfNeeded()V",
                                            2,
                                            22,
                                            List.of(
                                                    new Site(218, "addView", 3, 1, 4, 12, 0, 0),
                                                    new Site(221, "addView", 3, 1, 5, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->showThemeChangeLoadingLayout()V",
                                            2,
                                            6,
                                            List.of(
                                                    new Site(
                                                            151,
                                                            "removeViewImmediate",
                                                            2,
                                                            0,
                                                            2,
                                                            0,
                                                            0,
                                                            0),
                                                    new Site(158, "addView", 3, 0, 2, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->setTheme(IILjava/lang/String;)V",
                                            1,
                                            22,
                                            List.of(
                                                    new Site(282, "addView", 3, 8, 5, 3, 0, 0),
                                                    new Site(287, "addView", 3, 3, 6, 7, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/SecondHiboardBar;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->addWindowView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                            2,
                                            4,
                                            List.of(new Site(2, "addView", 3, 0, 2, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->removeWindowView(Landroid/view/View;)V",
                                            2,
                                            3,
                                            List.of(new Site(2, "removeView", 2, 0, 2, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->doScreenLift(I)V",
                                            1,
                                            7,
                                            List.of(
                                                    new Site(
                                                            135,
                                                            "updateViewLayout",
                                                            3,
                                                            2,
                                                            3,
                                                            4,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->addWindowView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                            2,
                                            4,
                                            List.of(new Site(2, "addView", 3, 0, 2, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->removeWindowView(Landroid/view/View;)V",
                                            2,
                                            3,
                                            List.of(new Site(2, "removeView", 2, 0, 2, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->doScreenLift(I)V",
                                            1,
                                            7,
                                            List.of(
                                                    new Site(
                                                            142,
                                                            "updateViewLayout",
                                                            3,
                                                            2,
                                                            3,
                                                            4,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/second/SecondVehicleHiboardBar;->doScreenLift(I)V",
                                            1,
                                            5,
                                            List.of(
                                                    new Site(
                                                            153,
                                                            "updateViewLayout",
                                                            3,
                                                            4,
                                                            0,
                                                            1,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/second/SecondVehicleHiboardBar;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0)))),
                            true),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "902562587355d9e48d3878773da7955cd5a793c03e5ba6e5b595a317c7ec0f39",
                            List.of(
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow$3;->onAnimationEnd(Landroid/animation/Animator;)V",
                                            1,
                                            5,
                                            List.of(new Site(45, "removeView", 2, 4, 0, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->moveWindow(ZI)V",
                                            2,
                                            8,
                                            List.of(new Site(283, "removeView", 2, 6, 7, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->updatePosition(I)V",
                                            2,
                                            5,
                                            List.of(
                                                    new Site(57, "addView", 3, 4, 0, 1, 0, 0),
                                                    new Site(
                                                            67,
                                                            "updateViewLayout",
                                                            3,
                                                            4,
                                                            0,
                                                            1,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->onStartTrackingTouch()V",
                                            0,
                                            4,
                                            List.of(new Site(31, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->update()V",
                                            1,
                                            4,
                                            List.of(
                                                    new Site(14, "addView", 3, 0, 1, 2, 0, 0),
                                                    new Site(
                                                            24,
                                                            "updateViewLayout",
                                                            3,
                                                            0,
                                                            1,
                                                            2,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BlindsWindow;->update(Lcom/qinggan/app/hiboard/BlindsWindow$LayoutParams;)V",
                                            1,
                                            4,
                                            List.of(
                                                    new Site(12, "addView", 3, 0, 1, 3, 0, 0),
                                                    new Site(
                                                            20,
                                                            "updateViewLayout",
                                                            3,
                                                            0,
                                                            1,
                                                            3,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar$13;->run()V",
                                            1,
                                            3,
                                            List.of(
                                                    new Site(8, "removeView", 2, 0, 1, 0, 0, 0),
                                                    new Site(19, "removeView", 2, 0, 1, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar$14;->run()V",
                                            1,
                                            3,
                                            List.of(
                                                    new Site(4, "removeView", 2, 0, 1, 0, 0, 0),
                                                    new Site(11, "removeView", 2, 0, 1, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar$15;->run()V",
                                            1,
                                            3,
                                            List.of(
                                                    new Site(
                                                            44,
                                                            "removeViewImmediate",
                                                            2,
                                                            0,
                                                            1,
                                                            0,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel$1;->onDimUpdate(IZ)V",
                                            1,
                                            6,
                                            List.of(
                                                    new Site(81, "removeView", 2, 5, 0, 0, 0, 0),
                                                    new Site(115, "addView", 3, 5, 0, 1, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel$2;->onDimUpdate(IZ)V",
                                            1,
                                            6,
                                            List.of(
                                                    new Site(59, "removeView", 2, 5, 0, 0, 0, 0),
                                                    new Site(93, "addView", 3, 5, 0, 1, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel;->removeDimBackground()V",
                                            2,
                                            3,
                                            List.of(new Site(7, "removeView", 2, 1, 0, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/BaseControlModel;->removeSecondDimBackground()V",
                                            2,
                                            3,
                                            List.of(new Site(7, "removeView", 2, 1, 0, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->amendThemeIfNeeded()V",
                                            2,
                                            22,
                                            List.of(
                                                    new Site(218, "addView", 3, 1, 4, 12, 0, 0),
                                                    new Site(221, "addView", 3, 1, 5, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->showThemeChangeLoadingLayout()V",
                                            2,
                                            6,
                                            List.of(
                                                    new Site(
                                                            151,
                                                            "removeViewImmediate",
                                                            2,
                                                            0,
                                                            2,
                                                            0,
                                                            0,
                                                            0),
                                                    new Site(158, "addView", 3, 0, 2, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->setTheme(IILjava/lang/String;)V",
                                            1,
                                            22,
                                            List.of(
                                                    new Site(282, "addView", 3, 8, 5, 3, 0, 0),
                                                    new Site(287, "addView", 3, 3, 6, 7, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/HiboardBar;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/system/SecondHiboardBar;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->addWindowView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                            2,
                                            4,
                                            List.of(new Site(2, "addView", 3, 0, 2, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->removeWindowView(Landroid/view/View;)V",
                                            2,
                                            3,
                                            List.of(new Site(2, "removeView", 2, 0, 2, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->doScreenLift(I)V",
                                            1,
                                            7,
                                            List.of(
                                                    new Site(
                                                            135,
                                                            "updateViewLayout",
                                                            3,
                                                            2,
                                                            3,
                                                            4,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNew;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->addWindowView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                            2,
                                            4,
                                            List.of(new Site(2, "addView", 3, 0, 2, 3, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->removeWindowView(Landroid/view/View;)V",
                                            2,
                                            3,
                                            List.of(new Site(2, "removeView", 2, 0, 2, 0, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->doScreenLift(I)V",
                                            1,
                                            7,
                                            List.of(
                                                    new Site(
                                                            142,
                                                            "updateViewLayout",
                                                            3,
                                                            2,
                                                            3,
                                                            4,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/main/VehicleHiBoardBarNewOTA;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/second/SecondVehicleHiboardBar;->doScreenLift(I)V",
                                            1,
                                            5,
                                            List.of(
                                                    new Site(
                                                            153,
                                                            "updateViewLayout",
                                                            3,
                                                            4,
                                                            0,
                                                            1,
                                                            0,
                                                            0))),
                                    new Body(
                                            "Lcom/qinggan/app/hiboard/vehiclehiboard/second/SecondVehicleHiboardBar;->show()V",
                                            1,
                                            4,
                                            List.of(new Site(14, "addView", 3, 0, 1, 2, 0, 0)))),
                            false));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.pin());
    }

    static Method normalized(Method method) {
        if (method.getImplementation() == null) {
            return method;
        }

        var body = new MutableMethodImplementation(method.getImplementation());

        for (int index = 0; index < body.getInstructions().size(); index++) {
            var instruction = body.getInstructions().get(index);

            if (instruction.getOpcode() == Opcode.CONST_STRING) {
                var source = (org.jf.dexlib2.iface.instruction.formats.Instruction21c) instruction;
                body.replaceInstruction(
                        index,
                        new org.jf.dexlib2.builder.instruction.BuilderInstruction31c(
                                Opcode.CONST_STRING_JUMBO,
                                source.getRegisterA(),
                                source.getReference()));
            }
        }

        body.getInstructions();
        var original = new ArrayList<org.jf.dexlib2.iface.debug.DebugItem>();
        method.getImplementation().getDebugItems().forEach(original::add);
        var moved = new ArrayList<org.jf.dexlib2.iface.debug.DebugItem>();
        body.getDebugItems().forEach(moved::add);
        require(original.size() == moved.size());
        var debug = new ArrayList<org.jf.dexlib2.immutable.debug.ImmutableDebugItem>();

        for (int i = 0; i < original.size(); i++) {
            var item = original.get(i);
            int at = moved.get(i).getCodeAddress();
            require(item.getDebugItemType() == moved.get(i).getDebugItemType());

            switch (item) {
                case StartLocal local ->
                        debug.add(
                                new org.jf.dexlib2.immutable.debug.ImmutableStartLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case EndLocal local ->
                        debug.add(
                                new org.jf.dexlib2.immutable.debug.ImmutableEndLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case RestartLocal local ->
                        debug.add(
                                new org.jf.dexlib2.immutable.debug.ImmutableRestartLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case LineNumber line ->
                        debug.add(
                                new org.jf.dexlib2.immutable.debug.ImmutableLineNumber(
                                        at, line.getLineNumber()));
                case PrologueEnd _ ->
                        debug.add(new org.jf.dexlib2.immutable.debug.ImmutablePrologueEnd(at));
                case EpilogueBegin _ ->
                        debug.add(new org.jf.dexlib2.immutable.debug.ImmutableEpilogueBegin(at));
                case SetSourceFile source ->
                        debug.add(
                                new org.jf.dexlib2.immutable.debug.ImmutableSetSourceFile(
                                        at, source.getSourceFile()));
                default -> require(false);
            }
        }

        var implementation =
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        body.getInstructions(),
                        body.getTryBlocks(),
                        debug);

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                implementation);
    }

    private static ClassDef normalized(ClassDef type) {
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();

        for (var method : type.getDirectMethods()) {
            direct.add(normalized(method));
        }

        for (var method : type.getVirtualMethods()) {
            virtual.add(normalized(method));
        }

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                type.getStaticFields(),
                type.getInstanceFields(),
                direct,
                virtual);
    }

    private static void require(boolean b) {
        Failure.require(
                b,
                "HIBOARD_ROUTING",
                "Exact Free A11 7.3.1 original Hiboard window routing contract required",
                3);
    }

    static Body body(String profile, Method m) {
        for (var b : VARIANTS.get(profile).bodies()) {
            if (b.method().equals(m.toString())) {
                return b;
            }
        }

        return null;
    }

    static void validateMethod(Method m, Body b) throws Exception {
        require(
                b != null
                        && m.toString().equals(b.method())
                        && m.getAccessFlags() == b.access()
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == b.registers()
                        && m.getImplementation().getTryBlocks().isEmpty());
        rewrite(m, b, true);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(
                VARIANTS.containsKey(profile)
                        && api == 30
                        && FilesSupport.hash(original).equals(VARIANTS.get(profile).pin()));
        require(dex != null);
        int found = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getMethods()) {
                var b = body(profile, m);

                if (b != null) {
                    validateMethod(m, b);
                    found++;
                }
            }
        }

        require(found == VARIANTS.get(profile).bodies().size());
    }

    static Method rewrite(Method m, Body b, boolean forward) {
        var code = new MutableMethodImplementation(m.getImplementation());
        int address = 0, replaced = 0;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var old = code.getInstructions().get(i);

            for (var s : b.sites()) {
                if (address == s.address()) {
                    require(
                            old.getOpcode()
                                            == (forward
                                                    ? Opcode.INVOKE_INTERFACE
                                                    : Opcode.INVOKE_STATIC)
                                    && old instanceof FiveRegisterInstruction
                                    && old instanceof ReferenceInstruction);
                    var r = (FiveRegisterInstruction) old;
                    require(
                            ((ReferenceInstruction) old)
                                            .getReference()
                                            .toString()
                                            .equals(s.reference(!forward))
                                    && r.getRegisterCount() == s.count()
                                    && r.getRegisterC() == s.c()
                                    && r.getRegisterD() == s.d()
                                    && r.getRegisterE() == s.e()
                                    && r.getRegisterF() == s.f()
                                    && r.getRegisterG() == s.g());
                    code.replaceInstruction(
                            i,
                            new BuilderInstruction35c(
                                    forward ? Opcode.INVOKE_STATIC : Opcode.INVOKE_INTERFACE,
                                    s.count(),
                                    s.c(),
                                    s.d(),
                                    s.e(),
                                    s.f(),
                                    s.g(),
                                    new ImmutableMethodReference(
                                            forward ? ROUTER : MANAGER,
                                            s.name(),
                                            s.parameters(forward),
                                            "V")));
                    replaced++;
                }
            }

            address += old.getCodeUnits();
        }

        require(replaced == b.sites().size());

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                code);
    }

    private static ClassDef rewriteClass(String profile, ClassDef c, boolean forward) {
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();

        for (var m : c.getDirectMethods()) {
            var b = body(profile, m);
            direct.add(b == null ? m : rewrite(m, b, forward));
        }

        for (var m : c.getVirtualMethods()) {
            var b = body(profile, m);
            virtual.add(b == null ? m : rewrite(m, b, forward));
        }

        return new ImmutableClassDef(
                c.getType(),
                c.getAccessFlags(),
                c.getSuperclass(),
                c.getInterfaces(),
                c.getSourceFile(),
                c.getAnnotations(),
                c.getStaticFields(),
                c.getInstanceFields(),
                direct,
                virtual);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = new TreeMap<String, ClassDef>();
        var changed = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(before.put(c.getType(), c) == null);
            changed.put(c.getType(), rewriteClass(profile, c, true));
        }

        byte[] out =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), changed.values()));
        var read = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), out).getClasses()) {
            require(read.put(c.getType(), c) == null);
        }

        require(read.keySet().equals(before.keySet()));

        for (var e : read.entrySet()) {
            if (VARIANTS.get(profile).jumbo()) {
                ClassDef expected = normalized(changed.get(e.getKey())),
                        actual = normalized(e.getValue());

                if (!FrameworkDexMaterializer.semantic(expected)
                        .equals(FrameworkDexMaterializer.semantic(actual))) {
                    throw new Failure(
                            "HIBOARD_ROUTING",
                            "Serialized class changed: "
                                    + e.getKey()
                                    + " "
                                    + FrameworkDexMaterializer.difference(expected, actual),
                            3);
                }

                ClassDef normalizedOriginal = normalized(before.get(e.getKey())),
                        restored = normalized(rewriteClass(profile, e.getValue(), false));

                if (!FrameworkDexMaterializer.semantic(normalizedOriginal)
                        .equals(FrameworkDexMaterializer.semantic(restored))) {
                    throw new Failure(
                            "HIBOARD_ROUTING",
                            "Inverse class changed: "
                                    + e.getKey()
                                    + " "
                                    + FrameworkDexMaterializer.difference(
                                            normalizedOriginal, restored),
                            3);
                }
            } else {
                require(
                        FrameworkDexMaterializer.semantic(changed.get(e.getKey()))
                                .equals(FrameworkDexMaterializer.semantic(e.getValue())));
                require(
                        FrameworkDexMaterializer.semantic(before.get(e.getKey()))
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                rewriteClass(profile, e.getValue(), false))));
            }
        }

        require(FilesSupport.hash(original).equals(VARIANTS.get(profile).pin()));

        return out;
    }
}
