/*
 * Minimal <log/log.h> for the voyemu stub build.
 *
 * The stub links against the guest /system/lib64/liblog.so, so only the
 * declarations needed at compile time live here; the full Android liblog
 * header tree is not shipped with the stub.
 */

#ifndef _LIBS_LOG_LOG_H
#define _LIBS_LOG_LOG_H

#include <android/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LOG_TAG
#define LOG_TAG NULL
#endif

#define ALOG(priority, tag, ...) \
    __android_log_print(ANDROID_##priority, tag, __VA_ARGS__)

#define ALOGV(...) ((void)ALOG(LOG_VERBOSE, LOG_TAG, __VA_ARGS__))
#define ALOGD(...) ((void)ALOG(LOG_DEBUG, LOG_TAG, __VA_ARGS__))
#define ALOGI(...) ((void)ALOG(LOG_INFO, LOG_TAG, __VA_ARGS__))
#define ALOGW(...) ((void)ALOG(LOG_WARN, LOG_TAG, __VA_ARGS__))
#define ALOGE(...) ((void)ALOG(LOG_ERROR, LOG_TAG, __VA_ARGS__))

#define LOG_ALWAYS_FATAL_IF(cond, ...)                                      \
    ((cond) ? __android_log_assert("cond", LOG_TAG, __VA_ARGS__) : (void)0)

#define LOG_FATAL_IF(cond, ...) LOG_ALWAYS_FATAL_IF(cond, ##__VA_ARGS__)

#define LOG_ALWAYS_FATAL(...)                                     \
    (__android_log_assert(nullptr, LOG_TAG, __VA_ARGS__))

#define LOG_FATAL(...) LOG_ALWAYS_FATAL(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // _LIBS_LOG_LOG_H
