cmake_minimum_required(VERSION 3.16)
project(RayNeoOpenVRDriver LANGUAGES CXX)

# --- Architecture Detection ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(WIN32)
        set(STEAMVR_ARCH "win64")
    elseif(UNIX AND NOT APPLE)
        set(STEAMVR_ARCH "linux64")
    endif()
else()
    if(WIN32)
        set(STEAMVR_ARCH "win32")
    elseif(UNIX AND NOT APPLE)
        set(STEAMVR_ARCH "linux32")
    endif()
endif()

if(NOT DEFINED STEAMVR_ARCH)
    message(WARNING "Unknown architecture for SteamVR, defaulting to win64")
    set(STEAMVR_ARCH "win64")
endif()

# --- Output Directories ---
set(DRIVER_NAME "rayneo")
set(DRIVER_ROOT "${CMAKE_BINARY_DIR}/drivers/${DRIVER_NAME}")
set(DRIVER_BIN_DIR "${DRIVER_ROOT}/bin/${STEAMVR_ARCH}")

# 1. RayNeo SDK
set(RAYNEO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAYNEO_BUILD_OPENVR_DRIVER OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparties/rayneo-sdk)

# 2. OpenVR
set(OPENVR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparties/openvr/headers")

# 3. Driver Target
file(GLOB_RECURSE DRIVER_SOURCES "src/*.cpp" "src/*.h" "src/*.hpp")

add_library(driver_rayneo SHARED ${DRIVER_SOURCES})

target_include_directories(driver_rayneo PRIVATE ${OPENVR_INCLUDE_DIR})
target_link_libraries(driver_rayneo PRIVATE RayNeoSDK)

set_target_properties(driver_rayneo PROPERTIES 
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "driver_rayneo"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${DRIVER_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${DRIVER_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${DRIVER_BIN_DIR}"
)

# Set RPATH for Linux to find libRayNeoSDK.so in the same directory
if(UNIX AND NOT APPLE)
    set_target_properties(driver_rayneo PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# Fix for Visual Studio appending /Debug, /Release to the output path
if(CMAKE_CONFIGURATION_TYPES)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(driver_rayneo PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${DRIVER_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${DRIVER_BIN_DIR}"
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${DRIVER_BIN_DIR}"
        )
    endforeach()
endif()

if(WIN32)
    set_target_properties(driver_rayneo PROPERTIES SUFFIX ".dll")
else()
    set_target_properties(driver_rayneo PROPERTIES SUFFIX ".so")
endif()

# --- Post-Build Actions ---

# Copy manifest
add_custom_command(TARGET driver_rayneo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/driver.vrdrivermanifest"
    "${DRIVER_ROOT}/driver.vrdrivermanifest"
    COMMENT "Copying driver manifest to ${DRIVER_ROOT}"
)

# Copy resources (input profiles, bindings, images)
add_custom_command(TARGET driver_rayneo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DRIVER_ROOT}/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${DRIVER_ROOT}/resources"
    COMMENT "Copying driver resources to ${DRIVER_ROOT}/resources"
)

# Copy RayNeoSDK
add_custom_command(TARGET driver_rayneo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:RayNeoSDK>
    "${DRIVER_BIN_DIR}"
    COMMENT "Copying RayNeoSDK to ${DRIVER_BIN_DIR}"
)

# Copy libusb (Best effort for Windows)
if(WIN32)
    # Try to find libusb-1.0.dll using the same logic as RayNeoSDK or just searching
    find_program(LIBUSB_DLL "libusb-1.0.dll" PATHS 
        "${CMAKE_BINARY_DIR}/thirdparties/rayneo-sdk" 
        ENV PATH
    )
    if(LIBUSB_DLL)
        add_custom_command(TARGET driver_rayneo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBUSB_DLL}"
            "${DRIVER_BIN_DIR}"
            COMMENT "Copying libusb to ${DRIVER_BIN_DIR}"
        )
    endif()
endif()

# --- Prelauncher Tool ---
add_executable(rayneo_prelauncher
    src/tools/rayneo_prelauncher.cpp
)
set_target_properties(rayneo_prelauncher PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(rayneo_prelauncher PRIVATE RayNeoSDK)

# Set RPATH for Linux to find libRayNeoSDK.so in the same directory
if(UNIX AND NOT APPLE)
    set_target_properties(rayneo_prelauncher PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

add_custom_command(TARGET rayneo_prelauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:RayNeoSDK> $<TARGET_FILE_DIR:rayneo_prelauncher>
    COMMENT "Copying RayNeoSDK next to prelauncher"
)
if(WIN32)
    # Attempt to also copy libusb if SDK deployed it next to RayNeoSDK
    add_custom_command(TARGET rayneo_prelauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE_DIR:RayNeoSDK>/libusb-1.0.dll
        $<TARGET_FILE_DIR:rayneo_prelauncher>/libusb-1.0.dll
        COMMENT "Copying libusb next to prelauncher (best effort)"
    )
endif()

# --- Deploy Target ---
set(DEPLOY_SCRIPT "${CMAKE_BINARY_DIR}/deploy_driver.cmake")

file(WRITE "${DEPLOY_SCRIPT}" "
    set(DRIVER_ROOT \"${DRIVER_ROOT}\")
    set(DRIVER_NAME \"${DRIVER_NAME}\")
    
    if(WIN32)
        # Attempt to find SteamVR Install Location
        # 1. Try HKLM (64-bit view)
        get_filename_component(STEAMVR_PATH \"[HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\Steam App 250820;InstallLocation]\" ABSOLUTE)
        
        # 2. Try HKLM (32-bit view - WOW6432Node) if first failed or returned input
        if(NOT EXISTS \"\${STEAMVR_PATH}\")
             get_filename_component(STEAMVR_PATH \"[HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\WOW6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\Steam App 250820;InstallLocation]\" ABSOLUTE)
        endif()

        if(NOT EXISTS \"\${STEAMVR_PATH}\")
            message(FATAL_ERROR \"Could not locate SteamVR installation in Registry (HKLM...Steam App 250820). Cannot deploy.\")
        endif()
        
        set(DEPLOY_PARENT_DIR \"\${STEAMVR_PATH}/drivers\")
        
    elseif(UNIX AND NOT APPLE)
        # Linux: Deploy to ~/.config as requested
        set(DEPLOY_PARENT_DIR \"\$ENV{HOME}/.config\")
    endif()

    if(DEFINED DEPLOY_PARENT_DIR)
        message(STATUS \"Deploying \${DRIVER_NAME} to \${DEPLOY_PARENT_DIR}...\")
        
        # Clean old installation
        if(EXISTS \"\${DEPLOY_PARENT_DIR}/\${DRIVER_NAME}\")
            message(STATUS \"Removing old driver at \${DEPLOY_PARENT_DIR}/\${DRIVER_NAME}\")
            file(REMOVE_RECURSE \"\${DEPLOY_PARENT_DIR}/\${DRIVER_NAME}\")
        endif()
        
        # Copy new driver
        # file(COPY) copies the directory 'rayneo' into 'DEPLOY_PARENT_DIR'
        file(COPY \"\${DRIVER_ROOT}\" DESTINATION \"\${DEPLOY_PARENT_DIR}\")
        
        message(STATUS \"Deployment Success!\")
    else()
        message(WARNING \"Deploy target not configured for this platform.\")
    endif()
")

add_custom_target(deploy_driver
    COMMAND ${CMAKE_COMMAND} -P "${DEPLOY_SCRIPT}"
    DEPENDS driver_rayneo
    COMMENT "Deploying driver..."
)