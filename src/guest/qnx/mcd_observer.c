/* Keep the writable load segment file-backed for the OEM loader. */
static volatile unsigned observer_file_data __attribute__((used)) = 1;
/* Dedicated non-graphics preload: forward OEM Mcdif, export only observations. */
#include "observer_transport.inc"
extern void *dlopen(const char *, int), *dlsym(void *, const char *);
extern char *getenv(const char *);
extern int getpid(void);
extern int pthread_create(int *, const void *, void *(*)(void *), void *),
        pthread_join(int, void **);
#include "mcd_trace.c"
#include "setting_observe.c"
#include "setting_trace.c"
#define QGFX_SETTING_OBSERVER observe_setting_state
#include "mcd_observer_worker.c"
