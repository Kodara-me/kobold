# Kirigami application (SPEC-APP.md).
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Qt's helper targets of the QML module (resources, plugin init) inherit the autogen include
# folders of kobold-app before Qt creates them; KDECompilerSettings then warns about it.
add_compile_options(-Wno-missing-include-dirs)

add_executable(kobold-gui
    main.cpp
)

# Everything but main(): also linked by the application tests.
add_library(kobold-app STATIC
    appcontroller.cpp
    kobobookscontroller.cpp
    shelfeditcontroller.cpp
    komgaimageprovider.cpp
    variantlistmodel.cpp
)

# The application icons the user picks from (SPEC-APP.md §12.1), at
# qrc:/qt/qml/me/kodara/kobold/icons/<id>.svg.
foreach(icon peek sync face)
    set_source_files_properties(${PROJECT_SOURCE_DIR}/data/icons/kobold-${icon}.svg PROPERTIES QT_RESOURCE_ALIAS icons/${icon}.svg)
endforeach()
# The Kobold Library's place icon, also when it is not installed (SPEC-SHELF.md §2.1).
set_source_files_properties(${PROJECT_SOURCE_DIR}/data/icons/places/folder-kobold-64.svg PROPERTIES QT_RESOURCE_ALIAS icons/folder-kobold.svg)

qt_add_qml_module(kobold-app
    URI me.kodara.kobold
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/me/kodara/kobold
    QML_FILES
        qml/Main.qml
        qml/MainPage.qml
        qml/Sidebar.qml
        qml/ContentPane.qml
        qml/KoboPane.qml
        qml/KomgaMessage.qml
        qml/KomgaSettingsPage.qml
        qml/KomgaSignInDialog.qml
        qml/KobosSettingsPage.qml
        qml/KoboBooksWindow.qml
        qml/DiagnosticsPage.qml
        qml/CacheSettingsPage.qml
        qml/AppearanceSettingsPage.qml
        qml/ParentalPinDialog.qml
        qml/ParentalSettingsPage.qml
        qml/RemovalDialog.qml
        qml/ShelvesSettingsPage.qml
        qml/ShelfEditorDialog.qml
        qml/ShelfLookupDialog.qml
        qml/SyncProgressDialog.qml
    RESOURCES
        ${PROJECT_SOURCE_DIR}/data/icons/kobold-peek.svg
        ${PROJECT_SOURCE_DIR}/data/icons/kobold-sync.svg
        ${PROJECT_SOURCE_DIR}/data/icons/kobold-face.svg
        ${PROJECT_SOURCE_DIR}/data/icons/places/folder-kobold-64.svg
)

target_include_directories(kobold-app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kobold-app
    PUBLIC
        kobold-services
        Qt6::Quick
        Qt6::Qml
)

target_link_libraries(kobold-gui
    PRIVATE
        kobold-app
        kobold-appplugin
        Qt6::Widgets
        Qt6::QuickControls2
        KF6::CoreAddons
        KF6::Crash
        KF6::I18n
        KF6::I18nQml
        KF6::IconThemes
)

install(TARGETS kobold-gui ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
