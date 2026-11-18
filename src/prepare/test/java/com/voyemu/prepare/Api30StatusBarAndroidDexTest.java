package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.nio.file.Path;
import java.util.List;
import java.util.TreeSet;

public final class Api30StatusBarAndroidDexTest {
    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), out = Path.of(a[1]), platform = Path.of(a[2]);
        Path source =
                root.resolve(
                        "profiles/free-11-chinese-20260731-7.3.1/patches/systemui-routing/src");
        Path dex = DexOperations.javaDex(source, null, List.of(), platform, out, 30);
        var classes = new TreeSet<String>();

        for (var c :
                DexFileFactory.loadDexFile(dex.resolve("classes.dex").toFile(), Opcodes.forApi(30))
                        .getClasses()) {
            classes.add(c.getType());
        }

        if (!classes.contains(SystemUiRouting.ROUTER)
                || classes.size() > 8
                || classes.stream()
                        .anyMatch(x -> !x.startsWith("Lcom/voyemu/output/StatusBarRouter"))) {
            throw new AssertionError("Only own status-bar classes may enter helper DEX");
        }

        System.out.println("API30 actual Android classpath+D8 PASS " + classes);
    }
}
