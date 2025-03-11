zephyr_library_sources(
    src/slider_module.c
    target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_SLIDER app PRIVATE src/behaviors/behavior_slider.c)
    zephyr_include_directories(include)
)