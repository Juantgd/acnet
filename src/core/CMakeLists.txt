add_library(corelib
  STATIC
  actor_module.cc
  actor_manager.cc
  actor_scheduler.cc
  plugin_manager.cc
  time_wheel.cc
  event_bus.cc
)

target_compile_definitions(corelib
  PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE>
)

target_link_libraries(corelib
  PRIVATE
    glaze::glaze
    quill::quill
)

add_executable(${PROJECT_NAME} main.cc)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_DL_LIBS}
    corelib
)
