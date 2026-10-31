# Orchestration shared by the CLI and the application (SPEC-APP.md §3).
add_library(kobold-services STATIC
    aboutdata.cpp
    bookformat.cpp
    comicsetup.cpp
    diagnosticsrunner.cpp
    kobomounter.cpp
    kobosyncrunner.cpp
    kobotarget.cpp
    komgasession.cpp
    parentalfilter.cpp
    parentallock.cpp
    progressrunner.cpp
    runner.h
    syncrunner.cpp
    translations.cpp
)

target_include_directories(kobold-services
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kobold-services
    PUBLIC
        kobold-core
)
if(KOBOLD_USE_KDE)
    target_link_libraries(kobold-services PUBLIC KF6::CoreAddons KF6::I18n)
endif()

# The build tree, where useBuildTreeTranslations() looks for compiled catalogs.
target_compile_definitions(kobold-services PRIVATE KOBOLD_BUILD_DIR="${PROJECT_BINARY_DIR}")
