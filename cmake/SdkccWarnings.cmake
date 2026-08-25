add_library(sdkcc_warnings INTERFACE)

if(MSVC)
  target_compile_options(sdkcc_warnings INTERFACE /W4 /permissive- /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928)
else()
  target_compile_options(sdkcc_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    -Wformat=2 -Wundef -Wcast-align
    $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
    $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
  )
endif()

