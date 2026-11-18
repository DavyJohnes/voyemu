package android.app;

public class ActivityManager {
    public static class RunningAppProcessInfo {
        public int pid, uid;
        public String processName;
        public String[] pkgList;
    }

    public java.util.List<RunningAppProcessInfo> processes = new java.util.ArrayList<>();

    public java.util.List<RunningAppProcessInfo> getRunningAppProcesses() {
        return processes;
    }
}
