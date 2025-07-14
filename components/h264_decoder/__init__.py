import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome import automation

CODEOWNERS = ["@your_github_username"]
DEPENDENCIES = ["esp32"]

h264_decoder_ns = cg.esphome_ns.namespace("h264_decoder")
H264DecoderComponent = h264_decoder_ns.class_("H264DecoderComponent", cg.Component)

DecodedFrame = h264_decoder_ns.struct("DecodedFrame")
DecodeFrameAction = h264_decoder_ns.class_("DecodeFrameAction", automation.Action)
FrameDecodedTrigger = h264_decoder_ns.class_(
    "FrameDecodedTrigger", 
    automation.Trigger.template(DecodedFrame)
)
DecodeErrorTrigger = h264_decoder_ns.class_(
    "DecodeErrorTrigger", 
    automation.Trigger.template(cg.std_string)
)

PixelFormat = h264_decoder_ns.enum("PixelFormat")
PIXEL_FORMATS = {
    "RGB565": PixelFormat.RGB565,
    "RGB888": PixelFormat.RGB888,
}

CONF_H264_DECODER = "h264_decoder"
CONF_FRAME_BUFFER_SIZE = "frame_buffer_size"
CONF_MAX_FRAME_WIDTH = "max_frame_width"
CONF_MAX_FRAME_HEIGHT = "max_frame_height"
CONF_PIXEL_FORMAT = "pixel_format"
CONF_ON_FRAME_DECODED = "on_frame_decoded"
CONF_ON_DECODE_ERROR = "on_decode_error"

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(H264DecoderComponent),
        cv.Optional(CONF_FRAME_BUFFER_SIZE): cv.positive_int,
        cv.Optional(CONF_MAX_FRAME_WIDTH, default=640): cv.positive_int,
        cv.Optional(CONF_MAX_FRAME_HEIGHT, default=480): cv.positive_int,
        cv.Optional(CONF_PIXEL_FORMAT, default="RGB565"): cv.enum(PIXEL_FORMATS, upper=True),
        cv.Optional(CONF_ON_FRAME_DECODED): automation.validate_automation({
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FrameDecodedTrigger),
        }),
        cv.Optional(CONF_ON_DECODE_ERROR): automation.validate_automation({
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DecodeErrorTrigger),
        }),
    }).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    cg.add(var.set_max_frame_size(
        config[CONF_MAX_FRAME_WIDTH],
        config[CONF_MAX_FRAME_HEIGHT]
    ))
    cg.add(var.set_pixel_format(config[CONF_PIXEL_FORMAT]))
    
    if CONF_FRAME_BUFFER_SIZE in config:
        cg.add(var.set_frame_buffer_size(config[CONF_FRAME_BUFFER_SIZE]))
    
    for conf in config.get(CONF_ON_FRAME_DECODED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(DecodedFrame, "frame")], conf)
    
    for conf in config.get(CONF_ON_DECODE_ERROR, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "error")], conf)

@automation.register_action(
    "h264_decoder.decode_frame",
    DecodeFrameAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(H264DecoderComponent),
        cv.Required("data"): cv.templatable(cv.string),
    }),
)
async def decode_frame_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    
    template_ = await cg.templatable(config["data"], args, cg.std_vector.template(cg.uint8))
    cg.add(var.set_data(template_))
    return var



