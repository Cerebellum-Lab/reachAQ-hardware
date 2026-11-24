# Pellet signing: MCUboot embeds a public key given at build time, and the
# application is built unsigned. The release image is signed afterwards with a
# private key held outside every repository (tools/firmware_signing.py sign).
# See docs/firmware-signing-keys.md.

if(NOT SB_CONFIG_BOOT_SIGNATURE_TYPE_RSA)
  message(FATAL_ERROR "Pellet firmware uses RSA-2048 MCUboot signatures.")
endif()

if(NOT PYTHON_EXECUTABLE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
endif()

execute_process(
  COMMAND ${PYTHON_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/../../tools/firmware_signing.py
          check-public "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}"
  RESULT_VARIABLE pellet_key_check_result
  OUTPUT_VARIABLE pellet_key_check_output
  ERROR_VARIABLE pellet_key_check_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT pellet_key_check_result EQUAL 0)
  message(FATAL_ERROR
    "Pellet MCUboot public key rejected: ${pellet_key_check_error}\n"
    "Build with -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\\\"<absolute path to the active "
    "pellet public key>\\\" (quotes included), or set REACHAQ_PELLET_SIGNING_PUBKEY for "
    "tools/build_pellet_release.sh.")
endif()
message(STATUS "Pellet MCUboot public key: ${pellet_key_check_output}")

# The sysbuild defaults would sign the application with the public key. This
# script runs after them, so its settings win in .config.sysbuild.
set_property(TARGET ${DEFAULT_IMAGE} APPEND PROPERTY IMAGE_CONF_SCRIPT
             ${CMAKE_CURRENT_LIST_DIR}/sysbuild/unsigned_application.cmake)
