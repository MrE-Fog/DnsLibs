cmake_minimum_required(VERSION 3.1)
project(dnsproxy)

set(CMAKE_CXX_STANDARD 17)

set(DNSLIBS_DIR ..)
set(SRC_DIR src)
set(TEST_DIR test)
set(THIRD_PARTY_DIR ${DNSLIBS_DIR}/third-party)

set(SRCS
        ${SRC_DIR}/dnsproxy.cpp
        ${SRC_DIR}/dns64.cpp
    )

add_library(dnsproxy EXCLUDE_FROM_ALL ${SRCS})

# workaround for successful build with spdlog headers
if (NOT MSVC)
    target_compile_options(dnsproxy PRIVATE -Werror)
else ()
    add_definitions(-DFMT_EXCEPTIONS=0)
endif ()

if(NOT TARGET dnslibs_common)
    add_subdirectory(${DNSLIBS_DIR}/common ${CMAKE_BINARY_DIR}/common)
endif(NOT TARGET dnslibs_common)

if(NOT TARGET dnsfilter)
    add_subdirectory(${DNSLIBS_DIR}/dnsfilter ${CMAKE_BINARY_DIR}/dnsfilter)
endif(NOT TARGET dnsfilter)

if(NOT TARGET upstream)
    add_subdirectory(${DNSLIBS_DIR}/upstream ${CMAKE_BINARY_DIR}/upstream)
endif(NOT TARGET upstream)

if (NOT TARGET ldns)
    add_subdirectory(${THIRD_PARTY_DIR}/ldns ${CMAKE_BINARY_DIR}/ldns)
endif ()

set_target_properties(dnsproxy PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(dnsproxy PUBLIC ./include)
target_link_libraries(dnsproxy dnslibs_common dnsfilter upstream ldns)

target_compile_options(dnsproxy PRIVATE -Wall -Wextra -Wformat=2 -Wno-unused-parameter -Wno-unused-variable)
if (NOT MSVC)
    target_compile_options(dnsproxy PRIVATE -fno-exceptions)
endif()


enable_testing()
if (NOT TARGET tests)
    add_subdirectory(${THIRD_PARTY_DIR}/googletest ${CMAKE_BINARY_DIR}/googletest)
    add_custom_target(tests)
endif ()

link_libraries(gtest gtest_main dnsproxy)

add_executable(dns64_test EXCLUDE_FROM_ALL test/dns64_test.cpp)
add_test(dns64_test dns64_test)
add_dependencies(tests dns64_test)

add_executable(dnsproxy_test EXCLUDE_FROM_ALL test/dnsproxy_test.cpp)
add_test(dnsproxy_test dnsproxy_test)
add_dependencies(tests dnsproxy_test)