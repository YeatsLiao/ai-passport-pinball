idf_component_register(
    SRCS "main.c"
         "pb_physics.c"
         "pb_table.c"
         "pb_game.c"
         "pb_render.c"
         "pb_audio.c"
         "pb_assets.c"
    INCLUDE_DIRS "."
    REQUIRES bsp nvs_flash
    EMBED_FILES "assets/pb_art.bin"
)
