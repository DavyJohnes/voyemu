package android.app;

import java.util.List;

public abstract class ActivityManager {
    public static final class RunningAppProcessInfo {
        public int pid, uid;
        public String processName;
        public String[] pkgList;
    }

    public abstract List<RunningAppProcessInfo> getRunningAppProcesses();
}
