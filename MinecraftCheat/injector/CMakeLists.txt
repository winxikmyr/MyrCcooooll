add_executable(Injector main.cpp)

target_compile_definitions(Injector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _UNICODE
    UNICODE
)

set_target_properties(Injector PROPERTIES
    OUTPUT_NAME "Injector"
    CXX_STANDARD 17
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

if(MSVC)
    target_compile_options(Injector PRIVATE /utf-8)
endif()

target_link_libraries(Injector PRIVATE advapi32 wininet shell32 urlmon ole32)
