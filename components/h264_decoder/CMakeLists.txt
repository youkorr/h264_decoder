set(COMPONENT_SRCS
    "h264_decoder.cpp"
)

set(COMPONENT_REQUIRES
    "esp_video_decoder"
    "esp_timer"
    "esp_log"
)

set(COMPONENT_PRIV_INCLUDEDIRS
    "."
)

register_component()
