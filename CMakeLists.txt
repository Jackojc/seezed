cmake_minimum_required(VERSION 3.26)
project(seezed CXX)

add_executable(seezed src/seezed.cpp)
target_compile_features(seezed PRIVATE cxx_std_20)

target_include_directories(seezed PUBLIC deps/conflict/include)
target_include_directories(seezed PUBLIC deps/fmt/include)
target_include_directories(seezed PUBLIC deps/libremidi/include)
target_include_directories(seezed PUBLIC include)

add_subdirectory(deps/libremidi/)
target_link_libraries(seezed PRIVATE libremidi)

add_subdirectory(deps/fmt/)
target_link_libraries(seezed PRIVATE fmt)

target_compile_options(seezed PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W4>
	$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
