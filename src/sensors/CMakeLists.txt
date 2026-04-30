target_sources(app PRIVATE
	bmp280.c
	ds18b20.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
