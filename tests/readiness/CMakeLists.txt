if(NOT CONFIG_ARCH_POSIX OR NOT CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC)
  message(FATAL_ERROR "Device Info readiness fixture requires native Studio RPC")
endif()
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/readiness_test.c)
