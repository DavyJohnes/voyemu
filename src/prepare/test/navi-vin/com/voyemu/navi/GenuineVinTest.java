package com.voyemu.navi;

import android.content.Context;
import android.os.Bundle;

import com.baidu.mapautosdk.util.ContextHolder;
import com.qinggan.os.SystemProperties;

public class GenuineVinTest {
    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        SystemProperties.vin = "genuine-property";
        check(GenuineVin.selectContent("other", null) == null && SystemProperties.calls == 0);
        Object other = new Object();
        check(GenuineVin.selectContent("vin", other) == other && SystemProperties.calls == 0);
        check(GenuineVin.select("oem-input").equals("oem-input") && SystemProperties.calls == 0);
        check(GenuineVin.selectContent("vin", null).equals("genuine-property"));
        SystemProperties.vin = "";
        check(GenuineVin.select(null) == null && GenuineVin.select("").equals(""));
        Context c = new Context();
        ContextHolder.context = c;
        Bundle b = new Bundle();
        b.value = "broker-genuine";
        c.resolver.result = b;
        check(GenuineVin.select(null).equals("broker-genuine"));
        check(c.resolver.calls == 1);
        b.value = "";
        check(GenuineVin.select("").equals(""));
        c.resolver.fail = true;
        check(GenuineVin.select(null) == null);
        SystemProperties.fail = true;
        check(GenuineVin.select("").equals(""));
        System.out.println(
                "PASS original, genuine getter, caller-bound broker and fail-closed cases");
    }
}
