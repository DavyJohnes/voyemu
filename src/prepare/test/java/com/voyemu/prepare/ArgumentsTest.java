package com.voyemu.prepare;

public final class ArgumentsTest {
    public static void main(String[] args) {
        Arguments a =
                Arguments.parse(
                        new String[] {
                            "--home", "/tmp/voyemu", "start", "free-09", "--with-cluster", "--json"
                        });

        StorageJsonTest.check(
                a.command().equals("start")
                        && a.profile().equals("free-09")
                        && a.flag("--with-cluster"));

        for (String[] bad :
                new String[][] {
                    {"install", "free"},
                    {"fetch", "../escape"},
                    {"start", "--force"},
                    {"--home"},
                    {"start", "--wat"},
                    {"start", "--json", "--json"},
                    {"fetch", "--serial", "car", "--input", "/tmp"}
                }) {
            StorageJsonTest.rejected(() -> Arguments.parse(bad));
        }

        System.out.println("CLI argument guards passed");
    }
}
