idf_component_register(SRCS "src/connect.c" "src/tcp_server.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "tcpip_adapter")

register_component()