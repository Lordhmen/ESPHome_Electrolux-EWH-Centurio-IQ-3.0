#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../tm1650/tm1650.h"

namespace esphome {
namespace boiler_climate {

enum class PowerStep : uint8_t {
  ECO         = 1,
  COMFORT     = 2,
  BOOST       = 3,
  ANTI_FREEZE = 4,
};

enum class BstState : uint8_t {
  OFF     = 0,
  RUNNING = 1,
};

enum class TimerState : uint8_t {
  IDLE          = 0,
  ARMED         = 1,
  SETTING_CLOCK = 2,
  SETTING_TIMER = 3,
};

struct BoilerNVS {
  float   target_temperature;
  uint8_t power_step;
  uint8_t timer_hour;
  uint8_t timer_minute;
  float   energy_today_wh;
  float   energy_total_wh;
  uint8_t energy_last_day;
};

static const char *const TAG = "boiler_climate";

class BoilerClimate : public climate::Climate, public PollingComponent {
 public:

  void set_sensor(sensor::Sensor *s)                        { sensor_ = s; }
  void set_relay_07kw(switch_::Switch *s)                   { relay_07kw_ = s; }
  void set_relay_13kw(switch_::Switch *s)                   { relay_13kw_ = s; }
  void set_display(tm1650::TM1650Display *d)                { display_ = d; }
  void set_click_switch(binary_sensor::BinarySensor *b)     { click_switch_ = b; }
  void set_power_button(binary_sensor::BinarySensor *b)     { power_button_ = b; }
  void set_timer_button(binary_sensor::BinarySensor *b)     { timer_button_ = b; }
  void set_potentiometer(sensor::Sensor *s)                 { potentiometer_ = s; }
  void set_bst_hour(number::Number *n)                      { bst_hour_ = n; }
  void set_bst_weekday(select::Select *s)                   { bst_weekday_ = s; }
  void set_status_sensor(text_sensor::TextSensor *s)        { status_sensor_ = s; }
  void set_power_sensor(sensor::Sensor *s)                   { power_sensor_ = s; }
  void set_energy_today_sensor(sensor::Sensor *s)            { energy_today_sensor_ = s; }
  void set_energy_total_sensor(sensor::Sensor *s)            { energy_total_sensor_ = s; }

  void set_min_temperature(float v)            { min_temp_ = v; }
  void set_max_temperature(float v)            { max_temp_ = v; }
  void set_heat_deadband(float v)              { heat_deadband_ = v; }
  void set_heat_overrun(float v)               { heat_overrun_ = v; }
  void set_default_target_temperature(float v) { default_target_ = v; }
  void set_default_power_step(PowerStep s)     { power_step_ = s; }

  PowerStep  get_power_step()       const { return power_step_; }
  bool       is_nf_heating()        const { return nf_heating_; }
  bool       is_bst_active()        const { return bst_state_ != BstState::OFF; }
  bool       is_bst_running()       const { return bst_state_ == BstState::RUNNING; }
  bool       is_timer_armed()       const { return timer_state_ == TimerState::ARMED; }
  bool       is_in_edit_mode()      const {
    return timer_state_ == TimerState::SETTING_CLOCK ||
           timer_state_ == TimerState::SETTING_TIMER;
  }
  uint8_t get_timer_hour()   const { return timer_hour_; }
  uint8_t get_timer_minute() const { return timer_minute_; }
  uint8_t get_edit_hour()    const { return edit_hour_; }
  uint8_t get_edit_minute()  const { return edit_minute_; }

  void set_power_step(PowerStep step);
  void reset_energy_total();
  void bst_turn_on();
  void bst_turn_off();

  void render_display(tm1650::TM1650Display &it);

  void get_heat_trigger(Trigger<> *t)                       { heat_trigger_ = t; }
  void get_idle_trigger(Trigger<> *t)                       { idle_trigger_ = t; }
  void get_off_trigger(Trigger<> *t)                        { off_trigger_ = t; }
  void get_bst_start_trigger(Trigger<> *t)                  { bst_start_trigger_ = t; }
  void get_bst_done_trigger(Trigger<> *t)                   { bst_done_trigger_ = t; }
  void get_nf_on_trigger(Trigger<> *t)                      { nf_on_trigger_ = t; }
  void get_nf_off_trigger(Trigger<> *t)                     { nf_off_trigger_ = t; }
  void get_clock_set_trigger(Trigger<uint8_t, uint8_t> *t)  { clock_set_trigger_ = t; }
  void get_timer_done_trigger(Trigger<> *t)                 { timer_done_trigger_ = t; }

  void setup() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:

  void update_relays_();
  void apply_action_(climate::ClimateAction a);
  void update_nf_();
  void save_nvs_();

  static climate::ClimatePreset step_to_preset(PowerStep s) {
    switch (s) {
      case PowerStep::COMFORT:     return climate::CLIMATE_PRESET_COMFORT;
      case PowerStep::BOOST:       return climate::CLIMATE_PRESET_BOOST;
      case PowerStep::ANTI_FREEZE: return climate::CLIMATE_PRESET_AWAY;
      default:                     return climate::CLIMATE_PRESET_ECO;
    }
  }
  static PowerStep preset_to_step(climate::ClimatePreset p) {
    switch (p) {
      case climate::CLIMATE_PRESET_COMFORT: return PowerStep::COMFORT;
      case climate::CLIMATE_PRESET_BOOST:   return PowerStep::BOOST;
      case climate::CLIMATE_PRESET_AWAY:    return PowerStep::ANTI_FREEZE;
      default:                              return PowerStep::ECO;
    }
  }

  void bst_start_cycle_();
  void bst_finish_cycle_();
  void bst_tick_();

  void check_timer_();
  void enter_edit_mode_(TimerState mode);
  void exit_edit_mode_(bool apply);
  void check_edit_timeout_();
  void check_bst_schedule_();
  void update_energy_();
  void check_errors_();
  void cancel_timer_();
  void set_system_time_(uint8_t hour, uint8_t minute);

  void on_click_switch_state_(bool state);
  void on_power_button_press_();
  void on_power_button_release_();
  void on_timer_button_press_();
  void on_timer_button_release_();
  void on_potentiometer_value_(float raw);
  void power_button_tick_();
  void timer_button_tick_();

  void display_tick_();

  sensor::Sensor              *sensor_       {nullptr};
  switch_::Switch             *relay_07kw_   {nullptr};
  switch_::Switch             *relay_13kw_   {nullptr};
  tm1650::TM1650Display       *display_      {nullptr};
  binary_sensor::BinarySensor *click_switch_ {nullptr};
  binary_sensor::BinarySensor *power_button_ {nullptr};
  binary_sensor::BinarySensor *timer_button_ {nullptr};
  sensor::Sensor              *potentiometer_{nullptr};
  number::Number              *bst_hour_     {nullptr};
  text_sensor::TextSensor     *status_sensor_      {nullptr};
  sensor::Sensor              *power_sensor_        {nullptr};
  sensor::Sensor              *energy_today_sensor_ {nullptr};
  sensor::Sensor              *energy_total_sensor_ {nullptr};
  select::Select              *bst_weekday_  {nullptr};

  float min_temp_      {35.0f};
  float max_temp_      {75.0f};
  float heat_deadband_ {1.0f};
  float heat_overrun_  {0.5f};
  float default_target_{50.0f};

  PowerStep power_step_   {PowerStep::ECO};
  bool nf_heating_        {false};
  bool overheat_latch_    {false};

  BstState bst_state_     {BstState::OFF};
  float    bst_saved_temp_{50.0f};
  int      bst_hold_sec_  {0};

  TimerState timer_state_      {TimerState::IDLE};
  uint8_t    timer_hour_       {0};
  uint8_t    timer_minute_     {0};
  uint8_t    edit_hour_        {0};
  uint8_t    edit_minute_      {0};
  uint32_t   edit_last_ms_     {0};
  bool       timer_fired_      {false};
  static constexpr uint32_t EDIT_TIMEOUT_MS = 10000;

  uint32_t power_press_start_ {0};
  uint32_t timer_press_start_ {0};
  uint32_t power_tick_last_   {0};
  uint32_t timer_tick_last_   {0};
  bool     power_held_        {false};
  bool     timer_held_        {false};

  float    energy_today_wh_   {0.0f};
  float    energy_total_wh_   {0.0f};
  uint8_t  energy_last_day_   {255};
  uint32_t energy_last_pub_ms_{0};

  uint32_t bst_start_ms_           {0};
  std::string last_status_         {"OK"};

  float last_pot_value_       {0.0f};

  bool     show_power_mode_       {false};
  bool     show_pot_mode_         {false};
  uint32_t display_timeout_ms_    {0};
  bool     display_blink_on_      {true};
  uint32_t display_blink_last_ms_ {0};
  uint32_t display_time_cache_ms_ {0};
  uint8_t  display_time_hour_     {0};
  uint8_t  display_time_minute_   {0};
  uint32_t display_update_last_   {0};

  ESPPreferenceObject pref_;

  Trigger<> *heat_trigger_      {nullptr};
  Trigger<> *idle_trigger_      {nullptr};
  Trigger<> *off_trigger_       {nullptr};
  Trigger<> *bst_start_trigger_ {nullptr};
  Trigger<> *bst_done_trigger_  {nullptr};
  Trigger<> *nf_on_trigger_     {nullptr};
  Trigger<> *nf_off_trigger_    {nullptr};
  Trigger<uint8_t, uint8_t> *clock_set_trigger_  {nullptr};
  Trigger<> *timer_done_trigger_ {nullptr};
};

class BoilerClimateHeatTrigger      : public Trigger<> {};
class BoilerClimateIdleTrigger      : public Trigger<> {};
class BoilerClimateOffTrigger       : public Trigger<> {};
class BoilerClimateBstStartTrigger  : public Trigger<> {};
class BoilerClimateBstDoneTrigger   : public Trigger<> {};
class BoilerClimateNfOnTrigger      : public Trigger<> {};
class BoilerClimateNfOffTrigger     : public Trigger<> {};
class BoilerClimateTimerDoneTrigger : public Trigger<> {};

template<typename... Ts>
class BoilerClimateSetPowerStepAction : public Action<Ts...> {
 public:
  explicit BoilerClimateSetPowerStepAction(BoilerClimate *c) : climate_(c) {}
  TEMPLATABLE_VALUE(PowerStep, power_step)
  void play(Ts... x) override { climate_->set_power_step(power_step_.value(x...)); }
 protected:
  BoilerClimate *climate_;
};

template<typename... Ts>
class BoilerClimateBstTurnOnAction : public Action<Ts...> {
 public:
  explicit BoilerClimateBstTurnOnAction(BoilerClimate *c) : climate_(c) {}
  void play(Ts... x) override { climate_->bst_turn_on(); }
 protected:
  BoilerClimate *climate_;
};

template<typename... Ts>
class BoilerClimateBstTurnOffAction : public Action<Ts...> {
 public:
  explicit BoilerClimateBstTurnOffAction(BoilerClimate *c) : climate_(c) {}
  void play(Ts... x) override { climate_->bst_turn_off(); }
 protected:
  BoilerClimate *climate_;
};

}
}
