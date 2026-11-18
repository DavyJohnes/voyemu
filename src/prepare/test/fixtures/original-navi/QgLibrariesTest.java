package com.voyemu.compat;

import com.android.server.pm.parsing.library.QgLibraries;
import com.android.server.pm.parsing.pkg.ParsedPackage;

import java.util.ArrayList;
import java.util.List;

public class QgLibrariesTest {
    static void ok(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static class P implements ParsedPackage {
        String name;
        List<String> req = new ArrayList<>(), opt = new ArrayList<>();
        int calls, fail = -1;

        P(String n) {
            name = n;
        }

        public String getPackageName() {
            return name;
        }

        public List<String> getUsesLibraries() {
            return req;
        }

        public List<String> getUsesOptionalLibraries() {
            return opt;
        }

        public ParsedPackage addUsesLibrary(int i, String s) {
            if (calls++ == fail) {
                throw new IllegalStateException("failure");
            }

            req.add(i, s);

            return this;
        }

        public ParsedPackage removeUsesOptionalLibrary(String s) {
            if (calls++ == fail) {
                throw new IllegalStateException("failure");
            }

            opt.remove(s);

            return this;
        }
    }

    public static void main(String[] x) {
        QgLibraries.apply(null);
        P p = new P("com.baidu.naviauto");
        p.req.add("existing");
        p.opt.addAll(List.of("optional", "QGAPI", "QGAPI"));
        QgLibraries.apply(p);
        ok(
                p.req.containsAll(List.of("existing", "QGMOD", "QGVehicle"))
                        && !p.req.contains("QGAPI")
                        && p.opt.equals(List.of("optional", "QGAPI", "QGAPI")));
        int n = p.calls;
        QgLibraries.apply(p);
        ok(p.calls == n);
        P other = new P("other");
        other.req.add("existing");
        QgLibraries.apply(other);
        ok(other.req.equals(List.of("existing")) && other.calls == 0);
        P fail = new P("com.baidu.naviauto");
        fail.fail = 1;

        try {
            QgLibraries.apply(fail);
            throw new AssertionError("exception swallowed");
        } catch (IllegalStateException expected) {
            ok(fail.req.size() == 1);
        }

        System.out.println(
                "PASS scope, preserve, preserve explicit optional, idempotence, failure propagates"
                        + " (partial mutation documented)");
    }
}
