import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.const import (
    CONF_ID,
    CONF_TRIGGER_ID,
    DEVICE_CLASS_EMPTY,
    ENTITY_CATEGORY_NONE,
)
from esphome.core import CORE, coroutine_with_priority
from esphome import automation

DEPENDENCIES = ["esp32"]

h264_decoder_ns = cg.esphome_ns.namespace("h264_decoder")
H264DecoderComponent = h264_decoder_ns.class_("H264DecoderComponent", cg.Component)

# Struct DecodedFrame
DecodedFrame = h264_decoder_ns.struct("DecodedFrame")

# Actions
DecodeFrameAction = h264_decoder_ns.class_("DecodeFrameAction", automation.Action)

# Triggers
FrameDecodedTrigger = h264_decoder_ns.class_("FrameDecodedTrigger", automation.Trigger.template(DecodedFrame.operator("ref")))
DecodeErrorTrigger = h264_decoder_ns.class_("DecodeErrorTrigger", automation.Trigger.template(cg.std_string.operator("const").operator("ref")))

# Enums
PixelFormat = h264_decoder_ns.enum("PixelFormat")
PIXEL_FORMATS = {
    "RGB565": PixelFormat.RGB565,
    "RGB888": PixelFormat.RGB888,
}

# Configuration keys
CONF_H264_DECODER_ID = "h264_decoder_id"
CONF_FRAME_BUFFER_SIZE = "frame_buffer_size"
CONF_MAX_FRAME_WIDTH = "max_frame_width"
CONF_MAX_FRAME_HEIGHT = "max_frame_height"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_ON_FRAME_DECODED = "on_frame_decoded"
CONF_ON_DECODE_ERROR = "on_decode_error"
CONF_H264_DATA = "h264_data"
CONF_DATA_SIZE = "data_size"

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(H264DecoderComponent),
            cv.Optional(CONF_FRAME_BUFFER_SIZE): cv.positive_int,
            cv.Optional(CONF_MAX_FRAME_WIDTH, default=640): cv.positive_int,
            cv.Optional(CONF_MAX_FRAME_HEIGHT, default=480): cv.positive_int,
            cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.enum(
                PIXEL_FORMATS, upper=True
            ),
            cv.Optional(CONF_ON_FRAME_DECODED): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FrameDecodedTrigger),
                }
            ),
            cv.Optional(CONF_ON_DECODE_ERROR): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DecodeErrorTrigger),
                }
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Configuration des paramètres
    if CONF_FRAME_BUFFER_SIZE in config:
        cg.add(var.set_frame_buffer_size(config[CONF_FRAME_BUFFER_SIZE]))
    
    cg.add(var.set_max_frame_size(
        config[CONF_MAX_FRAME_WIDTH], 
        config[CONF_MAX_FRAME_HEIGHT]
    ))
    
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    
    # Configuration des triggers
    for conf in config.get(CONF_ON_FRAME_DECODED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(DecodedFrame.operator("ref"), "frame")], conf)
    
    for conf in config.get(CONF_ON_DECODE_ERROR, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.const_char_ptr, "error")], conf)

# Action pour décoder une frame
@automation.register_action(
    "h264_decoder.decode_frame",
    DecodeFrameAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(H264DecoderComponent),
            cv.Required(CONF_H264_DATA): cv.templatable(cv.string),
            cv.Optional(CONF_DATA_SIZE): cv.templatable(cv.positive_int),
        }
    ),
)
async def decode_frame_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    
    template_ = await cg.templatable(config[CONF_H264_DATA], args, cg.std_vector.template(cg.uint8))
    cg.add(var.set_h264_data(template_))
    
    if CONF_DATA_SIZE in config:
        template_ = await cg.templatable(config[CONF_DATA_SIZE], args, cg.size_t)
        cg.add(var.set_data_size(template_))
    
    return var



