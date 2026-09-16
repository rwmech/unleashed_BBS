# File:        src/CMakeLists.txt
# Description: ESP-IDF component for the application sources.
#              PlatformIO registers src/ as an extra component (not "main"),
#              so IDF dependencies are listed explicitly. LittleFS comes from
#              the Component Manager (src/idf_component.yml, requirement is
#              injected automatically) or from a vendored copy in components/.
FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

set(app_requires esp_wifi esp_netif esp_event nvs_flash esp_timer esp_hw_support heap lwip)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../components/esp_littlefs")
    list(APPEND app_requires esp_littlefs)     # vendored fallback
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "../include"
                       REQUIRES ${app_requires})
