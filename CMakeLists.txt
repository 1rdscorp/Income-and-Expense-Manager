cmake_minimum_required(VERSION 3.21)

project(Ledgerly
    VERSION 1.4.0
    DESCRIPTION "Income & Expense Manager for small businesses"
    HOMEPAGE_URL "https://github.com/1rdscorp/Income-and-Expense-Manager"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(LEDGERLY_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Svg PrintSupport Network)

if(MSVC)
    # Source files contain UTF-8 (emoji icons, currency symbols).
    add_compile_options(/utf-8 /W3 /permissive-)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---- Core library (no widgets; unit tested) ----
add_library(ledgerly_core STATIC
    src/core/Models.h
    src/core/Util.h src/core/Util.cpp
    src/core/Defaults.h src/core/Defaults.cpp
    src/core/Json.h src/core/Json.cpp
    src/core/Recurrence.h src/core/Recurrence.cpp
    src/core/Store.h src/core/Store.cpp
    src/core/Backup.h src/core/Backup.cpp
    src/core/Reports.h src/core/Reports.cpp
    src/core/Xlsx.h src/core/Xlsx.cpp
    src/core/Sync.h src/core/Sync.cpp
    src/core/BankImport.h src/core/BankImport.cpp
    src/core/SimpleFin.h src/core/SimpleFin.cpp
)
target_include_directories(ledgerly_core PUBLIC src)
target_link_libraries(ledgerly_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network)
target_compile_definitions(ledgerly_core PUBLIC LEDGERLY_VERSION="${PROJECT_VERSION}")

# ---- Desktop UI library (shared by the app and UI tests) ----
add_library(ledgerly_ui STATIC
    src/ui/Theme.h src/ui/Theme.cpp
    src/ui/Widgets.h src/ui/Widgets.cpp
    src/ui/BarChart.h src/ui/BarChart.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/DashboardPage.h src/ui/DashboardPage.cpp
    src/ui/TransactionsPage.h src/ui/TransactionsPage.cpp
    src/ui/CategoriesPage.h src/ui/CategoriesPage.cpp
    src/ui/ReportsPage.h src/ui/ReportsPage.cpp
    src/ui/BackupPage.h src/ui/BackupPage.cpp
    src/ui/SettingsPage.h src/ui/SettingsPage.cpp
    src/ui/TransactionDialog.h src/ui/TransactionDialog.cpp
    src/ui/CategoryDialog.h src/ui/CategoryDialog.cpp
    src/ui/RecurringPage.h src/ui/RecurringPage.cpp
    src/ui/RecurringDialog.h src/ui/RecurringDialog.cpp
    src/ui/BankPage.h src/ui/BankPage.cpp
    src/ui/BankDialogs.h src/ui/BankDialogs.cpp
    src/ui/ImportDialog.h src/ui/ImportDialog.cpp
    src/ui/ReceiptViewer.h src/ui/ReceiptViewer.cpp
)
target_link_libraries(ledgerly_ui PUBLIC ledgerly_core Qt6::Widgets Qt6::Svg Qt6::PrintSupport)

# ---- Desktop application ----
set(APP_SOURCES src/main.cpp resources/resources.qrc)
if(WIN32)
    list(APPEND APP_SOURCES resources/windows/ledgerly.rc)
endif()
add_executable(Ledgerly WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(Ledgerly PRIVATE ledgerly_ui)

include(GNUInstallDirs)
install(TARGETS Ledgerly
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES resources/linux/ledgerly.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icons/app.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME ledgerly.svg)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "ledgerly")
    set(CPACK_PACKAGE_CONTACT "Ledgerly")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "office")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    include(CPack)
endif()

# ---- Tests ----
if(LEDGERLY_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(ledgerly_tests tests/tst_core.cpp)
    target_link_libraries(ledgerly_tests PRIVATE ledgerly_core Qt6::Test)
    add_test(NAME ledgerly_tests COMMAND ledgerly_tests)

    add_executable(ledgerly_ui_tests tests/tst_ui.cpp resources/resources.qrc)
    target_link_libraries(ledgerly_ui_tests PRIVATE ledgerly_ui Qt6::Test)
    add_test(NAME ledgerly_ui_tests COMMAND ledgerly_ui_tests -platform offscreen)
endif()
