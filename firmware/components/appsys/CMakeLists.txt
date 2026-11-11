idf_component_register(SRCS "app.c" "app_scene.c" "app_manifest.c" "json_pick.c" "app_delivery.c" "catalog.c" "app_store.c"
                            "text_template.c" "poll_schedule.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../../sdk"
                       REQUIRES gfx ui
                       PRIV_REQUIRES wasm3 json qr)
