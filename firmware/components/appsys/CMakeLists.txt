idf_component_register(SRCS "app.c" "app_scene.c" "app_manifest.c" "app_store.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../../sdk"
                       REQUIRES gfx ui
                       PRIV_REQUIRES wasm3 json)
