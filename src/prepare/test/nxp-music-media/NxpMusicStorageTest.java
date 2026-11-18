import android.content.Context;
import android.os.Environment;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import com.qinggan.app.music.model.ModelManager;
import com.qinggan.music.audiocom.External;
import com.qinggan.music.bean.Module;
import com.qinggan.music.dao.MusicDAOImpl;
import com.voyemu.output.NxpMusicStorage;

public final class NxpMusicStorageTest {
    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) {
        Context context = new Context();
        View root = new View(context);
        ViewGroup tabs = new ViewGroup(context);

        NxpMusicStorage.attach(root);
        NxpMusicStorage.select(context);
        need(
                root.observer.listeners.isEmpty() && MusicDAOImpl.queries == 0,
                "unmounted storage exposed");
        Environment.state = Environment.MEDIA_MOUNTED;
        NxpMusicStorage.attach(root);
        root.observer.layout();
        need(
                root.observer.listeners.size() == 1 && tabs.children.isEmpty(),
                "did not wait for OEM tab");
        View tab = new View(context);
        root.tab = tab;
        tab.parent = tabs;
        root.observer.layout();
        need(root.observer.listeners.size() == 1, "did not wait for measured OEM tab");
        tab.width = 120;
        tab.height = 48;
        tab.x = 30;
        tab.y = 15;
        root.observer.layout();
        root.observer.layout();
        need(
                root.observer.listeners.isEmpty()
                        && root.attachListeners.isEmpty()
                        && tabs.children.size() == 1,
                "entry duplicated or listener leaked");
        TextView entry = (TextView) tabs.children.get(0);
        need(
                entry.text.toString().equals("External storage")
                        && entry.description
                                .toString()
                                .equals("External storage: /storage/emulated/0"),
                "shared storage mislabeled");
        need(
                entry.x == 160 && entry.y == 15 && entry.width == 160 && entry.height == 48,
                "entry not adjacent to OEM tab");
        Module.LABLE_USB.childModule = new Module();
        entry.click.onClick(entry);
        need(
                MusicDAOImpl.queries == 1
                        && External.USB.entries == MusicDAOImpl.TRACKS
                        && External.USB.entries.size() == 2,
                "real DAO entries replaced");
        need(
                External.INTERNAL.entries == null && Module.LABLE_USB.childModule == null,
                "internal storage queried or stale USB child retained");
        need(
                ModelManager.INSTANCE.selected == External.USB.playlist
                        && External.USB.playlist.module == Module.LABLE_USB,
                "OEM playlist controller identity changed");
        MusicDAOImpl.failure = new IllegalArgumentException("provider failed");

        try {
            NxpMusicStorage.select(context);
            throw new AssertionError("DAO failure swallowed");
        } catch (IllegalStateException failure) {
            need(failure.getCause() == MusicDAOImpl.failure, "original failure lost");
        }

        View abandoned = new View(context);
        NxpMusicStorage.attach(abandoned);
        abandoned.detach();
        need(
                abandoned.observer.listeners.isEmpty() && abandoned.attachListeners.isEmpty(),
                "detached fragment retains pending listeners");
        View unmounted = new View(context);
        unmounted.tab = tab;
        NxpMusicStorage.attach(unmounted);
        Environment.state = "unmounted";
        unmounted.observer.layout();
        need(
                tabs.children.size() == 1
                        && unmounted.observer.listeners.isEmpty()
                        && unmounted.attachListeners.isEmpty(),
                "storage removed before layout was exposed");
        System.out.println(
                "PASS shared storage: mounted guard, deferred adjacent entry, real USB DAO"
                        + " entries/controller identity, stale child reset, provider failure"
                        + " preserved");
    }
}
