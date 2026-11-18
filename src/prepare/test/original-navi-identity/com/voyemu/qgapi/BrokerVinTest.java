package com.voyemu.qgapi;

public class BrokerVinTest {
    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static class E implements BrokerVin.Environment {
        int uid = 10130, calls;
        String[] packages = {"com.baidu.naviauto"};
        String value = "genuine";
        boolean fail;

        public int uid() {
            return uid;
        }

        public String[] packages(int u) {
            return packages;
        }

        public String broker() throws Exception {
            calls++;

            if (fail) {
                throw new Exception();
            }

            return value;
        }
    }

    public static void main(String[] a) {
        E e = new E();

        check("OEM".equals(BrokerVin.select("OEM", e)) && e.calls == 0);
        check("genuine".equals(BrokerVin.select(null, e)) && e.calls == 1);
        e.value = "";
        check(BrokerVin.select(null, e) == null);
        e.value = null;
        check("".equals(BrokerVin.select("", e)));
        e.fail = true;
        check(BrokerVin.select(null, e) == null);
        e.fail = false;
        e.uid = 1000;
        int n = e.calls;
        check(BrokerVin.select(null, e) == null && e.calls == n);
        e.uid = 10130;
        e.packages = new String[] {"com.baidu.naviauto", "other"};
        check(BrokerVin.select(null, e) == null && e.calls == n);
        e.packages = new String[] {"other"};
        check(BrokerVin.select(null, e) == null && e.calls == n);
        e.packages = null;
        check(BrokerVin.select(null, e) == null && e.calls == n);
        System.out.println("PASS OEM preservation, scoped caller, empty/error and recursion guard");
    }
}
