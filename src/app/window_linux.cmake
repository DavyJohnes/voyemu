# The X11 and Wayland drivers of SDL3 on Linux. SDL loads the system X11, Wayland, xkbcommon and
# libdecor libraries at run time by their sonames, so the build needs only their headers, a
# wayland-scanner for the protocol code and the sonames. The header sources are pinned in
# platform/component.json under window_linux; nothing is taken from the build host.
include_guard(GLOBAL)
include(FetchContent)

# Sets the cache entries that the SDL3 configuration reads: include and library search paths, the
# wayland-scanner, a pkg-config for the Wayland modules and the result of a link check that the
# sonames cannot answer.
function(voyemu_window_linux)
    set(component "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/component.json")
    file(READ "${component}" metadata)
    string(JSON count LENGTH "${metadata}" window_linux)
    math(EXPR last "${count} - 1")
    get_filename_component(project_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." REALPATH)
    set(root "${CMAKE_BINARY_DIR}/window-linux")
    set(include "${root}/include")
    file(MAKE_DIRECTORY "${include}/X11/extensions" "${root}/lib")
    foreach(index RANGE ${last})
        string(JSON name MEMBER "${metadata}" window_linux ${index})
        string(JSON url GET "${metadata}" window_linux ${name} url)
        string(JSON sha GET "${metadata}" window_linux ${name} sha256)
        string(JSON version GET "${metadata}" window_linux ${name} version)
        string(LENGTH "${sha}" sha_length)
        if(NOT sha_length EQUAL 64 OR NOT sha MATCHES "^[0-9a-f]+$" OR NOT url MATCHES "^https://")
            message(FATAL_ERROR "Invalid pinned window source: ${name}")
        endif()
        string(REGEX MATCH "\\.tar\\.(gz|xz)$" extension "${url}")
        set(cached "${project_root}/.local/toolchains/downloads/sources/${sha}${extension}")
        if(EXISTS "${cached}")
            if(IS_SYMLINK "${cached}")
                message(FATAL_ERROR "Linked window archive: ${name}")
            endif()
            set(url "${cached}")
        endif()
        string(MAKE_C_IDENTIFIER "voyemu_window_${name}" content)
        string(TOLOWER "${content}" content)
        FetchContent_Declare(${content} URL "${url}" URL_HASH "SHA256=${sha}" SOURCE_SUBDIR voyemu_source_only)
        FetchContent_MakeAvailable(${content})
        set(source_${name} "${${content}_SOURCE_DIR}")
        set(version_${name} "${version}")
    endforeach()

    # X11: the protocol headers, Xlib and the extension libraries that SDL uses.
    file(COPY "${source_xorgproto}/include/X11/" DESTINATION "${include}/X11" FILES_MATCHING PATTERN "*.h")
    # glibc names the descriptor bits of fd_set __fds_bits.
    file(READ "${source_xorgproto}/include/X11/Xpoll.h.in" text)
    string(REPLACE "@USE_FDS_BITS@" "__fds_bits" text "${text}")
    file(WRITE "${include}/X11/Xpoll.h" "${text}")
    file(COPY "${source_libX11}/include/X11/" DESTINATION "${include}/X11" FILES_MATCHING PATTERN "*.h")
    file(READ "${source_libX11}/include/X11/XlibConf.h.in" text)
    string(REPLACE "#undef XTHREADS" "#define XTHREADS 1" text "${text}")
    string(REPLACE "#undef XUSE_MTSAFE_API" "#define XUSE_MTSAFE_API 1" text "${text}")
    file(WRITE "${include}/X11/XlibConf.h" "${text}")
    foreach(name libXext libXi libXfixes libXrandr libXrender libXScrnSaver)
        file(COPY "${source_${name}}/include/X11/extensions/" DESTINATION "${include}/X11/extensions" FILES_MATCHING PATTERN "*.h")
    endforeach()
    file(COPY "${source_libXcursor}/include/X11/Xcursor/Xcursor.h" DESTINATION "${include}/X11/Xcursor")

    # Wayland: the client, EGL and cursor headers, xkbcommon and libdecor.
    string(REPLACE "." ";" parts "${version_wayland}")
    list(GET parts 0 major)
    list(GET parts 1 minor)
    list(GET parts 2 micro)
    file(READ "${source_wayland}/src/wayland-version.h.in" text)
    string(REPLACE "@WAYLAND_VERSION_MAJOR@" "${major}" text "${text}")
    string(REPLACE "@WAYLAND_VERSION_MINOR@" "${minor}" text "${text}")
    string(REPLACE "@WAYLAND_VERSION_MICRO@" "${micro}" text "${text}")
    string(REPLACE "@WAYLAND_VERSION@" "${version_wayland}" text "${text}")
    file(WRITE "${include}/wayland-version.h" "${text}")
    file(COPY "${source_wayland}/src/wayland-client.h" "${source_wayland}/src/wayland-client-core.h"
        "${source_wayland}/src/wayland-util.h" "${source_wayland}/egl/wayland-egl.h"
        "${source_wayland}/egl/wayland-egl-core.h" "${source_wayland}/cursor/wayland-cursor.h"
        DESTINATION "${include}")
    file(COPY "${source_libxkbcommon}/xkbcommon" DESTINATION "${include}")
    file(COPY "${source_libdecor}/src/libdecor.h" DESTINATION "${include}")

    # wayland-scanner of the pinned Wayland on the pinned expat, built for this host at configure,
    # as SDL runs it while it configures.
    file(WRITE "${root}/scanner/expat_config.h" "#define BYTEORDER 1234\n#define HAVE_GETRANDOM 1\n#define HAVE_SYSCALL_GETRANDOM 1\n#define XML_CONTEXT_BYTES 1024\n#define XML_DTD 1\n#define XML_GE 1\n#define XML_NS 1\n#define XML_DEV_URANDOM 1\n")
    set(scanner "${root}/wayland-scanner")
    if(NOT EXISTS "${scanner}")
        try_compile(scanner_built "${root}/scanner-build"
            SOURCES "${source_wayland}/src/scanner.c" "${source_wayland}/src/wayland-util.c"
                "${source_expat}/lib/xmlparse.c" "${source_expat}/lib/xmlrole.c" "${source_expat}/lib/xmltok.c"
                "${source_expat}/lib/xcs.c" "${source_expat}/lib/random_getrandom.c" "${source_expat}/lib/random_dev_urandom.c"
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${include};${source_wayland}/src;${source_expat}/lib;${root}/scanner"
            COMPILE_DEFINITIONS -DHAVE_EXPAT_CONFIG_H
            OUTPUT_VARIABLE scanner_output
            COPY_FILE "${scanner}")
        if(NOT scanner_built)
            message(FATAL_ERROR "wayland-scanner build failed:\n${scanner_output}")
        endif()
    endif()
    execute_process(COMMAND "${scanner}" client-header "${source_wayland}/protocol/wayland.xml" "${include}/wayland-client-protocol.h"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "wayland-scanner failed on the core protocol")
    endif()

    # Empty shared objects under the sonames that SDL loads, so that SDL derives the sonames and its
    # link checks link.
    file(WRITE "${root}/lib/empty.c" "")
    set(libraries X11=libX11.so.6 Xext=libXext.so.6 Xcursor=libXcursor.so.1 Xi=libXi.so.6
        Xfixes=libXfixes.so.3 Xrandr=libXrandr.so.2 Xrender=libXrender.so.1 Xss=libXss.so.1
        wayland-client=libwayland-client.so.0 wayland-egl=libwayland-egl.so.1
        wayland-cursor=libwayland-cursor.so.0 xkbcommon=libxkbcommon.so.0 decor-0=libdecor-0.so.0)
    foreach(entry IN LISTS libraries)
        string(REPLACE "=" ";" entry "${entry}")
        list(GET entry 0 name)
        list(GET entry 1 soname)
        if(NOT EXISTS "${root}/lib/${soname}")
            execute_process(COMMAND "${CMAKE_C_COMPILER}" -target "${CMAKE_C_COMPILER_TARGET}" -shared -nostdlib
                "-Wl,-soname,${soname}" -o "${root}/lib/${soname}" "${root}/lib/empty.c"
                RESULT_VARIABLE result)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "Soname stub failed: ${soname}")
            endif()
        endif()
        file(CREATE_LINK "${soname}" "${root}/lib/lib${name}.so" SYMBOLIC)
    endforeach()

    # The pkg-config of the Wayland modules: a CMake script that answers from the versions above.
    string(REPLACE "." ";" parts "${version_libxkbcommon}")
    file(WRITE "${root}/pkg-config.json" "{\"include\": \"${include}\", \"lib\": \"${root}/lib\", \"modules\": {\"wayland-client\": [\"${version_wayland}\", \"wayland-client\"], \"wayland-egl\": [\"${version_wayland}\", \"wayland-egl\"], \"wayland-cursor\": [\"${version_wayland}\", \"wayland-cursor\"], \"egl\": [\"1.5\", \"\"], \"xkbcommon\": [\"${version_libxkbcommon}\", \"xkbcommon\"], \"libdecor-0\": [\"${version_libdecor}\", \"decor-0\"]}}\n")
    set(PKG_CONFIG_EXECUTABLE "${CMAKE_COMMAND}" CACHE FILEPATH "pkg-config of the pinned window headers" FORCE)
    set(PKG_CONFIG_ARGN "-DVOYEMU_PKG_CONFIG=${root}/pkg-config.json;-P;${CMAKE_CURRENT_FUNCTION_LIST_DIR}/window_pkg_config.cmake;--" CACHE STRING "pkg-config arguments of the pinned window headers" FORCE)
    set(WAYLAND_SCANNER "${scanner}" CACHE FILEPATH "wayland-scanner of the pinned Wayland" FORCE)
    set(CMAKE_INCLUDE_PATH "${include}" PARENT_SCOPE)
    set(CMAKE_LIBRARY_PATH "${root}/lib" PARENT_SCOPE)
endfunction()
