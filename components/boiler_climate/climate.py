import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import climate, sensor, switch, binary_sensor, number, select, i2c, text_sensor
from esphome.const import CONF_ID, CONF_SENSOR, CONF_MIN_TEMPERATURE, CONF_MAX_TEMPERATURE

DEPENDENCIES = ["climate"]
AUTO_LOAD = ["sensor", "switch", "binary_sensor", "number", "select"]

boiler_climate_ns = cg.esphome_ns.namespace("boiler_climate")
BoilerClimate = boiler_climate_ns.class_(
    "BoilerClimate", climate.Climate, cg.PollingComponent
)

PowerStep = boiler_climate_ns.enum("PowerStep", is_class=True)
POWER_STEPS = {
    "ECO":         PowerStep.ECO,
    "COMFORT":     PowerStep.COMFORT,
    "BOOST":       PowerStep.BOOST,
    "ANTI_FREEZE": PowerStep.ANTI_FREEZE,
}

BoilerClimateSetPowerStepAction = boiler_climate_ns.class_(
    "BoilerClimateSetPowerStepAction", automation.Action
)
BoilerClimateBstTurnOnAction = boiler_climate_ns.class_(
    "BoilerClimateBstTurnOnAction", automation.Action
)
BoilerClimateBstTurnOffAction = boiler_climate_ns.class_(
    "BoilerClimateBstTurnOffAction", automation.Action
)

BoilerClimateHeatTrigger      = boiler_climate_ns.class_("BoilerClimateHeatTrigger",      cg.esphome_ns.class_("Trigger<>"))
BoilerClimateIdleTrigger      = boiler_climate_ns.class_("BoilerClimateIdleTrigger",      cg.esphome_ns.class_("Trigger<>"))
BoilerClimateOffTrigger       = boiler_climate_ns.class_("BoilerClimateOffTrigger",       cg.esphome_ns.class_("Trigger<>"))
BoilerClimateBstStartTrigger  = boiler_climate_ns.class_("BoilerClimateBstStartTrigger",  cg.esphome_ns.class_("Trigger<>"))
BoilerClimateBstDoneTrigger   = boiler_climate_ns.class_("BoilerClimateBstDoneTrigger",   cg.esphome_ns.class_("Trigger<>"))
BoilerClimateNfOnTrigger      = boiler_climate_ns.class_("BoilerClimateNfOnTrigger",      cg.esphome_ns.class_("Trigger<>"))
BoilerClimateNfOffTrigger     = boiler_climate_ns.class_("BoilerClimateNfOffTrigger",     cg.esphome_ns.class_("Trigger<>"))
BoilerClimateClockSetTrigger  = boiler_climate_ns.class_("BoilerClimateClockSetTrigger",  cg.esphome_ns.class_("Trigger<uint8_t, uint8_t>"))
BoilerClimateTimerDoneTrigger = boiler_climate_ns.class_("BoilerClimateTimerDoneTrigger", cg.esphome_ns.class_("Trigger<>"))

CONF_RELAY_07KW          = "relay_07kw"
CONF_RELAY_13KW          = "relay_13kw"
CONF_DISPLAY             = "display"
CONF_CLICK_SWITCH        = "click_switch"
CONF_POWER_BUTTON        = "power_button"
CONF_TIMER_BUTTON        = "timer_button"
CONF_POTENTIOMETER       = "potentiometer"
CONF_BST_HOUR            = "bst_hour"
CONF_BST_WEEKDAY         = "bst_weekday"
CONF_STATUS_SENSOR       = "status_sensor"
CONF_POWER_SENSOR        = "power_sensor"
CONF_ENERGY_TODAY_SENSOR = "energy_today_sensor"
CONF_ENERGY_TOTAL_SENSOR = "energy_total_sensor"
CONF_HEAT_DEADBAND       = "heat_deadband"
CONF_HEAT_OVERRUN        = "heat_overrun"
CONF_DEFAULT_TARGET_TEMP = "default_target_temperature"
CONF_DEFAULT_POWER_STEP  = "default_power_step"
CONF_HEAT_ACTION         = "heat_action"
CONF_IDLE_ACTION         = "idle_action"
CONF_OFF_ACTION          = "off_action"
CONF_BST_START_ACTION    = "bst_start_action"
CONF_BST_DONE_ACTION     = "bst_done_action"
CONF_NF_ON_ACTION        = "nf_on_action"
CONF_NF_OFF_ACTION       = "nf_off_action"
CONF_TIMER_DONE_ACTION   = "on_timer_done"

def _action_schema(trigger_class):
    return automation.validate_automation({
        cv.GenerateID(automation.CONF_TRIGGER_ID): cv.declare_id(trigger_class),
    })

tm1650_ns = cg.esphome_ns.namespace("tm1650")
TM1650Display = tm1650_ns.class_("TM1650Display")

CONFIG_SCHEMA = climate.climate_schema(BoilerClimate).extend({
    cv.Required(CONF_SENSOR):        cv.use_id(sensor.Sensor),
    cv.Required(CONF_RELAY_07KW):    cv.use_id(switch.Switch),
    cv.Required(CONF_RELAY_13KW):    cv.use_id(switch.Switch),
    cv.Required(CONF_DISPLAY):       cv.use_id(TM1650Display),
    cv.Required(CONF_CLICK_SWITCH):  cv.use_id(binary_sensor.BinarySensor),
    cv.Required(CONF_POWER_BUTTON):  cv.use_id(binary_sensor.BinarySensor),
    cv.Required(CONF_TIMER_BUTTON):  cv.use_id(binary_sensor.BinarySensor),
    cv.Required(CONF_POTENTIOMETER): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_BST_HOUR):      cv.use_id(number.Number),
    cv.Optional(CONF_BST_WEEKDAY):   cv.use_id(select.Select),
    cv.Optional(CONF_STATUS_SENSOR):  cv.use_id(text_sensor.TextSensor),
    cv.Optional(CONF_POWER_SENSOR):   cv.use_id(sensor.Sensor),
    cv.Optional(CONF_ENERGY_TODAY_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_ENERGY_TOTAL_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_MIN_TEMPERATURE,     default=35.0): cv.temperature,
    cv.Optional(CONF_MAX_TEMPERATURE,     default=75.0): cv.temperature,
    cv.Optional(CONF_HEAT_DEADBAND,       default=1.0):  cv.temperature,
    cv.Optional(CONF_HEAT_OVERRUN,        default=0.5):  cv.temperature,
    cv.Optional(CONF_DEFAULT_TARGET_TEMP, default=50.0): cv.temperature,
    cv.Optional(CONF_DEFAULT_POWER_STEP,  default="ECO"):
        cv.enum(POWER_STEPS, upper=True),
    cv.Optional(CONF_HEAT_ACTION):      _action_schema(BoilerClimateHeatTrigger),
    cv.Optional(CONF_IDLE_ACTION):      _action_schema(BoilerClimateIdleTrigger),
    cv.Optional(CONF_OFF_ACTION):       _action_schema(BoilerClimateOffTrigger),
    cv.Optional(CONF_BST_START_ACTION): _action_schema(BoilerClimateBstStartTrigger),
    cv.Optional(CONF_BST_DONE_ACTION):  _action_schema(BoilerClimateBstDoneTrigger),
    cv.Optional(CONF_NF_ON_ACTION):     _action_schema(BoilerClimateNfOnTrigger),
    cv.Optional(CONF_NF_OFF_ACTION):    _action_schema(BoilerClimateNfOffTrigger),
    cv.Optional(CONF_TIMER_DONE_ACTION):_action_schema(BoilerClimateTimerDoneTrigger),
}).extend(cv.polling_component_schema("200ms"))

TRIGGER_GETTERS = [
    (CONF_HEAT_ACTION,      "get_heat_trigger"),
    (CONF_IDLE_ACTION,      "get_idle_trigger"),
    (CONF_OFF_ACTION,       "get_off_trigger"),
    (CONF_BST_START_ACTION, "get_bst_start_trigger"),
    (CONF_BST_DONE_ACTION,  "get_bst_done_trigger"),
    (CONF_NF_ON_ACTION,     "get_nf_on_trigger"),
    (CONF_NF_OFF_ACTION,    "get_nf_off_trigger"),
    (CONF_TIMER_DONE_ACTION,"get_timer_done_trigger"),
]


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    sens = await cg.get_variable(config[CONF_SENSOR])
    cg.add(var.set_sensor(sens))

    r07 = await cg.get_variable(config[CONF_RELAY_07KW])
    cg.add(var.set_relay_07kw(r07))

    r13 = await cg.get_variable(config[CONF_RELAY_13KW])
    cg.add(var.set_relay_13kw(r13))

    disp = await cg.get_variable(config[CONF_DISPLAY])
    cg.add(var.set_display(disp))

    cs = await cg.get_variable(config[CONF_CLICK_SWITCH])
    cg.add(var.set_click_switch(cs))

    pb = await cg.get_variable(config[CONF_POWER_BUTTON])
    cg.add(var.set_power_button(pb))

    tb = await cg.get_variable(config[CONF_TIMER_BUTTON])
    cg.add(var.set_timer_button(tb))

    pot = await cg.get_variable(config[CONF_POTENTIOMETER])
    cg.add(var.set_potentiometer(pot))

    if CONF_BST_HOUR in config:
        bh = await cg.get_variable(config[CONF_BST_HOUR])
        cg.add(var.set_bst_hour(bh))

    if CONF_POWER_SENSOR in config:
        ps = await cg.get_variable(config[CONF_POWER_SENSOR])
        cg.add(var.set_power_sensor(ps))

    if CONF_ENERGY_TODAY_SENSOR in config:
        et = await cg.get_variable(config[CONF_ENERGY_TODAY_SENSOR])
        cg.add(var.set_energy_today_sensor(et))

    if CONF_ENERGY_TOTAL_SENSOR in config:
        etot = await cg.get_variable(config[CONF_ENERGY_TOTAL_SENSOR])
        cg.add(var.set_energy_total_sensor(etot))

    if CONF_STATUS_SENSOR in config:
        ss = await cg.get_variable(config[CONF_STATUS_SENSOR])
        cg.add(var.set_status_sensor(ss))

    if CONF_BST_WEEKDAY in config:
        bw = await cg.get_variable(config[CONF_BST_WEEKDAY])
        cg.add(var.set_bst_weekday(bw))

    cg.add(var.set_min_temperature(config[CONF_MIN_TEMPERATURE]))
    cg.add(var.set_max_temperature(config[CONF_MAX_TEMPERATURE]))
    cg.add(var.set_heat_deadband(config[CONF_HEAT_DEADBAND]))
    cg.add(var.set_heat_overrun(config[CONF_HEAT_OVERRUN]))
    cg.add(var.set_default_target_temperature(config[CONF_DEFAULT_TARGET_TEMP]))
    cg.add(var.set_default_power_step(config[CONF_DEFAULT_POWER_STEP]))

    for conf_key, getter_name in TRIGGER_GETTERS:
        if conf_key not in config:
            continue
        for conf in config[conf_key]:
            trigger = cg.new_Pvariable(conf[automation.CONF_TRIGGER_ID])
            cg.add(getattr(var, getter_name)(trigger))
            await automation.build_automation(trigger, [], conf)


@automation.register_action(
    "boiler_climate.set_power_step",
    BoilerClimateSetPowerStepAction,
    cv.Schema({
        cv.GenerateID(): cv.use_id(BoilerClimate),
        cv.Required("power_step"): cv.templatable(cv.enum(POWER_STEPS, upper=True)),
    }),
    synchronous=True,
)
async def set_power_step_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    tmpl = await cg.templatable(config["power_step"], args, PowerStep)
    cg.add(var.set_power_step(tmpl))
    return var


@automation.register_action(
    "boiler_climate.bst_turn_on",
    BoilerClimateBstTurnOnAction,
    cv.Schema({cv.GenerateID(): cv.use_id(BoilerClimate)}),
    synchronous=True,
)
async def bst_turn_on_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "boiler_climate.bst_turn_off",
    BoilerClimateBstTurnOffAction,
    cv.Schema({cv.GenerateID(): cv.use_id(BoilerClimate)}),
    synchronous=True,
)
async def bst_turn_off_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)
