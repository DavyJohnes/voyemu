package com.voyemu.api;

import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

/** JSON of one ViewServer window and its legacy dump nodes for an attached-window surface. */
final class AttachedJson {
    private AttachedJson() {}

    static JSONObject window(ViewServer.Window window) throws Exception {
        return new JSONObject()
                .put("id", window.hash)
                .put("title", window.title)
                .put("display_id", window.display)
                .put(
                        "identity_basis",
                        ObserverPlatform.provenance(
                                Build.VERSION.SDK_INT, "", "windowstate_identity_hash"))
                .put("source", "viewserver_legacy_dump")
                .put("package", window.ownerPackage == null ? JSONObject.NULL : window.ownerPackage)
                .put("frame", window.frame == null ? JSONObject.NULL : new JSONArray(window.frame))
                .put("layer", JSONObject.NULL)
                .put("bounds", window.frame == null ? JSONObject.NULL : new JSONArray(window.frame))
                .put("type", window.type == null ? JSONObject.NULL : window.type)
                .put("wm_order", window.order == null ? JSONObject.NULL : window.order)
                .put("wm_order_verified", window.order != null)
                .put("touchable", window.touchable == null ? JSONObject.NULL : window.touchable)
                .put("read_complete", false)
                .put("owner_uid", window.ownerUid == null ? JSONObject.NULL : window.ownerUid)
                .put(
                        "owner_package",
                        window.ownerPackage == null ? JSONObject.NULL : window.ownerPackage)
                .put(
                        "owner_provenance",
                        ObserverPlatform.provenance(
                                Build.VERSION.SDK_INT,
                                "",
                                "wm_windowstate_dump_not_shared_uid_inference"))
                .put("occlusion", "unknown")
                .put(
                        "unknown_fields",
                        new JSONArray()
                                .put("frame")
                                .put("z_order")
                                .put("input_region")
                                .put("owner_package"));
    }

    static JSONObject node(
            ViewServer.Window window,
            ViewDump.Node node,
            int slot,
            String resourceId,
            boolean bridge)
            throws Exception {
        String resource = ViewProperties.resource(node);
        boolean safeText = BridgeMetadata.textAllowed(node);
        String klass = node.identity.substring(0, node.identity.lastIndexOf('@'));

        return new JSONObject()
                .put("id", window.hash + ":" + node.identity)
                .put(
                        "parent",
                        node.parent == null ? JSONObject.NULL : window.hash + ":" + node.parent)
                .put("window_id", window.hash)
                .put("screen", slot == 0 ? "primary" : "secondary")
                .put("class", klass)
                .put("resource_id", resourceId == null ? JSONObject.NULL : resourceId)
                .put("resource_name", resource == null ? JSONObject.NULL : resource)
                .put(
                        "resource_package",
                        resourceId == null
                                ? JSONObject.NULL
                                : resourceId.substring(0, resourceId.indexOf(':')))
                .put(
                        "resource_provenance",
                        resourceId == null
                                ? "legacy_short_name_only"
                                : "pinned_framework_bridge_view_resources")
                .put("package", window.ownerPackage == null ? JSONObject.NULL : window.ownerPackage)
                .put("package_provenance", "windowmanager_owner_package")
                .put("text", BridgeMetadata.text(node))
                .put("description", "")
                .put("text_redacted", !safeText)
                .put("text_encoding", "legacy_dump_escaped_lf_cr")
                .put("editor_status", "unsupported")
                .put("selection_status", "unsupported")
                .put(
                        "redaction_reason",
                        safeText ? JSONObject.NULL : "bridge_privacy_verdict_missing_or_sensitive")
                .put("bridge_version", bridge ? 1 : JSONObject.NULL)
                .put("layout", new JSONObject(BridgeMetadata.layout(node)))
                .put("text_style", new JSONObject(BridgeMetadata.textStyle(node)))
                .put("text_layout", new JSONObject(BridgeMetadata.textLayout(node)))
                .put("image", new JSONObject(BridgeMetadata.image(node)))
                .put("background", new JSONObject(BridgeMetadata.background(node)))
                .put("bounds", JSONObject.NULL)
                .put("clipped_bounds", JSONObject.NULL)
                .put("present", true)
                .put("visible", JSONObject.NULL)
                .put("enabled", JSONObject.NULL)
                .put("clickable", JSONObject.NULL)
                .put("actionable", false)
                .put("actionable_basis", "unknown_window_transform_or_input_region")
                .put("occlusion", "unknown")
                .put("identity_scope", "this_observation_only")
                .put(
                        "unknown_fields",
                        new JSONArray()
                                .put("screen_bounds")
                                .put("visibility")
                                .put("resource_package")
                                .put("touchability"));
    }

    static void putStates(JSONObject n, ViewDump.Node node) throws Exception {
        String[] stateKeys = {
            "isLongClickable()", "isFocusable()", "isFocused()", "isSelected()", "isChecked()"
        };
        String[] stateNames = {"long_clickable", "focusable", "focused", "selected", "checked"};

        for (int i = 0; i < stateKeys.length; ++i) {
            Boolean state = ViewProperties.bool(node, stateKeys[i]);
            n.put(stateNames[i], state == null ? JSONObject.NULL : state);
        }

        Double alpha = ViewProperties.number(node, "drawing:getAlpha()");
        n.put("alpha", alpha == null ? JSONObject.NULL : alpha);
    }

    static String visibility(String visibility, boolean bridge) {
        if (bridge && "0".equals(visibility)) {
            return "VISIBLE";
        } else if (bridge && "4".equals(visibility)) {
            return "INVISIBLE";
        } else if (bridge && "8".equals(visibility)) {
            return "GONE";
        }

        return visibility;
    }

    /** Only local layout integers and booleans are allowed out of the raw DUMP. */
    static JSONObject rawLayout(ViewDump.Node node) throws Exception {
        JSONObject raw = new JSONObject();

        for (String key :
                new String[] {"mLeft", "mTop", "mRight", "mBottom", "mScrollX", "mScrollY"}) {
            String value =
                    node.properties.get(
                            (key.startsWith("mScroll") ? "scrolling:" : "layout:") + key);

            if (value != null && value.matches("-?[0-9]{1,9}")) {
                raw.put(key, Integer.parseInt(value));
            }
        }

        return raw;
    }
}
