package android.util;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

public class AtomicFile {
    private final File base, pending;
    public static volatile boolean failNextWrite;

    public AtomicFile(File file) {
        base = file;
        pending = new File(file.getPath() + ".new");
    }

    public FileInputStream openRead() throws FileNotFoundException {
        return new FileInputStream(base);
    }

    public FileOutputStream startWrite() throws IOException {
        if (failNextWrite) {
            failNextWrite = false;
            throw new IOException("test persistence failure");
        }

        return new FileOutputStream(pending);
    }

    public void finishWrite(FileOutputStream stream) throws IOException {
        stream.getFD().sync();
        stream.close();
        java.nio.file.Files.move(
                pending.toPath(),
                base.toPath(),
                java.nio.file.StandardCopyOption.ATOMIC_MOVE,
                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }

    public void failWrite(FileOutputStream stream) {
        try {
            stream.close();
        } catch (IOException ignored) {
            // Closing after a failed write has nothing left to report.
        }

        pending.delete();
    }
}
