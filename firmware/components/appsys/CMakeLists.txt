# One application running: the interpreter's side of the contract, the
# application as a scene, the delivery of answers. What an application is
# apart from running it is appstore's.
idf_component_register(SRCS "app.c" "app_api_draw.c" "app_api_ui.c" "app_api_data.c" "app_api_time.c" "app_api_sys.c"
                            "app_scene.c" "app_delivery.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../../sdk"
                       REQUIRES appstore gfx ui words board
                       PRIV_REQUIRES wasm3 qr)

# The table of imports is made from the contract: one place holds a call's signature.
set(host_api "${CMAKE_CURRENT_LIST_DIR}/../../../sdk/host_api.h")
set(gen_imports "${CMAKE_CURRENT_LIST_DIR}/../../../tools/hostapi/gen_imports.py")
set(imports_dir "${CMAKE_CURRENT_BINARY_DIR}/imports")
idf_build_get_property(python PYTHON)
add_custom_command(OUTPUT "${imports_dir}/host_imports.inc" "${imports_dir}/system_imports.inc"
                          "${imports_dir}/host_exports.inc"
                   COMMAND "${python}" "${gen_imports}" "${host_api}" "${imports_dir}"
                   DEPENDS "${host_api}" "${gen_imports}"
                   VERBATIM)
add_custom_target(host_imports DEPENDS "${imports_dir}/host_imports.inc"
                                       "${imports_dir}/system_imports.inc"
                                       "${imports_dir}/host_exports.inc")
add_dependencies(${COMPONENT_LIB} host_imports)
target_include_directories(${COMPONENT_LIB} PRIVATE "${imports_dir}")
