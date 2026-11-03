# SPDX-License-Identifier: MIT
if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM AND CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library()
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(screen.c screen_model.c font_5x7.c)
  # ZMK v0.2 otherwise removes the peripheral BAS event implementation together
  # with receiver battery polling. Keep events without sampling the receiver.
  if(NOT CONFIG_ZMK_BATTERY_REPORTING)
    zephyr_library_sources(${CMAKE_SOURCE_DIR}/src/events/battery_state_changed.c)
  endif()
endif()
