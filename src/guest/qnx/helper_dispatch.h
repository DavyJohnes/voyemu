#ifndef VOYEMU_QNX_HELPER_DISPATCH_H
#define VOYEMU_QNX_HELPER_DISPATCH_H
/* Front-module helper dispatch precedes every initializer side effect.
 * Standalone observer/capture modules retain their existing initialization. */
#ifndef QGFX_HELPER_DISPATCH
#define QGFX_HELPER_DISPATCH() ((void)0)
#endif
#ifndef QGFX_INITIALIZER
#define QGFX_INITIALIZER __attribute__((constructor))
#endif
#endif
