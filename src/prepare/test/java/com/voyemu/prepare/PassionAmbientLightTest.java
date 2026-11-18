package com.voyemu.prepare;

import android.view.View;
import android.view.ViewGroup;

import com.voyemu.window.PassionAmbientLight;

public class PassionAmbientLightTest {
    public static void main(String[] a) {
        View root = new View(), toggle = new View(), other = new View(), slider = new View();
        View disabled = new View();

        disabled.enabled = false;
        ViewGroup group = new ViewGroup(), nested = new ViewGroup();
        group.items.add(disabled);
        group.items.add(nested);
        nested.items.add(slider);
        root.children.put(0x7f0a044f, toggle);
        root.children.put(0x7f0a0273, group);
        root.children.put(77, other);
        PassionAmbientLight.refresh(root, 2);

        if (!toggle.enabled || !slider.enabled) {
            throw new AssertionError();
        }

        PassionAmbientLight.refresh(root, -1);

        if (toggle.enabled || group.enabled || nested.enabled || slider.enabled || !other.enabled) {
            throw new AssertionError();
        }

        PassionAmbientLight.refresh(root, 0);

        if (!toggle.enabled
                || !group.enabled
                || !nested.enabled
                || !slider.enabled
                || disabled.enabled) {
            throw new AssertionError(
                    "restore exact prior enabled state including original disabled control");
        }

        PassionAmbientLight.refresh(root, -1);
        PassionAmbientLight.refresh(root, -1);
        PassionAmbientLight.refresh(root, 2);

        if (!slider.enabled || disabled.enabled) {
            throw new AssertionError("repeated unavailable must not overwrite original states");
        }

        PassionAmbientLight.refresh(null, -1);
        PassionAmbientLight.refresh(new View(), -1);
        System.out.println(
                "ambient-only null disabled recursively; supported mode0/2 preserved; unrelated"
                        + " body controls unchanged; no light-state writes PASS");
    }
}
