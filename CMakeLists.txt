cmake_minimum_required(VERSION 3.31)
project(FastFileExplorer VERSION 1.0.0 LANGUAGES CXX)

# Use C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable optimizations in Release mode
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")

# Add Windows-specific flags
if(WIN32)
    # Define UNICODE and related macros for proper Windows API usage
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE)

    # Tell CMake this is a Windows GUI application (important for wWinMain)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
endif()

# Source files
file(GLOB_RECURSE SOURCES src/*.cpp)

# Header files
file(GLOB_RECURSE HEADERS src/*.hpp)

# Create executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Add Windows shell libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32 shlwapi comctl32)
endif()

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Installation
install(TARGETS ${PROJECT_NAME} DESTINATION bin)