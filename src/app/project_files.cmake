# Project files whose content deps, fetch, build and check read. An installation without src/ reads
# the copies that project_files.inc embeds in the launcher; a source checkout reads its tree.
# Included, it registers the build step; with -P it writes project_files.inc.
set(voyemu_project_files
    src/app/android-wheel-routes.json
    src/app/toolchain.json
    src/assets/voyemu-logo-white-black-1920-720.png
    src/guest/qnx/native_mcu_qinggan11_611.json
    src/guest/qnx/persistent-runtime.sh
    src/guest/qnx/qnx-passion-runtime.sh
    src/guest/qnx/qnx-runtime.sh
    src/guest/qnx/qnx-screen.conf
    src/prepare/libraries.json
    src/prepare/toolchain.json)
if(CMAKE_SCRIPT_MODE_FILE)
    cmake_policy(VERSION 3.22)
    set(voyemu_contents "")
    set(voyemu_entries "static constexpr EmbeddedProjectFile embedded_project_files[]={\n")
    set(voyemu_index 0)
    foreach(name IN LISTS voyemu_project_files)
        set(path "${VOYEMU_PROJECT_ROOT}/${name}")
        file(SIZE "${path}" bytes)
        file(READ "${path}" hex HEX)
        string(REGEX REPLACE "(..)" "0x\\1," hex "${hex}")
        string(APPEND voyemu_contents "static constexpr unsigned char project_file_${voyemu_index}[]={${hex}0};\n")
        string(APPEND voyemu_entries "{\"${name}\",${bytes},project_file_${voyemu_index}},\n")
        math(EXPR voyemu_index "${voyemu_index} + 1")
    endforeach()
    string(APPEND voyemu_entries "};\n")
    file(WRITE "${VOYEMU_PROJECT_FILE_OUTPUT}" "${voyemu_contents}${voyemu_entries}")
    return()
endif()
set(voyemu_project_root_directory "${CMAKE_CURRENT_SOURCE_DIR}/../..")
list(TRANSFORM voyemu_project_files PREPEND "${voyemu_project_root_directory}/" OUTPUT_VARIABLE voyemu_project_paths)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/project_files.inc"
    COMMAND "${CMAKE_COMMAND}" "-DVOYEMU_PROJECT_ROOT=${voyemu_project_root_directory}"
        "-DVOYEMU_PROJECT_FILE_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/project_files.inc"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/project_files.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/project_files.cmake" ${voyemu_project_paths}
    COMMENT "Embedding the project files" VERBATIM)
