import java.nio.file.Files;
import java.nio.file.Path;

/** Generates a blank emulator fixture for the early-inputs size gate. */
public final class GenerateHybridFixture {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("expected output path");
        }

        Files.write(Path.of(args[0]), new byte[0x22000]);
    }
}
