# Desktop integration of the application (SPEC-APP.md §13, SPEC-INSTALL.md §6.1).
# The desktop file and the metainfo get their translations from po/ (SPEC-I18N.md §6).
find_package(Gettext REQUIRED)

file(GLOB kobold_catalogs ${PROJECT_SOURCE_DIR}/po/*/kobold.po)

# Merges every catalog into <template>, one language after the other.
function(kobold_translate_file template mode output)
    set(current ${CMAKE_CURRENT_SOURCE_DIR}/${template})
    set(commands)
    set(step 0)
    foreach(catalog ${kobold_catalogs})
        get_filename_component(language_dir ${catalog} DIRECTORY)
        get_filename_component(language ${language_dir} NAME)
        math(EXPR step "${step} + 1")
        set(next ${CMAKE_CURRENT_BINARY_DIR}/${output}.${step})
        list(APPEND commands COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} ${mode} --template=${current} -l ${language} -o ${next} ${catalog})
        set(current ${next})
    endforeach()
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${output}
        ${commands}
        COMMAND ${CMAKE_COMMAND} -E copy ${current} ${CMAKE_CURRENT_BINARY_DIR}/${output}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${template} ${kobold_catalogs}
        VERBATIM)
endfunction()

kobold_translate_file(me.kodara.kobold.desktop --desktop me.kodara.kobold.desktop)
kobold_translate_file(me.kodara.kobold.metainfo.xml --xml me.kodara.kobold.metainfo.xml)
add_custom_target(kobold-desktop-files ALL DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/me.kodara.kobold.desktop
    ${CMAKE_CURRENT_BINARY_DIR}/me.kodara.kobold.metainfo.xml)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/me.kodara.kobold.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/me.kodara.kobold.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
# The default of the icons the user picks from (SPEC-APP.md §12.1).
install(FILES icons/kobold-peek.svg DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps RENAME me.kodara.kobold.svg)
# Place icon of the Kobold Library folder (SPEC-SHELF.md §2.1): Breeze's folder with a kobold.
foreach(size 32 64)
    install(FILES icons/places/folder-kobold-${size}.svg DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/${size}x${size}/places RENAME folder-kobold.svg)
endforeach()
