# Portable protocol code: no ESP-IDF headers, also built by host_tests/.
idf_component_register(SRCS "adpcm.c" "jbuf.c" "packet.c"
                       INCLUDE_DIRS "include")
