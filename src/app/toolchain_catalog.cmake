# The launcher reads src/app/toolchain.json through the embedded project files; configure checks its schema.
set(voyemu_toolchain_source "${CMAKE_CURRENT_SOURCE_DIR}/../../src/app/toolchain.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${voyemu_toolchain_source}")
file(READ "${voyemu_toolchain_source}" voyemu_toolchain_json)
string(JSON voyemu_toolchain_schema GET "${voyemu_toolchain_json}" schema)
if(NOT voyemu_toolchain_schema EQUAL 1)
    message(FATAL_ERROR "Unsupported toolchain manifest schema")
endif()
