# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	xG26_Devkit
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_xG26_Devkit target,
# which contains the content of the SLC project
include(xG26_Devkit.cmake)

add_executable(xG26_Devkit
    # Add additional sources here
    ../imu_driver/imu.cpp
    ../MAX30102_driver/MAX30105.cpp
    ../MAX30102_driver/MAX30102_manager.cpp
    ../other_driver/i2c_ctl.cpp
    ../sensor_hub/sensor_hub.cpp
    ../somniguard_layer/somniguard_dsp.cpp
    ../somniguard_layer/somniguard_motion.cpp
    ../somniguard_layer/somniguard_buffer.cpp
    ../somniguard_layer/somniguard_fsm.cpp
 
)

target_include_directories(xG26_Devkit PUBLIC
    # Add additional include paths here
    ../imu_driver
    ../MAX30102_driver
    ../other_driver
    ../sensor_hub
    ../somniguard_layer
)
target_include_directories(slc PUBLIC
    ../imu_driver
    ../MAX30102_driver
    ../other_driver
    ../sensor_hub
    ../somniguard_layer
)

target_compile_definitions(xG26_Devkit PUBLIC
    # Add additional macros here
)

target_compile_options(xG26_Devkit PUBLIC
    # Set additional compiler flags here
)

target_link_options(xG26_Devkit PUBLIC
    # Set additional linker flags here
    -u _printf_float
)

# Link with the content defined in the SLC project
target_link_libraries(xG26_Devkit PRIVATE
    slc
)

# Include managed project content if available
include(xG26_Devkit_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(xG26_Devkit PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET xG26_Devkit
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:xG26_Devkit>" "$<TARGET_FILE_DIR:xG26_Devkit>/$<TARGET_FILE_BASE_NAME:xG26_Devkit>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:xG26_Devkit>" "$<TARGET_FILE_DIR:xG26_Devkit>/$<TARGET_FILE_BASE_NAME:xG26_Devkit>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:xG26_Devkit>" "$<TARGET_FILE_DIR:xG26_Devkit>/$<TARGET_FILE_BASE_NAME:xG26_Devkit>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET xG26_Devkit
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()