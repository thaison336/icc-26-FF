# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	xG26_Explorerkit
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_xG26_Explorerkit target,
# which contains the content of the SLC project
include(xG26_Explorerkit.cmake)

add_executable(xG26_Explorerkit
    ../MAX30102_driver/MAX30105.cpp
    ../MAX30102_driver/heartRate.cpp
    ../MAX30102_driver/spo2_algorithm.cpp
    ../other_driver/i2c_ctl.cpp
    ../model/Interpreter.cpp
    ../model/ppg_compress_stat7.c
)

target_include_directories(xG26_Explorerkit PUBLIC
    ../MAX30102_driver
    ../other_driver
    ../model
)

target_include_directories(slc PUBLIC
    ../MAX30102_driver
    ../other_driver
    ../model
)

target_compile_definitions(xG26_Explorerkit PUBLIC
    # Add additional macros here
)

target_compile_options(xG26_Explorerkit PUBLIC
    # Set additional compiler flags here
)

target_link_options(xG26_Explorerkit PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(xG26_Explorerkit PRIVATE
    slc
)

# Include managed project content if available
include(xG26_Explorerkit_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(xG26_Explorerkit PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET xG26_Explorerkit
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:xG26_Explorerkit>" "$<TARGET_FILE_DIR:xG26_Explorerkit>/$<TARGET_FILE_BASE_NAME:xG26_Explorerkit>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:xG26_Explorerkit>" "$<TARGET_FILE_DIR:xG26_Explorerkit>/$<TARGET_FILE_BASE_NAME:xG26_Explorerkit>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:xG26_Explorerkit>" "$<TARGET_FILE_DIR:xG26_Explorerkit>/$<TARGET_FILE_BASE_NAME:xG26_Explorerkit>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET xG26_Explorerkit
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()