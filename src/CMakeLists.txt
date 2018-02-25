include_directories(
  "/usr/include/"
  "/usr/include/sword/"
  )

set(CMAKE_BUILD_TYPE Debug)
add_executable(convert convert.cpp)
find_library(sqlite3 SHARED IMPORTED)
find_library(sword SHARED IMPORTED)
target_link_libraries(convert sword sqlite3)

