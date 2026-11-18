#ifdef _WIN32
#define VOYEMU_LIBRARY_EXPORT __declspec(dllexport)
#else
#define VOYEMU_LIBRARY_EXPORT __attribute__((visibility("default")))
#endif

extern "C" VOYEMU_LIBRARY_EXPORT int voyemu_library_fixture() {
    return 73;
}
