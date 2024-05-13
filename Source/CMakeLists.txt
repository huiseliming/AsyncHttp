set(MODULE_NAME "AsyncHttp")
file(GLOB_RECURSE ${MODULE_NAME}_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
file(GLOB_RECURSE ${MODULE_NAME}_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${${MODULE_NAME}_HEADERS} ${${MODULE_NAME}_SOURCES}
)

add_executable(${MODULE_NAME} ${${MODULE_NAME}_HEADERS} ${${MODULE_NAME}_SOURCES})

target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenSSL REQUIRED)
target_link_libraries(${MODULE_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)

find_package(RapidJSON CONFIG REQUIRED)
target_link_libraries(${MODULE_NAME} PRIVATE rapidjson)

if(WIN32)
    set(Boost_USE_STATIC_LIBS ON)
else()
    set(Boost_USE_STATIC_LIBS OFF)
endif()
find_package(Boost COMPONENTS filesystem log url REQUIRED)
target_link_libraries(${MODULE_NAME} PUBLIC Boost::headers ${Boost_LIBRARIES})

# include(GenerateExportHeader)
# generate_export_header(${MODULE_NAME}
#     EXPORT_MACRO_NAME ASYNCHTTP_API
#     EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/${MODULE_NAME}.Api.h
# )
# target_precompile_headers(${MODULE_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/${MODULE_NAME}.Api.h)
