add_executable(kobold
    configcommand.cpp
    devicecommand.cpp
    kobobookscommand.cpp
    kobosynccommand.cpp
    console.cpp
    librarycommand.cpp
    main.cpp
    parentalcommand.cpp
    removalprompt.cpp
    selectcommand.cpp
    shelfcommand.cpp
)

target_link_libraries(kobold
    PRIVATE
        kobold-services
        KF6::CoreAddons
        KF6::I18n
)

install(TARGETS kobold ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
