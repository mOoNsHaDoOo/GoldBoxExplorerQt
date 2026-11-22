cmake_minimum_required(VERSION 3.21)
project(GoldBoxExplorerQt VERSION 0.90.0 LANGUAGES CXX)

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
    src/core/container.cpp
    src/core/adf.cpp
    src/core/tempfolder.cpp
    src/core/zip.cpp

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
    src/parse/ecl_disasm.cpp
    src/parse/ecl_basic.cpp
    src/parse/ecl_basic_symbols.cpp
    src/parse/item_names.cpp
    src/parse/iff.cpp
    src/parse/audio.cpp
    src/parse/daa.cpp
    src/parse/smus.cpp
    src/parse/glb.cpp
    src/parse/glib_codec.cpp
    src/parse/amiga_tile.cpp
    src/parse/record_layouts.cpp
    src/parse/record_layouts_gen.cpp
    src/parse/slb.cpp
    src/parse/mxtx.cpp
    src/parse/krynn_fields.cpp
    src/parse/krynn_records.cpp

    src/widgets/imagesheetview.cpp
    src/widgets/hexview.cpp
    src/widgets/characterview.cpp
    src/widgets/audioview.cpp
    src/widgets/geofpview.cpp
    src/widgets/walltemplateexportdialog.cpp
    src/widgets/eclbasicview.cpp

    src/plugins/pluginfactory.cpp
    src/plugins/imageplugins.cpp
    src/plugins/dataplugins.cpp
    src/plugins/geoplugins.cpp
    src/plugins/audioplugins.cpp
    src/plugins/iffplugin.cpp
    src/plugins/contentplugins.cpp
    src/plugins/glbplugin.cpp
    src/plugins/slbplugin.cpp
    src/plugins/mxtxplugin.cpp
    src/plugins/containerplugin.cpp
    src/plugins/krynnviews.cpp
    src/plugins/amigaplugins.cpp
    src/plugins/recordplugins.cpp
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
    foreach(test_file PIC.DAX BIG.DAX SPRITES.DAX STRATA.DAX MIXED.DAX MIXEDVGA.DAX 8X8TEST.DAX IMAGES.TLB BIG.TLB TEST.DAA DAXIN.DAA TEST.ANIM TEST-LBM.IFF TESTDAA.DAA)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            add_test(NAME probe_${test_file}
                     COMMAND gbexplorer --probe "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            set_tests_properties(probe_${test_file} PROPERTIES
                                 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TESTINST.IFF")
        add_test(NAME smoke_iff_inst
                 COMMAND gbexplorer --smoke "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TESTINST.IFF")
        set_tests_properties(smoke_iff_inst PROPERTIES
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                             TIMEOUT 10)
    endif()

    # Amiga disk image: list and extract (--adf FILE OUTDIR).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TEST.ADF")
        add_test(NAME adf_extract
                 COMMAND gbexplorer --adf "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TEST.ADF"
                         "${CMAKE_CURRENT_BINARY_DIR}/adf_test_out")
        set_tests_properties(adf_extract PROPERTIES
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                             PASS_REGULAR_EXPRESSION "S/startup-sequence  11")
    endif()

    # ZIP archive with a disk image (built-in deflate decoder, CRC check).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TESTADF.ZIP")
        add_test(NAME zip_extract
                 COMMAND gbexplorer --zip "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TESTADF.ZIP"
                         "${CMAKE_CURRENT_BINARY_DIR}/zip_test_out")
        set_tests_properties(zip_extract PROPERTIES
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                             PASS_REGULAR_EXPRESSION "TEST.ADF  10240  method 8")
    endif()

    # Death Knights of Krynn / Pool of Radiance record viewers.
    foreach(test_file TESTDKK.GAM TESTPOR.ITM)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            add_test(NAME smoke_${test_file}
                     COMMAND gbexplorer --smoke "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            set_tests_properties(smoke_${test_file} PROPERTIES
                                 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                                 TIMEOUT 10)
        endif()
    endforeach()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TESTITEM.DAX")
        add_test(NAME smoke_item_dax
                 COMMAND gbexplorer --smoke "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TESTITEM.DAX")
        set_tests_properties(smoke_item_dax PROPERTIES
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                             TIMEOUT 10)
    endif()

    # Container browser: --list must recognise the multi-block files and
    # --probe-block must find a specific viewer for one inner block.
    foreach(test_file PIC.DAX ECL1.DAX GEO1.DAX TESTDAA.DAA TESTITEM.DAX STRINGS.GLB IMAGES.TLB AMIGA.TLB)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            add_test(NAME list_${test_file}
                     COMMAND gbexplorer --list "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}")
            add_test(NAME block_${test_file}
                     COMMAND gbexplorer --probe-block "${CMAKE_CURRENT_SOURCE_DIR}/testdata/${test_file}" 1)
            set_tests_properties(list_${test_file} block_${test_file} PROPERTIES
                                 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endif()
    endforeach()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TEST.8SVX")
        add_test(NAME audio_8svx
                 COMMAND gbexplorer --audio "${CMAKE_CURRENT_SOURCE_DIR}/testdata/TEST.8SVX")
        set_tests_properties(audio_8svx PROPERTIES
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
