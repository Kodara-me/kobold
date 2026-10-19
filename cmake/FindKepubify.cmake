# Finds the kepubify executable.
#
# Result variables:
#   Kepubify_FOUND        TRUE if kepubify was found with a suitable version
#   Kepubify_EXECUTABLE   path to kepubify
#   Kepubify_VERSION      version reported by `kepubify --version`

find_program(Kepubify_EXECUTABLE NAMES kepubify)

if(Kepubify_EXECUTABLE)
    execute_process(
        COMMAND ${Kepubify_EXECUTABLE} --version
        OUTPUT_VARIABLE _kepubify_output
        ERROR_VARIABLE _kepubify_output
        RESULT_VARIABLE _kepubify_result
        TIMEOUT 10)
    if(_kepubify_result EQUAL 0 AND _kepubify_output MATCHES "([0-9]+(\\.[0-9]+)+)")
        set(Kepubify_VERSION ${CMAKE_MATCH_1})
    endif()
    unset(_kepubify_output)
    unset(_kepubify_result)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Kepubify
    REQUIRED_VARS Kepubify_EXECUTABLE
    VERSION_VAR Kepubify_VERSION)

mark_as_advanced(Kepubify_EXECUTABLE)
