#include "boiler_climate.h"
#include "esphome/core/log.h"

namespace esphome {
namespace boiler_climate {

static constexpr float    NF_ON_TEMP    = 4.0f;
static constexpr float    NF_OFF_TEMP   = 5.0f;
static constexpr float    BST_TARGET    = 70.0f;
static constexpr float    BST_MIN_TEMP  = 69.5f;
static constexpr int      BST_HOLD_SEC  = 1200;
static constexpr float    OVERHEAT_TEMP = 85.0f;
static constexpr uint32_t DISPLAY_TIMEOUT_MS   = 2000;
static constexpr uint32_t DISPLAY_BLINK_MS     = 500;
static constexpr uint32_t DISPLAY_UPDATE_MS    = 200;
static constexpr uint32_t POWER_LONG_PRESS_MS  = 3000;
static constexpr uint32_t TIMER_LONG_PRESS_MS  = 5000;
static constexpr uint32_t BOTH_LONG_PRESS_MS   = 800;
static constexpr uint32_t POWER_TICK_MS        = 500;
static constexpr uint32_t TIMER_TICK_MS        = 250;
static constexpr uint32_t TIME_CACHE_MS        = 10000;

void BoilerClimate::setup() {
  pref_ = global_preferences->make_preference<BoilerNVS>(
      this->get_object_id_hash() ^ 0xB01EC001UL);

  BoilerNVS saved{};
  if (pref_.load(&saved)) {
    if (saved.target_temperature >= min_temp_ &&
        saved.target_temperature <= max_temp_) {
      this->target_temperature = saved.target_temperature;
    } else {
      this->target_temperature = default_target_;
    }
    uint8_t s = saved.power_step;
    power_step_ = (s >= 1 && s <= 4) ? static_cast<PowerStep>(s) : PowerStep::ECO;
    if (power_step_ == PowerStep::ANTI_FREEZE) power_step_ = PowerStep::ECO;
    timer_hour_      = saved.timer_hour   < 24 ? saved.timer_hour   : 0;
    timer_minute_    = saved.timer_minute < 60 ? saved.timer_minute : 0;
    energy_today_wh_ = saved.energy_today_wh >= 0.0f ? saved.energy_today_wh : 0.0f;
    energy_total_wh_ = saved.energy_total_wh >= 0.0f ? saved.energy_total_wh : 0.0f;
    energy_last_day_ = saved.energy_last_day;
  } else {
    this->target_temperature = default_target_;
    power_step_   = PowerStep::ECO;
    timer_hour_   = 0;
    timer_minute_ = 0;
  }

  this->mode   = climate::CLIMATE_MODE_OFF;
  this->action = climate::CLIMATE_ACTION_OFF;
  this->preset = step_to_preset(power_step_);

  if (sensor_) {
    sensor_->add_on_state_callback([this](float t) {
      this->current_temperature = t;
      update_relays_();
      this->publish_state();
    });
    if (sensor_->has_state())
      this->current_temperature = sensor_->state;
  }

  if (click_switch_) {
    click_switch_->add_on_state_callback([this](bool state) {
      on_click_switch_state_(state);
    });
  }

  if (power_button_) {
    power_button_->add_on_state_callback([this](bool state) {
      if (state) on_power_button_press_();
      else       on_power_button_release_();
    });
  }

  if (timer_button_) {
    timer_button_->add_on_state_callback([this](bool state) {
      if (state) on_timer_button_press_();
      else       on_timer_button_release_();
    });
  }

  if (potentiometer_) {
    potentiometer_->add_on_state_callback([this](float raw) {
      on_potentiometer_value_(raw);
    });
  }

  if (click_switch_ && click_switch_->has_state()) {
    this->mode = click_switch_->state
                 ? climate::CLIMATE_MODE_HEAT
                 : climate::CLIMATE_MODE_OFF;
  }

  this->publish_state();

  if (status_sensor_) {
    status_sensor_->publish_state("OK");
  }
  if (power_sensor_)        power_sensor_->publish_state(0.0f);
  if (energy_today_sensor_) energy_today_sensor_->publish_state(energy_today_wh_);
  if (energy_total_sensor_) energy_total_sensor_->publish_state(energy_total_wh_ / 1000.0f);

  ESP_LOGI(TAG, "setup: уставка=%.1f°C ступень=%d таймер=%02d:%02d",
           this->target_temperature, static_cast<int>(power_step_),
           timer_hour_, timer_minute_);
}

void BoilerClimate::update() {
  uint32_t now_ms = millis();

  static uint32_t bst_last = 0;
  if (now_ms - bst_last >= 1000) {
    bst_last = now_ms;
    if (bst_state_ == BstState::RUNNING) bst_tick_();
    if (timer_state_ == TimerState::ARMED) check_timer_();
    if (is_in_edit_mode()) check_edit_timeout_();
    check_bst_schedule_();
    check_errors_();
    update_energy_();
  }

  if (power_held_ && now_ms - power_tick_last_ >= POWER_TICK_MS) {
    power_tick_last_ = now_ms;
    power_button_tick_();
  }
  if (timer_held_ && now_ms - timer_tick_last_ >= TIMER_TICK_MS) {
    timer_tick_last_ = now_ms;
    timer_button_tick_();
  }

  if ((show_power_mode_ || show_pot_mode_) &&
      now_ms - display_timeout_ms_ >= DISPLAY_TIMEOUT_MS) {
    show_power_mode_ = false;
    show_pot_mode_   = false;
  }

  if (is_in_edit_mode() && now_ms - display_blink_last_ms_ >= DISPLAY_BLINK_MS) {
    display_blink_last_ms_ = now_ms;
    display_blink_on_ = !display_blink_on_;
  }

  if (display_ && now_ms - display_update_last_ >= DISPLAY_UPDATE_MS) {
    display_update_last_ = now_ms;
    render_display(*display_);
  }
}

climate::ClimateTraits BoilerClimate::traits() {
  auto t = climate::ClimateTraits();
  t.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT,
  });
  t.set_supported_presets({
      climate::CLIMATE_PRESET_ECO,
      climate::CLIMATE_PRESET_COMFORT,
      climate::CLIMATE_PRESET_BOOST,
      climate::CLIMATE_PRESET_AWAY,
  });
  t.set_visual_min_temperature(min_temp_);
  t.set_visual_max_temperature(max_temp_);
  t.set_visual_target_temperature_step(1.0f);
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION |
                      climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  return t;
}

void BoilerClimate::control(const climate::ClimateCall &call) {
  bool changed = false;

  if (call.get_mode().has_value()) {
    auto m = call.get_mode().value();
    if (this->mode != m) {
      this->mode = m;
      changed = true;
      if (m == climate::CLIMATE_MODE_OFF) {
        if (power_step_ == PowerStep::ANTI_FREEZE) {
          power_step_  = PowerStep::ECO;
          this->preset = climate::CLIMATE_PRESET_ECO;
          nf_heating_  = false;
        }
        if (bst_state_ == BstState::RUNNING) bst_finish_cycle_();
        bst_state_ = BstState::OFF;
        cancel_timer_();
      }
    }
  }

  if (call.get_target_temperature().has_value()) {
    float t = call.get_target_temperature().value();
    t = t < min_temp_ ? min_temp_ : (t > max_temp_ ? max_temp_ : t);
    if (fabsf(this->target_temperature - t) > 0.05f) {
      if (bst_state_ == BstState::RUNNING) {
        bst_saved_temp_ = t;
      } else {
        this->target_temperature = t;
        save_nvs_();
        changed = true;
      }
    }
  }

  if (call.get_preset().has_value()) {
    auto new_preset = call.get_preset().value();
    auto new_step   = preset_to_step(new_preset);
    bool was_nf     = (power_step_ == PowerStep::ANTI_FREEZE);

    if (new_step == PowerStep::ANTI_FREEZE) {
      if (!was_nf) {
        power_step_  = PowerStep::ANTI_FREEZE;
        this->preset = climate::CLIMATE_PRESET_AWAY;
        nf_heating_  = false;
        this->mode   = climate::CLIMATE_MODE_OFF;
        changed = true;
        save_nvs_();
        if (nf_on_trigger_) nf_on_trigger_->trigger();
      }
    } else {
      if (power_step_ != new_step || was_nf) {
        power_step_  = new_step;
        this->preset = new_preset;
        changed = true;
        save_nvs_();
        if (was_nf) {
          nf_heating_ = false;
          if (this->mode != climate::CLIMATE_MODE_HEAT)
            this->mode = climate::CLIMATE_MODE_HEAT;
          if (nf_off_trigger_) nf_off_trigger_->trigger();
        }
      }
    }
  }

  if (changed) {
    update_relays_();
    this->publish_state();
  }
}

void BoilerClimate::set_power_step(PowerStep step) {
  bool was_nf = (power_step_ == PowerStep::ANTI_FREEZE);
  if (step == PowerStep::ANTI_FREEZE) {
    if (!was_nf) {
      power_step_  = PowerStep::ANTI_FREEZE;
      this->preset = climate::CLIMATE_PRESET_AWAY;
      nf_heating_  = false;
      this->mode   = climate::CLIMATE_MODE_OFF;
      save_nvs_();
      if (nf_on_trigger_) nf_on_trigger_->trigger();
    }
  } else {
    power_step_  = step;
    this->preset = step_to_preset(step);
    save_nvs_();
    if (was_nf) {
      nf_heating_ = false;
      this->mode  = climate::CLIMATE_MODE_HEAT;
      if (nf_off_trigger_) nf_off_trigger_->trigger();
    }
  }
  update_relays_();
  this->publish_state();
}

void BoilerClimate::bst_turn_on() {
  if (this->mode == climate::CLIMATE_MODE_OFF &&
      power_step_ != PowerStep::ANTI_FREEZE) {
    ESP_LOGW(TAG, "BST: нельзя включить на выключенном приборе.");
    return;
  }
  if (bst_state_ != BstState::OFF) return;
  if (power_step_ == PowerStep::ANTI_FREEZE) set_power_step(PowerStep::ECO);
  bst_start_cycle_();
}

void BoilerClimate::bst_turn_off() {
  if (bst_state_ == BstState::OFF) return;
  if (bst_state_ == BstState::RUNNING) bst_finish_cycle_();
  bst_state_ = BstState::OFF;
  update_relays_();
  this->publish_state();
}

void BoilerClimate::bst_start_cycle_() {
  bst_saved_temp_ = this->target_temperature;
  bst_hold_sec_   = 0;
  bst_state_      = BstState::RUNNING;
  bst_start_ms_   = millis();
  this->target_temperature = BST_TARGET;
  this->mode = climate::CLIMATE_MODE_HEAT;
  update_relays_();
  this->publish_state();
  if (bst_start_trigger_) bst_start_trigger_->trigger();
  ESP_LOGI(TAG, "BST: цикл запущен, сохранена уставка %.1f°C", bst_saved_temp_);
}

void BoilerClimate::bst_finish_cycle_() {
  bst_hold_sec_ = 0;
  bst_state_    = BstState::OFF;
  this->target_temperature = bst_saved_temp_;
  save_nvs_();
  update_relays_();
  this->publish_state();
  if (bst_done_trigger_) bst_done_trigger_->trigger();
  ESP_LOGI(TAG, "BST: цикл завершён, уставка %.1f°C", bst_saved_temp_);
}

void BoilerClimate::bst_tick_() {
  if (std::isnan(this->current_temperature)) return;
  float t = this->current_temperature;
  if (t >= BST_MIN_TEMP) {
    bst_hold_sec_++;
    ESP_LOGD(TAG, "BST: %d/%d сек (%.1f°C)", bst_hold_sec_, BST_HOLD_SEC, t);
  } else {
    if (bst_hold_sec_ > 0) {
      ESP_LOGI(TAG, "BST: упала до %.1f°C, сброс счётчика", t);
      bst_hold_sec_ = 0;
    }
  }
  if (bst_hold_sec_ >= BST_HOLD_SEC) bst_finish_cycle_();
}

void BoilerClimate::check_bst_schedule_() {
  time_t now_t = ::time(nullptr);
  if (now_t < 1000000) return;
  struct tm *lt = localtime(&now_t);
  if (!lt) return;

  if (lt->tm_min != 0 || lt->tm_sec != 0) return;

  if (bst_hour_ && lt->tm_hour != (int)bst_hour_->state) return;

  if (bst_weekday_) {
    int dow = lt->tm_wday == 0 ? 7 : lt->tm_wday;
    int target_dow = 7;
    auto day = bst_weekday_->current_option();
    if      (day == "Понедельник") target_dow = 1;
    else if (day == "Вторник")     target_dow = 2;
    else if (day == "Среда")       target_dow = 3;
    else if (day == "Четверг")     target_dow = 4;
    else if (day == "Пятница")     target_dow = 5;
    else if (day == "Суббота")     target_dow = 6;
    if (dow != target_dow) return;
  }

  if (this->mode == climate::CLIMATE_MODE_HEAT && !is_bst_active()) {
    ESP_LOGI(TAG, "BST: автозапуск по расписанию %02d:00", lt->tm_hour);
    bst_turn_on();
  }
}

void BoilerClimate::cancel_timer_() {
  if (timer_state_ == TimerState::IDLE) return;
  timer_state_ = TimerState::IDLE;
  timer_fired_ = false;
  ESP_LOGI(TAG, "Таймер сброшен");
}

void BoilerClimate::check_timer_() {
  time_t t = ::time(nullptr);
  if (t < 1000000) return;
  struct tm *lt = localtime(&t);
  if (!lt) return;

  if (lt->tm_hour == timer_hour_ && lt->tm_min == timer_minute_) {
    if (!timer_fired_) {
      timer_fired_ = true;
      timer_state_ = TimerState::IDLE;
      this->mode   = climate::CLIMATE_MODE_HEAT;
      update_relays_();
      this->publish_state();
      ESP_LOGI(TAG, "Таймер сработал!");
    }
  } else {
    if (timer_fired_ && lt->tm_min != timer_minute_) timer_fired_ = false;
  }
}

void BoilerClimate::enter_edit_mode_(TimerState mode) {
  timer_state_ = mode;
  if (mode == TimerState::SETTING_TIMER) {
    edit_hour_   = timer_hour_;
    edit_minute_ = timer_minute_;
    ESP_LOGI(TAG, "Редактирование таймера (%02d:%02d)", edit_hour_, edit_minute_);
  } else {
    edit_hour_   = 0;
    edit_minute_ = 0;
    ESP_LOGI(TAG, "Редактирование часов");
  }
  display_blink_on_      = true;
  display_blink_last_ms_ = millis();
  edit_last_ms_          = millis();
}

void BoilerClimate::exit_edit_mode_(bool apply) {
  if (!is_in_edit_mode()) return;
  TimerState prev = timer_state_;
  timer_state_ = TimerState::IDLE;
  display_blink_on_ = true;

  if (!apply) return;

  if (prev == TimerState::SETTING_TIMER) {
    timer_hour_   = edit_hour_;
    timer_minute_ = edit_minute_;
    save_nvs_();
    ESP_LOGI(TAG, "Таймер установлен: %02d:%02d", timer_hour_, timer_minute_);
  } else {
    set_system_time_(edit_hour_, edit_minute_);
  }
  this->publish_state();
}

void BoilerClimate::check_edit_timeout_() {
  uint32_t elapsed = millis() - edit_last_ms_;
  if (elapsed >= EDIT_TIMEOUT_MS && elapsed < 60000) {
    ESP_LOGI(TAG, "Таймаут редактирования (%d мс)", elapsed);
    exit_edit_mode_(true);
  }
}

void BoilerClimate::set_system_time_(uint8_t hour, uint8_t minute) {
  time_t utc_now = ::time(nullptr);
  if (utc_now < 1000000) return;
  struct tm utc_tm = {};
  gmtime_r(&utc_now, &utc_tm);
  struct tm local_tm = {};
  localtime_r(&utc_now, &local_tm);

  int diff_h = local_tm.tm_hour - utc_tm.tm_hour;
  if (diff_h > 12)  diff_h -= 24;
  if (diff_h < -12) diff_h += 24;
  int32_t tz_offset = diff_h * 3600;

  time_t new_utc = utc_now
                 - (utc_tm.tm_hour * 3600 + utc_tm.tm_min * 60 + utc_tm.tm_sec)
                 + ((int)hour * 3600 + (int)minute * 60)
                 - tz_offset;
  struct timeval tv = { .tv_sec = new_utc, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  ESP_LOGI(TAG, "Время установлено: %02d:%02d (tz=%dч)", hour, minute, diff_h);
}

void BoilerClimate::on_click_switch_state_(bool state) {
  if (is_in_edit_mode()) return;
  auto call = this->make_call();
  if (state) {
    call.set_mode(climate::CLIMATE_MODE_HEAT);
  } else {
    show_power_mode_ = false;
    show_pot_mode_   = false;
    call.set_mode(climate::CLIMATE_MODE_OFF);
  }
  call.perform();
}

void BoilerClimate::on_power_button_press_() {
  power_press_start_ = millis();
  power_tick_last_   = millis() + BOTH_LONG_PRESS_MS;
  power_held_        = true;
}

void BoilerClimate::on_power_button_release_() {
  power_held_ = false;
  uint32_t held = millis() - power_press_start_;

  if (timer_held_) return;

  if (is_in_edit_mode()) return;

  if (held >= POWER_LONG_PRESS_MS) {
    if (this->mode == climate::CLIMATE_MODE_OFF) return;
    if (is_bst_active()) bst_turn_off();
    else                 bst_turn_on();
  } else {
    if (this->mode == climate::CLIMATE_MODE_OFF &&
        power_step_ != PowerStep::ANTI_FREEZE) return;

    auto step = power_step_;
    auto call = this->make_call();

    if (step == PowerStep::ECO) {
      call.set_preset(climate::CLIMATE_PRESET_COMFORT);
    } else if (step == PowerStep::COMFORT) {
      call.set_preset(climate::CLIMATE_PRESET_BOOST);
    } else if (step == PowerStep::BOOST) {
      call.set_preset(climate::CLIMATE_PRESET_AWAY);
    } else if (step == PowerStep::ANTI_FREEZE) {
      call.set_preset(climate::CLIMATE_PRESET_ECO);
      call.set_mode(climate::CLIMATE_MODE_HEAT);
    }
    call.perform();

    show_power_mode_     = true;
    show_pot_mode_       = false;
    display_timeout_ms_  = millis();
  }
}

void BoilerClimate::power_button_tick_() {
  uint32_t held = millis() - power_press_start_;

  if (timer_held_ && held >= BOTH_LONG_PRESS_MS && !is_in_edit_mode()) {
    enter_edit_mode_(TimerState::SETTING_CLOCK);
    return;
  }

  if (is_in_edit_mode()) {
    edit_hour_ = (edit_hour_ + 1) % 24;
    edit_last_ms_ = millis();
  }
}

void BoilerClimate::on_timer_button_press_() {
  timer_press_start_ = millis();
  timer_tick_last_   = millis() + BOTH_LONG_PRESS_MS;
  timer_held_        = true;
}

void BoilerClimate::on_timer_button_release_() {
  timer_held_ = false;
  uint32_t held = millis() - timer_press_start_;

  if (is_in_edit_mode()) return;

  if (held >= TIMER_LONG_PRESS_MS) {
    if (this->mode != climate::CLIMATE_MODE_HEAT &&
        timer_state_ != TimerState::ARMED) return;
    enter_edit_mode_(TimerState::SETTING_TIMER);
    edit_last_ms_ = millis();
  } else {
    if (timer_state_ == TimerState::ARMED) {
      timer_state_ = TimerState::IDLE;
      timer_fired_ = false;
      this->mode   = climate::CLIMATE_MODE_HEAT;
      update_relays_();
      this->publish_state();
      ESP_LOGI(TAG, "Таймер выключен");
    } else if (timer_state_ == TimerState::IDLE) {
      if (this->mode != climate::CLIMATE_MODE_HEAT) return;
      timer_state_ = TimerState::ARMED;
      timer_fired_ = false;
      update_relays_();
      this->publish_state();
      ESP_LOGI(TAG, "Таймер взведён на %02d:%02d", timer_hour_, timer_minute_);
    }
  }
}

void BoilerClimate::timer_button_tick_() {
  if (is_in_edit_mode()) {
    edit_minute_ = (edit_minute_ + 1) % 60;
    edit_last_ms_ = millis();
  }
}

void BoilerClimate::on_potentiometer_value_(float raw) {
  if (click_switch_ && !click_switch_->state) return;

  float target = raw * (min_temp_ - max_temp_) / (2.33f - 0.15f)
                 + max_temp_ - 0.15f * (min_temp_ - max_temp_) / (2.33f - 0.15f);
  target = min_temp_ + (max_temp_ - min_temp_) * (2.33f - raw) / (2.33f - 0.15f);
  if (target < min_temp_) target = min_temp_;
  if (target > max_temp_) target = max_temp_;
  target = roundf(target);

  if (is_in_edit_mode()) {
    if (fabsf(last_pot_value_ - target) >= 1.0f) {
      last_pot_value_ = target;
      exit_edit_mode_(true);
    }
    return;
  }

  if (fabsf(last_pot_value_ - target) < 1.0f) return;
  last_pot_value_ = target;

  auto call = this->make_call();
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    call.set_mode(climate::CLIMATE_MODE_HEAT);
  }
  call.set_target_temperature(target);
  call.perform();

  show_pot_mode_      = true;
  show_power_mode_    = false;
  display_timeout_ms_ = millis();
}

void BoilerClimate::update_relays_() {
  if (!relay_07kw_ || !relay_13kw_) return;

  auto all_off = [this]() {
    relay_07kw_->turn_off();
    relay_13kw_->turn_off();
  };

  if (std::isnan(this->current_temperature)) {
    ESP_LOGE(TAG, "Датчик недоступен! ТЭНы отключены.");
    all_off();
    apply_action_(climate::CLIMATE_ACTION_OFF);
    return;
  }

  if (this->current_temperature > OVERHEAT_TEMP) {
    if (!overheat_latch_) {
      overheat_latch_ = true;
      ESP_LOGE(TAG, "ПЕРЕГРЕВ %.1f°C!", this->current_temperature);
    }
    all_off();
    apply_action_(climate::CLIMATE_ACTION_OFF);
    return;
  }
  overheat_latch_ = false;

  if (power_step_ == PowerStep::ANTI_FREEZE) {
    update_nf_();
    return;
  }

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    all_off();
    apply_action_(climate::CLIMATE_ACTION_OFF);
    return;
  }

  if (timer_state_ == TimerState::ARMED) {
    all_off();
    apply_action_(climate::CLIMATE_ACTION_IDLE);
    return;
  }

  if (timer_fired_) {
    if (this->current_temperature >= this->target_temperature + heat_overrun_) {
      timer_fired_ = false;
      this->mode   = climate::CLIMATE_MODE_OFF;
      all_off();
      apply_action_(climate::CLIMATE_ACTION_OFF);
      this->publish_state();
      if (timer_done_trigger_) timer_done_trigger_->trigger();
      ESP_LOGI(TAG, "Таймер: температура достигнута, выключаемся");
      return;
    }
  }

  float cur    = this->current_temperature;
  float target = this->target_temperature;
  climate::ClimateAction new_action = this->action;

  if (cur <= target - heat_deadband_)   new_action = climate::CLIMATE_ACTION_HEATING;
  else if (cur >= target + heat_overrun_) new_action = climate::CLIMATE_ACTION_IDLE;

  apply_action_(new_action);

  if (new_action == climate::CLIMATE_ACTION_HEATING) {
    switch (power_step_) {
      case PowerStep::ECO:
        relay_07kw_->turn_on();  relay_13kw_->turn_off(); break;
      case PowerStep::COMFORT:
        relay_07kw_->turn_off(); relay_13kw_->turn_on();  break;
      case PowerStep::BOOST:
        relay_07kw_->turn_on();  relay_13kw_->turn_on();  break;
      default: all_off(); break;
    }
  } else {
    all_off();
  }
}

void BoilerClimate::update_nf_() {
  float t = this->current_temperature;
  if (!nf_heating_ && t < NF_ON_TEMP) {
    nf_heating_ = true;
    ESP_LOGI(TAG, "nF ON: %.1f°C", t);
  } else if (nf_heating_ && t >= NF_OFF_TEMP) {
    nf_heating_ = false;
    ESP_LOGI(TAG, "nF OFF: %.1f°C", t);
  }
  if (nf_heating_) {
    relay_07kw_->turn_on(); relay_13kw_->turn_on();
    apply_action_(climate::CLIMATE_ACTION_HEATING);
  } else {
    relay_07kw_->turn_off(); relay_13kw_->turn_off();
    apply_action_(climate::CLIMATE_ACTION_IDLE);
  }
}

void BoilerClimate::apply_action_(climate::ClimateAction a) {
  if (this->action == a) return;
  this->action = a;
  switch (a) {
    case climate::CLIMATE_ACTION_HEATING:
      if (!heat_check_active_ && !std::isnan(this->current_temperature)) {
        heat_check_active_     = true;
        heat_check_start_temp_ = this->current_temperature;
        heat_check_start_ms_   = millis();
      }
      if (heat_trigger_) heat_trigger_->trigger();
      break;
    case climate::CLIMATE_ACTION_IDLE:
      heat_check_active_ = false;
      if (idle_trigger_) idle_trigger_->trigger();
      break;
    case climate::CLIMATE_ACTION_OFF:
      heat_check_active_ = false;
      if (off_trigger_) off_trigger_->trigger();
      break;
    default: break;
  }
}

void BoilerClimate::render_display(tm1650::TM1650Display &it) {
  if (is_in_edit_mode()) {
    if (display_blink_on_) {
      it.print_time(edit_hour_, edit_minute_);
    } else {
      it.clear();
    }
    return;
  }

  if (power_step_ == PowerStep::ANTI_FREEZE) {
    if (nf_heating_) {
      it.print(0, " nF ");
    } else {
      uint32_t phase = (millis() / 2000) % 2;
      if (phase == 0) {
        if (!std::isnan(this->current_temperature)) {
          it.printf(0, "%4.0f", this->current_temperature);
        } else {
          it.print(0, "----");
        }
      } else {
        it.print(0, " nF ");
      }
    }
    return;
  }

  if (is_bst_active()) {
    if (is_bst_running()) {
      it.print(0, " Sc ");
    } else {
      uint32_t phase_ms = millis() % 6000;
      if (phase_ms < 1000) {
        it.print(0, " Sc ");
      } else {
        if (!std::isnan(this->current_temperature)) {
          it.print(0, "  ");
          it.printf(2, "%2.0f", this->current_temperature);
        } else {
          it.print(0, "----");
        }
      }
    }
    return;
  }

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    it.clear();
    return;
  }

  int lvl = static_cast<int>(power_step_);

  if (show_pot_mode_) {
    it.print(0, "  ");
    it.printf(2, "%2.0f", this->target_temperature);
    return;
  }

  if (show_power_mode_) {
    it.printf(0, "%d", lvl);
    if (!std::isnan(this->current_temperature)) {
      it.printf(2, "%2.0f", this->current_temperature);
    } else {
      it.print(2, "--");
    }
    return;
  }

  if (timer_state_ == TimerState::ARMED) {
    uint32_t now_ms = millis();
    if (now_ms - display_time_cache_ms_ >= TIME_CACHE_MS || display_time_cache_ms_ == 0) {
      time_t tnow = ::time(nullptr);
      if (tnow > 1000000) {
        struct tm *lt = localtime(&tnow);
        if (lt) {
          display_time_hour_   = lt->tm_hour;
          display_time_minute_ = lt->tm_min;
        }
      }
      display_time_cache_ms_ = now_ms;
    }
    uint32_t phase = (now_ms / 2000) % 2;
    if (phase == 0) {
      it.print_time(display_time_hour_, display_time_minute_);
    } else {
      it.print_time(timer_hour_, timer_minute_);
    }
    return;
  }

  it.printf(0, "%d", lvl);
  if (!std::isnan(this->current_temperature)) {
    it.printf(2, "%2.0f", this->current_temperature);
  } else {
    it.print(2, "--");
  }
}

void BoilerClimate::save_nvs_() {
  BoilerNVS data{};
  data.target_temperature = this->target_temperature;
  data.power_step         = static_cast<uint8_t>(power_step_);
  data.timer_hour         = timer_hour_;
  data.timer_minute       = timer_minute_;
  data.energy_today_wh    = energy_today_wh_;
  data.energy_total_wh    = energy_total_wh_;
  data.energy_last_day    = energy_last_day_;
  pref_.save(&data);
}

void BoilerClimate::check_errors_() {
  std::string status = "OK";
  bool emergency_shutdown = false;

  if (std::isnan(this->current_temperature)) {
    status = "Датчик температуры недоступен";
    emergency_shutdown = true;
  }
  else if (this->current_temperature > OVERHEAT_TEMP) {
    status = "Перегрев";
    emergency_shutdown = true;
  }
  else if (::time(nullptr) < 1000000) {
    status = "Время не синхронизировано";
  }
  else if (bst_state_ == BstState::RUNNING &&
           millis() - bst_start_ms_ > 4UL * 3600UL * 1000UL) {
    status = "BST: нет нагрева более 4 часов";
    emergency_shutdown = true;
  }
  else if (heat_check_active_ &&
           millis() - heat_check_start_ms_ > 30UL * 60UL * 1000UL) {
    if (!std::isnan(this->current_temperature) &&
        this->current_temperature < heat_check_start_temp_ + 2.0f) {
      status = "Нет нагрева более 30 минут";
      emergency_shutdown = true;
    } else {
      heat_check_active_ = false;
    }
  }

  if (emergency_shutdown && this->mode != climate::CLIMATE_MODE_OFF) {
    ESP_LOGE(TAG, "АВАРИЯ: %s — аварийное отключение!", status.c_str());
    if (relay_07kw_) relay_07kw_->turn_off();
    if (relay_13kw_) relay_13kw_->turn_off();
    if (bst_state_ == BstState::RUNNING) {
      bst_hold_sec_ = 0;
      bst_state_    = BstState::OFF;
      this->target_temperature = bst_saved_temp_;
    }
    bst_state_         = BstState::OFF;
    heat_check_active_ = false;
    cancel_timer_();
    this->mode   = climate::CLIMATE_MODE_OFF;
    this->action = climate::CLIMATE_ACTION_OFF;
    this->publish_state();
  }

  if (status != last_status_) {
    last_status_ = status;
    if (status != "OK") {
      ESP_LOGW(TAG, "Статус: %s", status.c_str());
    } else {
      ESP_LOGI(TAG, "Статус: OK");
    }
    if (status_sensor_) {
      status_sensor_->publish_state(status);
    }
  }
}

void BoilerClimate::update_energy_() {
  float power_w = 0.0f;
  if (relay_07kw_ && relay_13kw_) {
    bool r1 = relay_07kw_->state;
    bool r2 = relay_13kw_->state;
    if (r1 && r2)       power_w = 2000.0f;
    else if (r1)        power_w = 700.0f;
    else if (r2)        power_w = 1300.0f;
  }

  if (power_sensor_) {
    power_sensor_->publish_state(power_w);
  }

  float wh = power_w / 3600.0f;
  energy_today_wh_ += wh;
  energy_total_wh_ += wh;

  time_t now_t = ::time(nullptr);
  if (now_t > 1000000) {
    struct tm *lt = localtime(&now_t);
    if (lt && lt->tm_mday != energy_last_day_) {
      energy_last_day_  = lt->tm_mday;
      energy_today_wh_  = 0.0f;
      ESP_LOGI(TAG, "Энергия: сброс счётчика за сегодня (новый день)");
    }
  }

  uint32_t now_ms = millis();
  if (now_ms - energy_last_pub_ms_ >= 10000) {
    energy_last_pub_ms_ = now_ms;
    if (energy_today_sensor_)
      energy_today_sensor_->publish_state(energy_today_wh_);
    if (energy_total_sensor_)
      energy_total_sensor_->publish_state(energy_total_wh_ / 1000.0f);  // в kWh
    save_nvs_();
  }
}

void BoilerClimate::reset_energy_total() {
  energy_total_wh_ = 0.0f;
  energy_today_wh_ = 0.0f;
  save_nvs_();
  if (energy_today_sensor_)
    energy_today_sensor_->publish_state(0.0f);
  if (energy_total_sensor_)
    energy_total_sensor_->publish_state(0.0f);
  ESP_LOGI(TAG, "Энергия: счётчик сброшен вручную");
}

}  // namespace boiler_climate
}  // namespace esphome
