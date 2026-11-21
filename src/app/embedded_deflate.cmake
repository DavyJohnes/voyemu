# voyemu_deflate_initializer(<input> <scratch> <out_var> <size_var>) sets out_var to the C initializer
# bytes of the raw deflate stream of the file input and size_var to its size; core/embedded_data.hpp
# inflates them. The stream is the payload of a gzip file without its header, which carries the time.
function(voyemu_deflate_initializer input scratch out_var size_var)
    file(ARCHIVE_CREATE OUTPUT "${scratch}" PATHS "${input}" FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
    file(READ "${scratch}" header HEX LIMIT 4)
    if(NOT header STREQUAL "1f8b0800")
        message(FATAL_ERROR "Unexpected gzip header of ${input}")
    endif()
    file(SIZE "${scratch}" bytes)
    math(EXPR length "${bytes} - 18")
    file(READ "${scratch}" hex HEX OFFSET 10 LIMIT ${length})
    file(REMOVE "${scratch}")
    file(SIZE "${input}" size)
    string(REGEX REPLACE "(..)" "0x\\1," initializer "${hex}")
    set(${out_var} "${initializer}" PARENT_SCOPE)
    set(${size_var} "${size}" PARENT_SCOPE)
endfunction()
