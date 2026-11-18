package com.voyemu.prepare;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import javax.tools.ToolProvider;

public final class PassionStatusBarWindowTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    public static void main(String[] args) throws Exception {
        Path source = Path.of(args[0]), work = Path.of(args[1]);

        Files.createDirectories(work);
        Map<String, String> fixtures =
                Map.of(
                        "android/content/Context.java",
                        "package android.content; public class Context {public final String owner;"
                                + " public Context(String x){owner=x;} public String"
                                + " getPackageName(){return owner;}}",
                        "android/view/View.java",
                        "package android.view; import android.content.Context; public class View {"
                            + " public static final int GONE=8; public int visibility=0; public"
                            + " final Context context; public View(String owner){context=new"
                            + " Context(owner);} public Context getContext(){return context;}public"
                            + " void setVisibility(int v){visibility=v;}}",
                        "android/view/ViewGroup.java",
                        "package android.view; public class ViewGroup { public static class"
                                + " LayoutParams {public int width=1920,height=24;} }",
                        "android/view/WindowManager.java",
                        "package android.view; public interface WindowManager {void addView(View"
                                + " v,ViewGroup.LayoutParams p);void updateViewLayout(View"
                                + " v,ViewGroup.LayoutParams p);public static class LayoutParams"
                                + " extends ViewGroup.LayoutParams {public int type=2000;public"
                                + " CharSequence title=\"StatusBar\";public CharSequence"
                                + " getTitle(){return title;}}}",
                        "com/voyemu/output/RecordingWindowManager.java",
"""
package com.voyemu.output;
import android.view.*;
public class RecordingWindowManager implements WindowManager {
 public int add,update;public View seen;public ViewGroup.LayoutParams params;
 public final java.util.Map<View,Integer> attached=new java.util.IdentityHashMap<>();public View statusBar;
 public void addView(View v,ViewGroup.LayoutParams p){
  if(attached.containsKey(v))throw new IllegalArgumentException("Already attached");
  int type=p instanceof WindowManager.LayoutParams?((WindowManager.LayoutParams)p).type:0;
  if(type==2000&&statusBar!=null)throw new IllegalArgumentException("Another window of type 2000 already exists");
  if(type==2000)statusBar=v;attached.put(v,type);add++;seen=v;params=p;
 }
 public void updateViewLayout(View v,ViewGroup.LayoutParams p){
  if(!attached.containsKey(v))throw new IllegalArgumentException("Not attached");
  int type=p instanceof WindowManager.LayoutParams?((WindowManager.LayoutParams)p).type:0;
  if(attached.get(v)!=type)throw new IllegalArgumentException("Window type cannot change after add");
  update++;seen=v;params=p;
 }
 static void check(boolean b){if(!b)throw new AssertionError("Lifecycle, slot ownership or OEM geometry changed");}
 static void ownership(){
  RecordingWindowManager manager=new RecordingWindowManager();View stock=new View("com.android.systemui");WindowManager.LayoutParams hidden=new WindowManager.LayoutParams();
  PassionStatusBarWindow.addView(manager,stock,hidden);
  View oem=new View("com.voyah.os.carlauncher");WindowManager.LayoutParams genuine=new WindowManager.LayoutParams();genuine.title="ly-statusBar";
  PassionStatusBarWindow.addView(manager,oem,genuine);
  check(manager.statusBar==oem&&manager.attached.get(stock)==2041&&manager.attached.get(oem)==2000);
  for(int i=0;i<3;i++){hidden.type=2000;hidden.height=24;PassionStatusBarWindow.updateViewLayout(manager,stock,hidden);check(hidden.type==2041&&hidden.height==0&&stock.visibility==8);}
  PassionStatusBarWindow.updateViewLayout(manager,oem,genuine);check(genuine.type==2000&&genuine.height==24&&oem.visibility==0&&manager.add==2&&manager.update==4);
 }
 public static void run(){
  ownership();
  for(int mode=0;mode<10;mode++){
   RecordingWindowManager manager=new RecordingWindowManager();View view=new View(mode==1?"com.qinggan.systemui":mode==6?"com.voyah.os.carlauncher":"com.android.systemui");
   WindowManager.LayoutParams window=new WindowManager.LayoutParams();if(mode==2)window.type=2118;if(mode==3)window.title="StatusBar1";if(mode==4)window.title=null;if(mode==6)window.title="ly-statusBar";if(mode==7)window.type=2041;if(mode==8){window.type=2041;window.title="Other";}if(mode==9){window.type=2040;}
   ViewGroup.LayoutParams params=mode==5?new ViewGroup.LayoutParams():window;boolean target=mode==0||mode==7;
   PassionStatusBarWindow.addView(manager,view,params);PassionStatusBarWindow.updateViewLayout(manager,view,params);
   check(manager.seen==view&&manager.params==params&&manager.add==1&&manager.update==1&&manager.attached.containsKey(view));
   check(params.width==1920&&params.height==(target?0:24)&&view.visibility==(target?8:0));
   if(params==window)check(window.type==(target||mode==8?2041:mode==2?2118:mode==9?2040:2000));
  }
 }
}
""");
        var command =
                new ArrayList<String>(
                        List.of("--release", "25", "-d", work.resolve("classes").toString()));

        for (var e : fixtures.entrySet()) {
            Path p = work.resolve(e.getKey());
            Files.createDirectories(p.getParent());
            Files.writeString(p, e.getValue());
            command.add(p.toString());
        }

        command.add(source.toString());
        check(
                ToolProvider.getSystemJavaCompiler()
                                .run(null, null, null, command.toArray(String[]::new))
                        == 0,
                "Compile actual helper against recording public API");

        try (var loader =
                new URLClassLoader(
                        new java.net.URL[] {work.resolve("classes").toUri().toURL()}, null)) {
            loader.loadClass("com.voyemu.output.RecordingWindowManager")
                    .getMethod("run")
                    .invoke(null);
        }

        System.out.println(
                "Actual helper: stock additional window keeps lifecycle; genuine OEM owns exclusive"
                        + " status-bar slot; 8 negative guards unchanged");
    }
}
