# One translation catalog for the whole project (SPEC-I18N.md §4).
add_compile_definitions(TRANSLATION_DOMAIN="kobold")

add_subdirectory(core)
add_subdirectory(services)
add_subdirectory(cli)
if(BUILD_GUI)
    add_subdirectory(app)
endif()
