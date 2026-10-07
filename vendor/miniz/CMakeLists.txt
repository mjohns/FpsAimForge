add_library(miniz
  miniz.h
  miniz.c)

target_include_directories(miniz PUBLIC .)
target_compile_features(miniz PRIVATE cxx_std_20)
