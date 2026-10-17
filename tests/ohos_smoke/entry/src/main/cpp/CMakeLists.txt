cmake_minimum_required(VERSION 3.16)
project(nativeapi_ohos_smoke LANGUAGES C CXX)
get_filename_component(NATIVEAPI_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)
add_subdirectory("${NATIVEAPI_CORE_ROOT}/src" nativeapi-core)
add_library(nativeapi_smoke SHARED smoke.cpp)
add_library(nativeapi_clipboard_lifetime SHARED clipboard_lifetime.cpp)
target_compile_features(nativeapi_clipboard_lifetime PRIVATE cxx_std_17)
target_compile_options(nativeapi_clipboard_lifetime PRIVATE -frtti -fexceptions)
target_link_options(nativeapi_clipboard_lifetime PRIVATE -Wl,-Bsymbolic)
target_link_libraries(nativeapi_clipboard_lifetime PRIVATE nativeapi ace_napi.z hilog_ndk.z)
add_library(nativeapi_device_info_fault_probe OBJECT
  device_info_fault_probe.cpp
  "${NATIVEAPI_CORE_ROOT}/src/device_info.cpp"
  "${NATIVEAPI_CORE_ROOT}/src/platform/ohos/device_info_ohos.cpp")
target_compile_features(nativeapi_device_info_fault_probe PRIVATE cxx_std_17)
set_target_properties(nativeapi_device_info_fault_probe PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(nativeapi_device_info_fault_probe PRIVATE
  nativeapi=nativeapi_device_info_fault_probe
  OH_GetMarketName=NativeDeviceProbeMarketName
  OH_GetProductModel=NativeDeviceProbeProductModel
  OH_GetManufacture=NativeDeviceProbeManufacture
  OH_GetDistributionOSName=NativeDeviceProbeOSName
  OH_GetDistributionOSVersion=NativeDeviceProbeOSVersion
  OH_GetOSFullName=NativeDeviceProbeOSFullName
  uname=NativeDeviceProbeUname
  napi_load_module=NativeDeviceProbeLoadModule)
target_compile_features(nativeapi_smoke PRIVATE cxx_std_17)
target_compile_options(nativeapi_smoke PRIVATE -frtti -fexceptions)
target_link_libraries(nativeapi_smoke PRIVATE nativeapi nativeapi_device_info_fault_probe
  ace_napi.z uv hilog_ndk.z ability_access_control)
