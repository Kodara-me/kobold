ecm_add_tests(
    browsingtest.cpp
    comicconvertertest.cpp
    convertertest.cpp
    deviceidentitytest.cpp
    devicelocatortest.cpp
    filenamingtest.cpp
    komgabrowsingtest.cpp
    kobobookstest.cpp
    kobodatabasetest.cpp
    koboconftest.cpp
    koboregistrytest.cpp
    komgabooktest.cpp
    komgaclienttest.cpp
    manifesttest.cpp
    metadatalookuptest.cpp
    parentaltest.cpp
    progresscalculatortest.cpp
    progresssynctest.cpp
    selectionresolvertest.cpp
    selectionstoretest.cpp
    settingstest.cpp
    syncexecutortest.cpp
    syncplannertest.cpp
    thumbnailcachetest.cpp
    volumerangetest.cpp
    LINK_LIBRARIES kobold-core KF6::Archive Qt6::Test
)

# Empty when kepubify is not installed: the real conversion test is then skipped.
if(Kepubify_FOUND)
    set(KOBOLD_TEST_KEPUBIFY "${Kepubify_EXECUTABLE}")
endif()
target_compile_definitions(convertertest PRIVATE
    KOBOLD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    KOBOLD_KEPUBIFY_EXECUTABLE="${KOBOLD_TEST_KEPUBIFY}"
)

# Development tool for the application: screenshots of every page (not a test).
if(BUILD_GUI)
    add_executable(kobold-gui-screenshots appscreenshots.cpp)
    target_link_libraries(kobold-gui-screenshots PRIVATE kobold-app kobold-appplugin Qt6::Widgets Qt6::QuickControls2 KF6::CoreAddons KF6::I18n KF6::I18nQml)
endif()

if(BUILD_GUI)
    ecm_add_test(appcontrollertest.cpp LINK_LIBRARIES kobold-app Qt6::Test)
endif()

ecm_add_test(kobosyncrunnertest.cpp LINK_LIBRARIES kobold-services Qt6::Test)
ecm_add_test(parentallocktest.cpp LINK_LIBRARIES kobold-services Qt6::Test)
ecm_add_test(syncrunnertest.cpp LINK_LIBRARIES kobold-services Qt6::Test)
ecm_add_test(kobomountertest.cpp LINK_LIBRARIES kobold-services Qt6::Test)

add_test(NAME parentalcli COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/parentalcli.sh $<TARGET_FILE:kobold>)
add_test(NAME localfilescli COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/localfilescli.sh $<TARGET_FILE:kobold>)
add_test(NAME shelfcli COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/shelfcli.sh $<TARGET_FILE:kobold>)
add_test(NAME kobobookscli COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/kobobookscli.sh $<TARGET_FILE:kobold>)
add_test(NAME kobosynccli COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/kobosynccli.sh $<TARGET_FILE:kobold>)

ecm_add_test(shelfmetadatatest.cpp LINK_LIBRARIES kobold-core KF6::Archive Qt6::Gui Qt6::Test)
set_tests_properties(shelfmetadatatest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
ecm_add_test(shelfscannertest.cpp LINK_LIBRARIES kobold-core KF6::Archive Qt6::Test)

# Tests check English messages, whatever the language of the machine (SPEC-I18N.md §6).
get_property(kobold_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${kobold_tests} PROPERTIES ENVIRONMENT_MODIFICATION "LANGUAGE=set:en")

# Translations (SPEC-I18N.md §7): complete French catalog, CLI in both languages.
add_test(NAME getkobold COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/getkobold.sh ${PROJECT_SOURCE_DIR}/scripts/get-kobold.sh)
add_test(NAME translations-complete COMMAND ${PROJECT_SOURCE_DIR}/scripts/update-translations.sh --check)
add_test(NAME translations-cli-fr COMMAND kobold --help)
# LANGUAGE is ignored under the C locale, and fr_FR is not generated everywhere: the
# language comes from LANGUAGE on top of a locale that most systems have.
set_tests_properties(translations-cli-fr PROPERTIES
    ENVIRONMENT "LANGUAGE=fr;LANG=en_US.UTF-8"
    PASS_REGULAR_EXPRESSION "Commandes"
    FAIL_REGULAR_EXPRESSION "Commands:")
add_test(NAME translations-cli-en COMMAND kobold --help)
set_tests_properties(translations-cli-en PROPERTIES
    ENVIRONMENT "LANGUAGE=en;LANG=en_US.UTF-8"
    PASS_REGULAR_EXPRESSION "Commands:")
add_test(NAME translations-cli-fallback COMMAND kobold --help)
set_tests_properties(translations-cli-fallback PROPERTIES
    ENVIRONMENT "LANGUAGE=de;LANG=de_DE.UTF-8"
    PASS_REGULAR_EXPRESSION "Commands:")
ecm_add_test(shelfeditstest.cpp LINK_LIBRARIES kobold-core KF6::Archive Qt6::Gui Qt6::Sql Qt6::Test)
set_tests_properties(shelfeditstest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# No test ever reads or writes the system keychain: keys live in memory (SPEC-INSTALL.md §8).
get_property(KOBOLD_ALL_TESTS DIRECTORY PROPERTY TESTS)
set_property(TEST ${KOBOLD_ALL_TESTS} APPEND PROPERTY ENVIRONMENT "KOBOLD_KEYCHAIN=memory")
