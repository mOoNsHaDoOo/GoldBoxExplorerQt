cmake_minimum_required(VERSION 3.21)
project(GoldBoxExplorerQt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(gbexplorer
    src/main.cpp
    src/MainWindow.cpp
    src/ExportDialog.cpp

    src/core/daxfile.cpp
    src/core/game.cpp

    src/dax/palette.cpp
    src/dax/renderblocks.cpp
    src/dax/daximagefile.cpp
    src/dax/walldef.cpp

    src/tbl/tlbfile.cpp

    src/parse/character.cpp
    src/parse/character_tables.cpp
    src/parse/geo.cpp
    src/parse/geodax.cpp
    src/parse/geodax_names.cpp
    src/parse/ecl.cpp
    src/parse/item_names.cpp

    src/widgets/imagesheetview.cpp
    src/widgets/hexview.cpp
    src/widgets/characterview.cpp

    src/plugins/pluginfactory.cpp
    src/plugins/imageplugins.cpp
    src/plugins/dataplugins.cpp
    src/plugins/geoplugins.cpp
)

target_include_directories(gbexplorer PRIVATE src)
target_link_libraries(gbexplorer PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(gbexplorer PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    set_target_properties(gbexplorer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Gold Box Explorer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.goldboxexplorer.qt"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
elseif(WIN32)
    set_target_properties(gbexplorer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS gbexplorer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

include(CTest)
if(BUILD_TESTING)
    # Synthetic files from tools/make_testdata.py. Each of these should decode
    # at least one image; --probe returns non-zero otherwise.
    foreach(test_file PIC.DAX BIG.DAX SPRITES.DAX STRATA.DAX MIXED.DAX MIXEDVGA.DAX 8X8TEST.DAX IMAGES.TLB BIG.TLB)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            add_test(NAME probe_${test_file}
                     COMMAND gbexplorer --probe "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            set_tests_properties(probe_${test_file} PROPERTIES
                                 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()
endif()
