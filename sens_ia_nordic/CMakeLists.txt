cmake_minimum_required(VERSION 3.20.0)

# Pick a Python interpreter that can import both west and pyelftools.
set(_zephyr_python_candidates "")

if(DEFINED WEST_PYTHON)
  list(APPEND _zephyr_python_candidates "${WEST_PYTHON}")
endif()

find_program(WEST_EXECUTABLE west)
if(WEST_EXECUTABLE)
  file(READ "${WEST_EXECUTABLE}" WEST_LAUNCHER_HEAD LIMIT 256)
  string(REGEX MATCH "^#!([^\n\r]+)" WEST_SHEBANG "${WEST_LAUNCHER_HEAD}")
  if(CMAKE_MATCH_1)
    list(APPEND _zephyr_python_candidates "${CMAKE_MATCH_1}")
  endif()
endif()

if(DEFINED ENV{ZEPHYR_PYTHON})
  list(APPEND _zephyr_python_candidates "$ENV{ZEPHYR_PYTHON}")
endif()

file(GLOB _ncs_toolchain_pythons "/opt/nordic/ncs/toolchains/*/bin/python3")
list(APPEND _zephyr_python_candidates ${_ncs_toolchain_pythons})

find_program(SYSTEM_PYTHON3_EXECUTABLE python3)
if(SYSTEM_PYTHON3_EXECUTABLE)
  list(APPEND _zephyr_python_candidates "${SYSTEM_PYTHON3_EXECUTABLE}")
endif()

list(REMOVE_DUPLICATES _zephyr_python_candidates)

unset(_selected_python)
foreach(_py IN LISTS _zephyr_python_candidates)
  if(EXISTS "${_py}")
    execute_process(
      COMMAND "${_py}" -c "import west.version, elftools"
      RESULT_VARIABLE _py_ok
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(_py_ok EQUAL 0)
      set(_selected_python "${_py}")
      break()
    endif()
  endif()
endforeach()

if(DEFINED _selected_python)
  set(WEST_PYTHON "${_selected_python}" CACHE FILEPATH "Python interpreter used by west" FORCE)
  set(Python3_EXECUTABLE "${_selected_python}" CACHE FILEPATH "Python interpreter used by Zephyr" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sens_ia_nordic)

# Add Edge Impulse library
include(sense_ia_datat001-cpp-mcu-v1/edge-impulse-sdk/cmake/utils.cmake)
add_subdirectory(sense_ia_datat001-cpp-mcu-v1/edge-impulse-sdk/cmake/zephyr)

RECURSIVE_FIND_FILE_APPEND(MODEL_SOURCE "sense_ia_datat001-cpp-mcu-v1/tflite-model" "*.cpp")
target_include_directories(app PRIVATE sense_ia_datat001-cpp-mcu-v1)

target_sources(app PRIVATE
  src/main.cpp
  src/ble.c
  src/biohub_max32664.c
  src/imu_lsm6.c
  src/sd.c
  ${MODEL_SOURCE}
)
