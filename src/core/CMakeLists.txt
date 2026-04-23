# Copyright (c) 2026 juantgd. All Rights Reserved.

add_library(corelib
  STATIC
  mail_box.cc
  actor_module.cc
  actor_scheduler.cc
  time_wheel.cc
  event_bus.cc
)

set_target_properties(corelib PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(corelib
  PUBLIC
    glaze::glaze
    quill::quill
)

target_compile_definitions(corelib
  PUBLIC
  $<$<CONFIG:Debug>:DEBUG_MODE>
)

add_executable(${PROJECT_NAME}
  main.cc
  actor_manager.cc
  plugin_manager.cc
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_DL_LIBS}
    corelib
)
