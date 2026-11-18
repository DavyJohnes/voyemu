package com.voyemu.prepare;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import javax.tools.ToolProvider;

public final class PassionSidebarWindowTest {
    public static void main(String[] args) throws Exception {
        Path source = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);
        Map<String, String> fixtures =
                Map.of(
                        "android/os/Build.java",
                        "package android.os; public final class Build {public static class VERSION"
                                + " {public static int SDK_INT=30;}}",
                        "android/view/WindowManager.java",
                        "package android.view; public interface WindowManager {public static class"
                            + " LayoutParams {public int"
                            + " type=2038,width=140,height=-1,gravity=8388611,flags=8650792,softInputMode=16,format=-3,fit=15;public"
                            + " String packageName=\"com.voyah.os.carlauncher\";public CharSequence"
                            + " title=\"ly-navigationBar\";public Object token=new Object();public"
                            + " int[] providesInsetsTypes;public CharSequence getTitle(){return"
                            + " title;}public void setFitInsetsTypes(int value){fit=value;}}}",
                        "com/voyemu/output/SidebarContract.java",
"""
package com.voyemu.output;
import android.os.Build;import android.view.WindowManager.LayoutParams;
public final class SidebarContract {
 static void check(boolean b){if(!b)throw new AssertionError("Dock source, geometry or negative guard changed");}
 public static void run(){
  for(int mode=0;mode<12;mode++){
   LayoutParams p=new LayoutParams();Object token=p.token;int[] existing={7};p.providesInsetsTypes=existing;
   if(mode==1){p.title="ly-navigationBar-second";p.gravity=8388613;}
   if(mode==2)p.packageName="com.android.systemui";
   if(mode==3)p.type=2019;
   if(mode==4)p.title="ly-navigationBar-other";
   if(mode==5)p.width=141;
   if(mode==6)p.height=720;
   if(mode==7)p.gravity=8388613;
   if(mode==8)p.title=null;
   if(mode==9)Build.VERSION.SDK_INT=31;
   if(mode==10)p.packageName=null;
   if(mode==11){p.title="ly-navigationBar-second";p.gravity=8388611;}
   int type=p.type,width=p.width,height=p.height,gravity=p.gravity;
   PassionSidebarWindow.configure(p);boolean target=mode<2;
   check(p.type==type&&p.width==width&&p.height==height&&p.gravity==gravity&&p.flags==8650792&&p.softInputMode==16&&p.format==-3&&p.token==token);
   check(p.fit==(target?0:15));check(target?ArraysEqual(p.providesInsetsTypes,new int[]{1}):p.providesInsetsTypes==existing);
   if(target){PassionSidebarWindow.configure(p);check(p.fit==0&&ArraysEqual(p.providesInsetsTypes,new int[]{1}));}
   Build.VERSION.SDK_INT=30;
  }
  PassionSidebarWindow.configure(null);
 }
 static boolean ArraysEqual(int[] a,int[] b){return java.util.Arrays.equals(a,b);}
}
""");
        var command =
                new ArrayList<String>(
                        List.of("--release", "25", "-d", work.resolve("classes").toString()));

        for (var e : fixtures.entrySet()) {
            Path file = work.resolve(e.getKey());
            Files.createDirectories(file.getParent());
            Files.writeString(file, e.getValue());
            command.add(file.toString());
        }

        command.add(source.toString());

        if (ToolProvider.getSystemJavaCompiler()
                        .run(null, null, null, command.toArray(String[]::new))
                != 0) {
            throw new AssertionError("Actual helper compile failed");
        }

        try (var loader =
                new URLClassLoader(
                        new java.net.URL[] {work.resolve("classes").toUri().toURL()}, null)) {
            loader.loadClass("com.voyemu.output.SidebarContract").getMethod("run").invoke(null);
        }

        System.out.println(
                "PASS genuine START/END dock source, repeated configuration, preserved OEM"
                        + " geometry/token/flags, 10 negative guards and null; runtime insets not"
                        + " inferred");
    }
}
