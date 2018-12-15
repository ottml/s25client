set(RTTR_Assert_Enabled 2 CACHE STRING "Status of RTTR assertions: 0=Disabled, 1=Enabled, 2=Default(Enabled only in debug)")

file(GLOB COMMON_SRC src/*.cpp)
file(GLOB COMMON_HEADERS include/*.h??)
file(GLOB COMMON_HELPERS_SRC src/helpers/*.cpp)
file(GLOB COMMON_HELPERS_HEADERS include/helpers/*.h??)
set(ALL_SRC ${COMMON_SRC} ${COMMON_HEADERS} ${COMMON_HELPERS_SRC} ${COMMON_HELPERS_HEADERS})

source_group(src FILES ${COMMON_SRC} ${COMMON_HEADERS})
source_group(helpers FILES ${COMMON_HELPERS_SRC} ${COMMON_HELPERS_HEADERS})

add_library(s25Common STATIC ${ALL_SRC})
target_include_directories(s25Common PUBLIC include)
target_include_directories(s25Common SYSTEM PUBLIC ${Boost_INCLUDE_DIR})
target_link_libraries(s25Common PUBLIC s25util)
set_property(TARGET s25Common PROPERTY POSITION_INDEPENDENT_CODE ON)

if(RTTR_Assert_Enabled EQUAL 0)
	target_compile_definitions(s25Common PUBLIC RTTR_ENABLE_ASSERTS=0)
elseif(RTTR_Assert_Enabled EQUAL 1)
	target_compile_definitions(s25Common PUBLIC RTTR_ENABLE_ASSERTS=1)
endif()
if(HAVE_STRUCT_TIMESPEC)
	target_compile_definitions(s25Common PUBLIC HAVE_STRUCT_TIMESPEC)
endif()

include(EnableWarnings)
enable_warnings(s25Common)