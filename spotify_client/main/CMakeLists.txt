# Embed the server root certificate into the final binary
#
# (If this was a component, we would set COMPONENT_EMBED_TXTFILES here.)
idf_component_register(SRCS "main.c" "parseobjects.c" "parsejson.c" "strlib.c" "spotifyclient.c"
    INCLUDE_DIRS "include"
    EMBED_TXTFILES spotify_com_root_cert.pem)

register_component()