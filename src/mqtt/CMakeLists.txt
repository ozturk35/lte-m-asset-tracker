target_sources(app PRIVATE
  mqtt_client.c
  payload_queue.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
