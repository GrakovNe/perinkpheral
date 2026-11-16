# What an application is, before and apart from running it: its manifest,
# its files on the device, the room it takes, the catalogs it comes from,
# the polls it asks for and the requests it waits in line with. Pure: it
# knows no interpreter, no screen and nothing of ESP-IDF.
idf_component_register(SRCS "app_manifest.c" "app_store.c" "app_room.c" "catalog.c" "json_tree.c"
                            "text_template.c" "poll_schedule.c" "request_queue.c"
                            "../../../sdk/lib/json_pick.c"
                       INCLUDE_DIRS "include" "../../../sdk/lib"
                       PRIV_INCLUDE_DIRS "../../../sdk"
                       REQUIRES board)
