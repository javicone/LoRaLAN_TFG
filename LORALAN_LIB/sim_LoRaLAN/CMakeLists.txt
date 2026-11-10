cmake_minimum_required(VERSION 3.16)
project(sim_LoRaLAN CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fuentes de la biblioteca.
set(LORALAN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../LoRaLAN/src" CACHE PATH "Fuentes de LoRaLAN")
# Formula de tiempo en el aire: la misma que usa el gateway de la Raspberry Pi.
set(AIRTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../gatewayLoRaLAN/src/radio")

file(GLOB LIB_SOURCES CONFIGURE_DEPENDS "${LORALAN_SRC}/*.cpp")
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)

add_executable(sim_tests ${TEST_SOURCES} ${CORE_SOURCES} ${LIB_SOURCES} "${AIRTIME_DIR}/airtime.cpp")
target_include_directories(sim_tests PRIVATE shim core "${LORALAN_SRC}" "${AIRTIME_DIR}")
target_compile_options(sim_tests PRIVATE -Wall -Wextra -g)
target_link_options(sim_tests PRIVATE -static)

add_executable(sim_perf experiments/perf_main.cpp ${CORE_SOURCES} ${LIB_SOURCES} "${AIRTIME_DIR}/airtime.cpp")
target_include_directories(sim_perf PRIVATE shim core "${LORALAN_SRC}" "${AIRTIME_DIR}")
target_compile_options(sim_perf PRIVATE -Wall -Wextra -O2)
target_link_options(sim_perf PRIVATE -static)

# Red de 254 nodos: copia de la biblioteca con una sola constante cambiada. La
# biblioteca real no se toca; si la constante cambia de forma, la configuracion falla.
# Se regenera al cambiar cualquier fuente: si no, sim_perf254 mediria una version antigua.
file(GLOB LORALAN_ALL "${LORALAN_SRC}/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LORALAN_ALL})
set(LIB254 "${CMAKE_CURRENT_BINARY_DIR}/lib254")
file(REMOVE_RECURSE "${LIB254}")
file(COPY "${LORALAN_SRC}/" DESTINATION "${LIB254}")
file(READ "${LIB254}/LoRaLANGateway.h" GATEWAY_H)
string(FIND "${GATEWAY_H}" "kMaxNodes = 32" MAX_NODES_POS)
if(MAX_NODES_POS EQUAL -1)
    message(FATAL_ERROR "No se encuentra kMaxNodes = 32 en LoRaLANGateway.h")
endif()
string(REPLACE "kMaxNodes = 32" "kMaxNodes = 254" GATEWAY_H "${GATEWAY_H}")
file(WRITE "${LIB254}/LoRaLANGateway.h" "${GATEWAY_H}")
file(GLOB LIB254_SOURCES "${LIB254}/*.cpp")
add_executable(sim_perf254 experiments/perf_main.cpp ${CORE_SOURCES} ${LIB254_SOURCES} "${AIRTIME_DIR}/airtime.cpp")
target_include_directories(sim_perf254 PRIVATE shim core "${LIB254}" "${AIRTIME_DIR}")
target_compile_options(sim_perf254 PRIVATE -Wall -Wextra -O2)
target_link_options(sim_perf254 PRIVATE -static)
