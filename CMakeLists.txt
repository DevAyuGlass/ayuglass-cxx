cmake_minimum_required(VERSION 3.22)
project(ayuglass LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TG_API_ID "0" CACHE STRING "")
set(TG_API_HASH "" CACHE STRING "")
set(SKIA_DIR "" CACHE PATH "Папка с lib/*.a, include/, modules/")
set(SDL3_SOURCE_DIR "" CACHE PATH "Исходники SDL3; пусто = системный SDL3")
set(TD_ROOT "" CACHE PATH "Android: папка с <abi>/ для TDLib")
set(SKIA_ROOT "" CACHE PATH "Android: папка с <abi>/ для Skia")

if("${TG_API_ID}" STREQUAL "")
  set(TG_API_ID 0)
endif()

if(ANDROID)
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
  if(TD_ROOT)
    set(Td_DIR "${TD_ROOT}/${ANDROID_ABI}/lib/cmake/Td")
    set(OPENSSL_ROOT_DIR "${TD_ROOT}/${ANDROID_ABI}/openssl")
    set(OPENSSL_USE_STATIC_LIBS ON)
  endif()
  if(SKIA_ROOT)
    set(SKIA_DIR "${SKIA_ROOT}/${ANDROID_ABI}")
  endif()
endif()

find_package(Td REQUIRED)
find_package(OpenSSL REQUIRED)

if(SDL3_SOURCE_DIR)
  add_subdirectory(${SDL3_SOURCE_DIR} SDL3)
else()
  find_package(SDL3 REQUIRED CONFIG)
endif()

# --- ядро (без UI и без платформенного кода) ---
add_library(core STATIC core/TdClient.cpp)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC Td::TdStatic)

# --- Skia (готовые статики из Actions) ---
file(GLOB SKIA_LIBS ${SKIA_DIR}/lib/*.a)
# PartitionAlloc (подмена malloc/free) приложению не нужен и требует флагов --wrap
list(FILTER SKIA_LIBS EXCLUDE REGEX "/lib(allocator_|raw_ptr)")
if(NOT SKIA_LIBS)
  message(FATAL_ERROR "Не найдены ${SKIA_DIR}/lib/*.a, задай -DSKIA_DIR=...")
endif()

if(ANDROID)
  add_library(ayuglass SHARED src/main.cpp)   # SDL грузит libmain.so
  set_target_properties(ayuglass PROPERTIES OUTPUT_NAME main)
  set(PLATFORM_LIBS EGL GLESv3 android log)
else()
  add_executable(ayuglass src/main.cpp)
  find_package(OpenGL REQUIRED)
  find_package(Fontconfig REQUIRED)
  set(PLATFORM_LIBS OpenGL::GL Fontconfig::Fontconfig dl pthread)
endif()

target_include_directories(ayuglass PRIVATE ${SKIA_DIR})   # "include/core/..." и "modules/..."
target_compile_definitions(ayuglass PRIVATE
  TG_API_ID=${TG_API_ID}
  TG_API_HASH=\"${TG_API_HASH}\")
target_link_libraries(ayuglass PRIVATE
  core SDL3::SDL3
  -Wl,--start-group ${SKIA_LIBS} -Wl,--end-group
  ${PLATFORM_LIBS})

if(ANDROID)
  # Две копии zlib: в libskia.a (вшит) и в libz.a из NDK (его тянет TDLib).
  # Символ x86_cpu_enable_avx512 определён в обеих; оставляем первое определение.
  target_link_options(ayuglass PRIVATE -Wl,--allow-multiple-definition)
endif()
