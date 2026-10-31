add_library(kobold-core STATIC
    apikeystore.cpp
    comicconverter.cpp
    browsing.cpp
    converter.cpp
    deviceidentity.cpp
    epubmetadatawriter.cpp
    devicelocator.cpp
    filenaming.cpp
    jsonfile.cpp
    koboconf.cpp
    kobobooks.cpp
    kobodatabase.cpp
    kobovolumes.cpp
    localfiles.cpp
    komgabook.cpp
    komgaclient.cpp
    komgatypes.cpp
    logging.cpp
    manifest.cpp
    metadatalookup.cpp
    parental.cpp
    platform.cpp
    processrunner.cpp
    progresscalculator.cpp
    progresssync.cpp
    selectionresolver.cpp
    selectionstore.cpp
    settings.cpp
    shelfcovers.cpp
    shelfdatabase.cpp
    shelfedits.cpp
    shelffolder.cpp
    shelflibrary.cpp
    shelfmetadata.cpp
    shelfscanner.cpp
    syncexecutor.cpp
    syncplanner.cpp
    thumbnailcache.cpp
    volumerange.cpp
)

target_include_directories(kobold-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_BINARY_DIR}
)

target_link_libraries(kobold-core
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Gui
        Qt6::Concurrent
)

if(Qt6Keychain_FOUND)
    target_link_libraries(kobold-core PRIVATE qt6keychain)
    target_compile_definitions(kobold-core PRIVATE KOBOLD_HAVE_KEYCHAIN)
endif()

if(TARGET Poppler::Qt6)
    target_link_libraries(kobold-core PRIVATE Poppler::Qt6)
    target_compile_definitions(kobold-core PRIVATE KOBOLD_HAVE_POPPLER)
endif()

if(KOBOLD_USE_KDE)
    target_link_libraries(kobold-core
        PUBLIC
            Qt6::DBus
            KF6::ConfigCore
            KF6::I18n
        PRIVATE
            KF6::Archive
            KF6::Solid
    )
    target_compile_definitions(kobold-core PRIVATE KOBOLD_HAVE_SOLID)
else()
    # Stand-ins for the few KDE classes the sources use (KZip is built on libarchive).
    find_package(LibArchive REQUIRED)
    target_include_directories(kobold-core PUBLIC ${PROJECT_SOURCE_DIR}/compat)
    target_link_libraries(kobold-core PUBLIC LibArchive::LibArchive)
endif()
