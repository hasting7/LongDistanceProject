idf_component_register(
    SRCS
        "main.c"
        "disk_interface.c"
        "wifi_interface.c"
        "gpio_interface.c"
        "api_interface.c"
        "eink_driver.c"
        "bitmap_builder.c"
        "net_setup.c"
        "provisioning_interface.c"

    INCLUDE_DIRS "includes"

    EMBED_TXTFILES
        "portal/index.html"
        "portal/style.css"
)
