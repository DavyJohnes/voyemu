package android.app;

import android.os.PeerTestRuntime;

import java.util.ArrayList;
import java.util.List;

public class ActivityManager {
    public static class RunningAppProcessInfo {
        public int uid, pid;
        public String processName;
        public String[] pkgList;
    }

    public List<RunningAppProcessInfo> getRunningAppProcesses() {
        List<RunningAppProcessInfo> result = new ArrayList<RunningAppProcessInfo>();

        for (PeerTestRuntime.Identity id :
                new PeerTestRuntime.Identity[] {PeerTestRuntime.PHONE, PeerTestRuntime.LAUNCHER}) {
            RunningAppProcessInfo p = new RunningAppProcessInfo();
            p.uid = id.uid;
            p.pid = id.pid;
            p.processName = id.name;
            p.pkgList = new String[] {id.name};
            result.add(p);
        }

        return result;
    }
}
