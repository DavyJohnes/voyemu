#ifndef VOYEMU_PASSION_HELPER_DISPATCH_H
#define VOYEMU_PASSION_HELPER_DISPATCH_H
/* Each helper is opt-in; bounded-wait dispatch keeps priority and its immediate exit. */
#define QGFX_HELPER_DISPATCH()                                                                     \
    do {                                                                                           \
        qbw_start();                                                                               \
        qnet_start();                                                                              \
    } while (0)
#endif
