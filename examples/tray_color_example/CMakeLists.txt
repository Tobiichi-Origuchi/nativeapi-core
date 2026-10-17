cmake_minimum_required(VERSION 3.16)
project(tray_color_example LANGUAGES CXX)

add_executable(tray_color_example main.cpp)
target_link_libraries(tray_color_example PRIVATE nativeapi)
target_compile_features(tray_color_example PRIVATE cxx_std_17)

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  set_source_files_properties(main.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_options(tray_color_example PRIVATE -fobjc-arc)
  set_target_properties(tray_color_example PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Tray Colours"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.libnativeapi.tray-colours-example")
endif()

if(WIN32)
  target_compile_options(tray_color_example PRIVATE /utf-8)
endif()
