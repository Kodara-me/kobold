cmake_minimum_required(VERSION 3.20)

project(kobold VERSION 0.1.0 LANGUAGES CXX)

# Minimum versions, kept in sync with SPEC-INSTALL.md §3.
set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")
set(QTKEYCHAIN_MIN_VERSION "0.14.0")
set(KEPUBIFY_MIN_VERSION "4.0")

find_package(ECM ${KF_MIN_VERSION} QUIET NO_MODULE)
if(NOT ECM_FOUND)
    message(FATAL_ERROR
        "extra-cmake-modules >= ${KF_MIN_VERSION} not found.\n"
        "Arch package: extra-cmake-modules (sudo pacman -S --needed extra-cmake-modules)")
endif()
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEClangFormat)
include(ECMSetupVersion)
include(ECMAddTests)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_GUI "Build the Kirigami application (kobold-gui)" ON)

# Required packages are looked up without REQUIRED so that feature_summary can report
# every missing package at once, each with its Arch package name.
set(KOBOLD_QT_COMPONENTS Core DBus Gui Network Sql Concurrent)
if(BUILD_TESTING)
    list(APPEND KOBOLD_QT_COMPONENTS Test)
endif()
if(BUILD_GUI)
    list(APPEND KOBOLD_QT_COMPONENTS Gui Widgets Qml Quick QuickControls2)
endif()
find_package(Qt6 ${QT_MIN_VERSION} CONFIG COMPONENTS ${KOBOLD_QT_COMPONENTS})
set_package_properties(Qt6 PROPERTIES
    TYPE REQUIRED
    DESCRIPTION "Arch packages: qt6-base, and qt6-declarative for the application"
    PURPOSE "Core, networking, SQLite access, background jobs, tests")

find_package(KF6CoreAddons ${KF_MIN_VERSION} CONFIG)
set_package_properties(KF6CoreAddons PROPERTIES
    TYPE REQUIRED DESCRIPTION "Arch package: kcoreaddons" PURPOSE "Application metadata")

find_package(KF6I18n ${KF_MIN_VERSION} CONFIG)
set_package_properties(KF6I18n PROPERTIES
    TYPE REQUIRED DESCRIPTION "Arch package: ki18n" PURPOSE "Translations")

find_package(KF6Config ${KF_MIN_VERSION} CONFIG)
set_package_properties(KF6Config PROPERTIES
    TYPE REQUIRED DESCRIPTION "Arch package: kconfig" PURPOSE "Settings")

find_package(KF6Solid ${KF_MIN_VERSION} CONFIG)
set_package_properties(KF6Solid PROPERTIES
    TYPE REQUIRED DESCRIPTION "Arch package: solid" PURPOSE "Kobo detection")

find_package(KF6Archive ${KF_MIN_VERSION} CONFIG)
set_package_properties(KF6Archive PROPERTIES
    TYPE REQUIRED DESCRIPTION "Arch package: karchive" PURPOSE "Reading EPUB and CBZ metadata and covers on shelves")

find_package(Poppler COMPONENTS Qt6)
# Arch package: poppler-qt6.
set_package_properties(Poppler PROPERTIES
    TYPE REQUIRED PURPOSE "Reading PDF metadata and covers on shelves (Arch package: poppler-qt6)")

find_package(Qt6Keychain ${QTKEYCHAIN_MIN_VERSION} CONFIG)
set_package_properties(Qt6Keychain PROPERTIES
    TYPE REQUIRED DESCRIPTION "Arch package: qtkeychain-qt6" PURPOSE "Komga API key storage")

if(BUILD_GUI)
    find_package(KF6Kirigami ${KF_MIN_VERSION} CONFIG)
    set_package_properties(KF6Kirigami PROPERTIES
        TYPE REQUIRED DESCRIPTION "Arch package: kirigami" PURPOSE "Application user interface")
    find_package(KF6KirigamiAddons 1.0 CONFIG)
    set_package_properties(KF6KirigamiAddons PROPERTIES
        TYPE RUNTIME DESCRIPTION "Arch package: kirigami-addons" PURPOSE "Settings forms and About page")
    find_package(KF6Crash ${KF_MIN_VERSION} CONFIG)
    set_package_properties(KF6Crash PROPERTIES
        TYPE REQUIRED DESCRIPTION "Arch package: kcrash" PURPOSE "Crash handling")
    find_package(KF6IconThemes ${KF_MIN_VERSION} CONFIG)
    set_package_properties(KF6IconThemes PROPERTIES
        TYPE REQUIRED DESCRIPTION "Arch package: kiconthemes" PURPOSE "Plasma icon integration")
endif()

find_package(Kepubify ${KEPUBIFY_MIN_VERSION})
set_package_properties(Kepubify PROPERTIES
    TYPE RUNTIME
    DESCRIPTION "AUR package: kepubify (paru -S kepubify)"
    PURPOSE "EPUB to KEPUB conversion, tests that need it are skipped when absent")

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KOBOLD
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/kobold_version.h)

add_subdirectory(src)
# Compiled into <build>/locale and installed into share/locale (SPEC-I18N.md §6).
ki18n_install(po)
if(BUILD_GUI)
    add_subdirectory(data)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

file(GLOB_RECURSE KOBOLD_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h)
kde_clang_format(${KOBOLD_FORMAT_SOURCES})
