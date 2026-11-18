/* QNX pthread_t is _Int32t in the pinned OEM DWARF. */
extern int pthread_create(int *, const void *, void *(*)(void *), void *);
extern int pthread_join(int, void **);
static int theme_thread;
static unsigned char theme_started;
static unsigned theme_created, theme_stop;

static void *theme_worker(void *unused) {
    (void)unused;
    const char *profile = getenv("QGFX_INPUT_PROFILE");
    const int trace = profile && profile[0] == '1' && !profile[1];
    const char *mcu = getenv("QGFX_MCU_RELAY"), *wheel = getenv("QGFX_WHEEL_RELAY");
    const int eligible = (mcu && mcu[0] == '1' && !mcu[1]) ||
                         (wheel && wheel[0] == '1' && !wheel[1]);
    unsigned iteration = 0, interactive = mcu_delivered > 1 || wheel_delivered;

    while (!__atomic_load_n(&theme_stop, __ATOMIC_ACQUIRE)) {
#ifdef QGFX_KANZI_UI
        kanzi_observer_heartbeat();
#endif
        const int fast = eligible && interactive;

        if (!fast || iteration % 10 == 0) {
            if (trace) {
                char text[96];
                snprintf(text, sizeof(text), "QIN iteration=%u phase=theme level=DEBUG\n",
                         iteration);
                emit(text);
            }

            relay_theme();
        }

        if (trace) {
            char text[96];
            snprintf(text, sizeof(text), "QIN iteration=%u phase=mcu level=DEBUG\n", iteration);
            emit(text);
        }

        relay_mcu_input();

        if (trace) {
            char text[96];
            snprintf(text, sizeof(text), "QIN iteration=%u phase=wheel level=DEBUG\n", iteration);
            emit(text);
        }

        relay_wheel_menu();

        if (!fast || iteration % 10 == 0) {
            if (trace) {
                char text[96];
                snprintf(text, sizeof(text), "QIN iteration=%u phase=observe level=DEBUG\n",
                         iteration);
                emit(text);
            }

            drain_mcd_trace();
            relay_dlt_tail();
        }

        /* Initial state is preparation, not interactive input. Avoid competing with OEM asset
         * loading. */
        if (!interactive && (mcu_delivered > 1 || wheel_delivered)) {
            interactive = 1;
            iteration = 0;
        }

        if (trace) {
            char text[96];
            snprintf(text, sizeof(text), "QIN iteration=%u phase=sleep level=DEBUG\n", iteration);
            emit(text);
        }

        if (eligible) {
            input_wake_advertise();
        }

        input_wait(eligible && interactive ? 20000 : 200000, &theme_stop);
        iteration++;
    }

    return 0;
}

static void start_theme_worker(void) {
    const char *flag = getenv("QGFX_THEME_RELAY");

    if (!flag || flag[0] != '1' || flag[1] ||
        __atomic_test_and_set(&theme_started, __ATOMIC_ACQUIRE)) {
        return;
    }

    int rc = pthread_create(&theme_thread, 0, theme_worker, 0);

    if (rc) {
        __atomic_clear(&theme_started, __ATOMIC_RELEASE);
        emit("QG3 THEME worker-start-failed\n");

        return;
    }

    __atomic_store_n(&theme_created, 1, __ATOMIC_RELEASE);
}

__attribute__((destructor)) static void stop_theme_worker(void) {
    __atomic_store_n(&theme_stop, 1, __ATOMIC_RELEASE);

    if (__atomic_exchange_n(&theme_created, 0, __ATOMIC_ACQ_REL)) {
        pthread_join(theme_thread, 0);
    }
}
