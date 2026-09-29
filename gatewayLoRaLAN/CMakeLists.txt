cmake_minimum_required(VERSION 3.18)
project(gatewayLoRaLAN CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Rutas a las dependencias externas --------------------------------------
# RADIOLIB_DIR : clon de https://github.com/jgromes/RadioLib
# LORALAN_DIR  : IMPLEMENTACIONMIA/libraries/LoRaLAN/src de este proyecto
#
# Se pueden fijar al invocar cmake:
#   cmake -DRADIOLIB_DIR=/home/pi/RadioLib -DLORALAN_DIR=/home/pi/LoRaLAN/src ..

set(RADIOLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../RadioLib" CACHE PATH "Ruta a RadioLib")
set(LORALAN_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../../IMPLEMENTACIONMIA/libraries/LoRaLAN/src" CACHE PATH "Ruta a la biblioteca LoRaLAN")

if(NOT EXISTS "${RADIOLIB_DIR}/src/RadioLib.h")
  message(FATAL_ERROR
    "No se encuentra RadioLib en ${RADIOLIB_DIR}\n"
    "Clonalo con:  git clone https://github.com/jgromes/RadioLib\n"
    "y pasa -DRADIOLIB_DIR=<ruta>")
endif()

if(NOT EXISTS "${LORALAN_DIR}/LoRaLANGateway.h")
  message(FATAL_ERROR
    "No se encuentra la biblioteca LoRaLAN en ${LORALAN_DIR}\n"
    "Pasa -DLORALAN_DIR=<ruta a libraries/LoRaLAN/src>")
endif()

add_subdirectory("${RADIOLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/RadioLib")

# --- Fuentes de la biblioteca LoRaLAN (se compilan SIN MODIFICAR) -----------
set(LORALAN_SOURCES
  "${LORALAN_DIR}/LoRaLANPacketCore.cpp"
  "${LORALAN_DIR}/LoRaLANPacketBuilders.cpp"
  "${LORALAN_DIR}/LoRaLANPacketMic.cpp"
  "${LORALAN_DIR}/LoRaLANDevice.cpp"
  "${LORALAN_DIR}/LoRaLANGateway.cpp"
)

# AppProtocol.h (mensajes de aplicacion) vive en el sketch del nodo: una sola
# definicion para los dos extremos.
set(APP_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Device_ESP32_LoRaLAN")

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/bridge.cpp
  src/compat/Arduino.cpp
  src/net/http_client.cpp
  src/radio/airtime.cpp
  ${LORALAN_SOURCES}
)

# El shim de Arduino va PRIMERO: la biblioteca LoRaLAN hace #include <Arduino.h>
# y debe resolverse contra src/compat/Arduino.h, no contra ninguna cabecera del
# sistema.
target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/compat"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${LORALAN_DIR}"
  "${APP_PROTOCOL_DIR}"
  "${RADIOLIB_DIR}/src"
)

find_library(LGPIO_LIB lgpio)
if(NOT LGPIO_LIB)
  message(FATAL_ERROR
    "No se encuentra liblgpio. Instalala con:\n"
    "  sudo apt install liblgpio-dev\n"
    "(en Raspberry Pi OS antiguo: compilar desde https://github.com/joan2937/lg)")
endif()

# mbedTLS proporciona el AES-128-CTR real. Sin ella, LoRaLANCrypto.h detecta su
# ausencia y aes128CtrCrypt devuelve false: el protocolo funcionaria pero SIN
# CIFRAR, lo que en este despliegue no es aceptable.
find_path(MBEDTLS_INCLUDE mbedtls/aes.h)
find_library(MBEDCRYPTO_LIB mbedcrypto)
if(NOT MBEDTLS_INCLUDE OR NOT MBEDCRYPTO_LIB)
  message(FATAL_ERROR
    "No se encuentra mbedTLS. Instalala con:\n"
    "  sudo apt install libmbedtls-dev")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE "${MBEDTLS_INCLUDE}")
target_link_libraries(${PROJECT_NAME} PRIVATE
  RadioLib
  ${LGPIO_LIB}
  ${MBEDCRYPTO_LIB}
  pthread
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

# Test de radio sin LoRaLAN:  make test_radio && sudo ./test_radio [rx]
add_executable(test_radio test_radio.cpp)
target_include_directories(test_radio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${RADIOLIB_DIR}/src")
target_link_libraries(test_radio PRIVATE RadioLib ${LGPIO_LIB})

# Autocomprobaciones sin hardware:  make test_airtime test_app_protocol test_bridge
add_executable(test_airtime test_airtime.cpp src/radio/airtime.cpp)
target_include_directories(test_airtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(test_app_protocol test_app_protocol.cpp)
target_include_directories(test_app_protocol PRIVATE "${APP_PROTOCOL_DIR}")
add_executable(test_bridge test_bridge.cpp src/bridge.cpp)
target_include_directories(test_bridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${APP_PROTOCOL_DIR}")
