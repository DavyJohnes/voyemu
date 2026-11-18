package com.voyemu.oem.media;

import org.apache.commons.logging.Log;

public final class UsbLogFactory implements Log {
    private final String tag;

    private UsbLogFactory(String tag) {
        this.tag = tag;
    }

    public static Log getLog(Class<?> type) {
        return new UsbLogFactory(type.getSimpleName());
    }

    public static Log getLog(String name) {
        return new UsbLogFactory(name);
    }

    private void write(int priority, Object message, Throwable error) {
        android.util.Log.println(
                priority,
                tag,
                String.valueOf(message)
                        + (error == null
                                ? ""
                                : "\n" + android.util.Log.getStackTraceString(error)));
    }

    public void trace(Object m) {
        write(android.util.Log.VERBOSE, m, null);
    }

    public void trace(Object m, Throwable t) {
        write(android.util.Log.VERBOSE, m, t);
    }

    public void debug(Object m) {
        write(android.util.Log.DEBUG, m, null);
    }

    public void debug(Object m, Throwable t) {
        write(android.util.Log.DEBUG, m, t);
    }

    public void info(Object m) {
        write(android.util.Log.INFO, m, null);
    }

    public void info(Object m, Throwable t) {
        write(android.util.Log.INFO, m, t);
    }

    public void warn(Object m) {
        write(android.util.Log.WARN, m, null);
    }

    public void warn(Object m, Throwable t) {
        write(android.util.Log.WARN, m, t);
    }

    public void error(Object m) {
        write(android.util.Log.ERROR, m, null);
    }

    public void error(Object m, Throwable t) {
        write(android.util.Log.ERROR, m, t);
    }

    public void fatal(Object m) {
        write(android.util.Log.ASSERT, m, null);
    }

    public void fatal(Object m, Throwable t) {
        write(android.util.Log.ASSERT, m, t);
    }

    public boolean isTraceEnabled() {
        return android.util.Log.isLoggable(tag, android.util.Log.VERBOSE);
    }

    public boolean isDebugEnabled() {
        return android.util.Log.isLoggable(tag, android.util.Log.DEBUG);
    }

    public boolean isInfoEnabled() {
        return android.util.Log.isLoggable(tag, android.util.Log.INFO);
    }

    public boolean isWarnEnabled() {
        return android.util.Log.isLoggable(tag, android.util.Log.WARN);
    }

    public boolean isErrorEnabled() {
        return android.util.Log.isLoggable(tag, android.util.Log.ERROR);
    }

    public boolean isFatalEnabled() {
        return android.util.Log.isLoggable(tag, android.util.Log.ASSERT);
    }
}
