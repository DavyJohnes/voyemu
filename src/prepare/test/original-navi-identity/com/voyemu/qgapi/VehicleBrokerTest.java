package com.voyemu.qgapi;

public class VehicleBrokerTest {
    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static class E implements VehicleBroker.Environment {
        int uid = 10130, calls, value = 134;
        String[] p = {"com.baidu.naviauto"};
        boolean fail;

        public int uid() {
            return uid;
        }

        public String[] packages(int u) {
            return p;
        }

        public int broker() throws Exception {
            calls++;

            if (fail) {
                throw new Exception();
            }

            return value;
        }
    }

    public static void main(String[] a) {
        E e = new E();

        check(VehicleBroker.lookup(e) == 134 && e.calls == 1);
        e.value = 135;
        check(VehicleBroker.lookup(e) == 0);
        e.value = 0;
        check(VehicleBroker.lookup(e) == 0);
        e.fail = true;
        check(VehicleBroker.lookup(e) == 0);
        e.fail = false;
        int n = e.calls;
        e.uid = 1000;
        check(VehicleBroker.lookup(e) == 0 && e.calls == n);
        e.uid = 10130;
        e.p = new String[] {"com.baidu.naviauto", "other"};
        check(VehicleBroker.lookup(e) == 0 && e.calls == n);
        e.p = new String[] {"other"};
        check(VehicleBroker.lookup(e) == 0 && e.calls == n);
        e.p = null;
        check(VehicleBroker.lookup(e) == 0 && e.calls == n);
        System.out.println("PASS vehicle authorised, zero/other/error and system recursion skip");
    }
}
