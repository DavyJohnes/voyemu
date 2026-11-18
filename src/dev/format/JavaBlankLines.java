// Blank lines between Java statements: around if, for, while, do, switch, try and synchronized,
// before a return that is not first in its block and after the declarations at the top of a
// method, as Uncrustify does for C and C++ except before try. Only blank lines are added;
// google-java-format runs next.
// Usage: java src/dev/format/JavaBlankLines.java --check|--fix FILE...
import com.sun.source.tree.BlockTree;
import com.sun.source.tree.CaseTree;
import com.sun.source.tree.CompilationUnitTree;
import com.sun.source.tree.LineMap;
import com.sun.source.tree.MethodTree;
import com.sun.source.tree.ReturnTree;
import com.sun.source.tree.StatementTree;
import com.sun.source.tree.Tree;
import com.sun.source.tree.VariableTree;
import com.sun.source.util.JavacTask;
import com.sun.source.util.SourcePositions;
import com.sun.source.util.TreeScanner;
import com.sun.source.util.Trees;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import javax.tools.JavaCompiler;
import javax.tools.JavaFileObject;
import javax.tools.StandardJavaFileManager;
import javax.tools.ToolProvider;

@SuppressWarnings("DefaultPackage") // A single-file program run with `java FILE`.
public final class JavaBlankLines {
    private static final Set<Tree.Kind> CONTROL =
            Set.of(
                    Tree.Kind.IF,
                    Tree.Kind.FOR_LOOP,
                    Tree.Kind.ENHANCED_FOR_LOOP,
                    Tree.Kind.WHILE_LOOP,
                    Tree.Kind.DO_WHILE_LOOP,
                    Tree.Kind.SWITCH,
                    Tree.Kind.TRY,
                    Tree.Kind.SYNCHRONIZED);

    private JavaBlankLines() {}

    public static void main(String[] args) throws IOException {
        if (args.length < 2 || !(args[0].equals("--check") || args[0].equals("--fix"))) {
            System.err.println("usage: JavaBlankLines --check|--fix FILE...");
            System.exit(2);
        }

        boolean fix = args[0].equals("--fix");
        List<String> changed = new ArrayList<>();

        for (int i = 1; i < args.length; i++) {
            Path file = Path.of(args[i]);
            String source = Files.readString(file, StandardCharsets.UTF_8);
            String result = format(source, file.toString());

            if (!result.equals(source)) {
                changed.add(args[i]);

                if (fix) {
                    Files.writeString(file, result, StandardCharsets.UTF_8);
                }
            }
        }

        if (!fix && !changed.isEmpty()) {
            for (String name : changed) {
                System.err.println(name + ": missing blank lines between statements");
            }

            System.exit(1);
        }
    }

    /**
     * Returns the source with the missing blank lines added; unparsable sources stay as they are.
     */
    static String format(String source, String name) throws IOException {
        JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();

        try (StandardJavaFileManager files = compiler.getStandardFileManager(null, null, null)) {
            JavaFileObject object = new Source(name, source);
            JavacTask task =
                    (JavacTask)
                            compiler.getTask(
                                    null,
                                    files,
                                    diagnostic -> {},
                                    List.of("-proc:none"),
                                    null,
                                    List.of(object));
            CompilationUnitTree unit = task.parse().iterator().next();
            Rules rules = new Rules(unit, Trees.instance(task).getSourcePositions(), source);
            rules.scan(unit, null);

            return insert(source, rules.after);
        }
    }

    private static String insert(String source, Set<Integer> afterLines) {
        if (afterLines.isEmpty()) {
            return source;
        }

        String[] lines = source.split("\n", -1);
        StringBuilder out = new StringBuilder(source.length() + afterLines.size());

        for (int i = 0; i < lines.length; i++) {
            out.append(lines[i]);

            if (i + 1 < lines.length) {
                out.append('\n');

                if (afterLines.contains(i + 1)) {
                    out.append('\n');
                }
            }
        }

        return out.toString();
    }

    private static final class Rules extends TreeScanner<Void, Void> {
        private final CompilationUnitTree unit;
        private final SourcePositions positions;
        private final LineMap lines;
        private final String[] text;
        private final TreeSet<Integer> after = new TreeSet<>();

        Rules(CompilationUnitTree unit, SourcePositions positions, String source) {
            this.unit = unit;
            this.positions = positions;
            this.lines = unit.getLineMap();
            this.text = source.split("\n", -1);
        }

        @Override
        public Void visitMethod(MethodTree method, Void unused) {
            if (method.getBody() != null) {
                topDeclarations(method.getBody().getStatements());
            }

            return super.visitMethod(method, null);
        }

        @Override
        public Void visitBlock(BlockTree block, Void unused) {
            statements(block.getStatements());

            return super.visitBlock(block, null);
        }

        @Override
        public Void visitCase(CaseTree item, Void unused) {
            if (item.getStatements() != null) {
                statements(item.getStatements());
            }

            return super.visitCase(item, null);
        }

        private void topDeclarations(List<? extends StatementTree> body) {
            int count = 0;

            while (count < body.size() && body.get(count) instanceof VariableTree) {
                count++;
            }

            if (count > 0 && count < body.size()) {
                separate(body.get(count - 1), body.get(count));
            }
        }

        private void statements(List<? extends StatementTree> body) {
            for (int i = 1; i < body.size(); i++) {
                StatementTree previous = body.get(i - 1);
                StatementTree current = body.get(i);

                if (CONTROL.contains(previous.getKind())
                        || CONTROL.contains(current.getKind())
                        || current instanceof ReturnTree) {
                    separate(previous, current);
                }
            }
        }

        /** Adds a blank line after the previous statement unless one already separates them. */
        private void separate(StatementTree previous, StatementTree current) {
            long end = positions.getEndPosition(unit, previous);
            long start = positions.getStartPosition(unit, current);

            if (end < 0 || start < 0) {
                return;
            }

            int last = (int) lines.getLineNumber(end);
            int first = (int) lines.getLineNumber(start);

            if (first <= last) {
                return;
            }

            for (int line = last + 1; line < first; line++) {
                if (text[line - 1].isBlank()) {
                    return;
                }
            }

            after.add(last);
        }
    }

    private static final class Source extends javax.tools.SimpleJavaFileObject {
        private final String text;

        Source(String name, String text) {
            super(Path.of(name).toUri(), Kind.SOURCE);
            this.text = text;
        }

        @Override
        public CharSequence getCharContent(boolean ignoreEncodingErrors) {
            return text;
        }
    }
}
