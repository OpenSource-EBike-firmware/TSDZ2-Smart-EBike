/*
 * LCD3 firmware
 *
 * Copyright (C) Casainho, 2018.
 *
 * Released under the GPL License, Version 3
 */

#include <string.h>
#include "stm8s.h"
#include "stm8s_gpio.h"
#include "stm8s_iwdg.h"
#include "gpio.h"
#include "timers.h"
#include "ht162.h"
#include "lcd.h"
#include "adc.h"
#include "buttons.h"
#include "main.h"
#include "config.h"
#include "eeprom.h"
#include "pins.h"
#include "uart.h"

#define LCD_MENU_CONFIG_SUBMENU_MAX_NUMBER 10

uint8_t ui8_lcd_frame_buffer[LCD_FRAME_BUFFER_SIZE];

uint8_t ui8_lcd_field_offset[] = {
    ASSIST_LEVEL_DIGIT_OFFSET,
    ODOMETER_DIGIT_OFFSET,
    TEMPERATURE_DIGIT_OFFSET,
    WHEEL_SPEED_OFFSET,
    BATTERY_POWER_DIGIT_OFFSET,
    0
};

uint8_t ui8_lcd_digit_mask[] = {
    NUMBER_0_MASK,
    NUMBER_1_MASK,
    NUMBER_2_MASK,
    NUMBER_3_MASK,
    NUMBER_4_MASK,
    NUMBER_5_MASK,
    NUMBER_6_MASK,
    NUMBER_7_MASK,
    NUMBER_8_MASK,
    NUMBER_9_MASK
};

uint8_t ui8_lcd_digit_mask_inverted[] = {
    NUMBER_0_MASK_INVERTED,
    NUMBER_1_MASK_INVERTED,
    NUMBER_2_MASK_INVERTED,
    NUMBER_3_MASK_INVERTED,
    NUMBER_4_MASK_INVERTED,
    NUMBER_5_MASK_INVERTED,
    NUMBER_6_MASK_INVERTED,
    NUMBER_7_MASK_INVERTED,
    NUMBER_8_MASK_INVERTED,
    NUMBER_9_MASK_INVERTED
};

typedef struct _var_number
{
  void *p_var_number;
  uint8_t ui8_size;
  uint8_t ui8_decimal_digit;
  uint32_t ui32_max_value;
  uint32_t ui32_min_value;
  uint32_t ui32_increment_step;
  uint8_t ui8_odometer_field;
} var_number_t;

static uint32_t ui32_battery_voltage_accumulated_x10000 = 0;
static uint16_t ui16_battery_voltage_filtered_x10;

static uint16_t ui16_battery_current_accumulated_x5 = 0;
static uint16_t ui16_battery_current_filtered_x5;

static uint16_t ui16_battery_power_accumulated = 0;
static uint16_t ui16_battery_power_filtered_x50;
static uint16_t ui16_battery_power_filtered;

static uint32_t ui32_wh_sum_x5 = 0;
static uint32_t ui32_wh_sum_counter = 0;
static uint32_t ui32_wh_x10 = 0;
static uint8_t ui8_config_wh_x10_offset;

static uint32_t ui32_pedal_torque_accumulated = 0;
static uint16_t ui16_pedal_torque_filtered;
static uint32_t ui32_pedal_power_accumulated = 0;
static uint16_t ui16_pedal_power_filtered;

static uint16_t ui16_pedal_cadence_accumulated = 0;
static uint8_t ui8_pedal_cadence_filtered;

static uint8_t ui8_motor_controller_init = 1;

static uint8_t ui8_lights_state = 0;
static uint8_t lcd_lights_symbol = 0;

static uint8_t ui8_lcd_menu = 0;
static uint8_t ui8_lcd_menu_config_submenu_state = 0;
static uint8_t ui8_lcd_menu_flash_counter = 0;
static uint16_t ui16_lcd_menu_flash_counter_temperature = 0;
static uint8_t ui8_lcd_menu_flash_state;
static uint8_t ui8_lcd_menu_flash_state_temperature;
static uint8_t ui8_lcd_menu_config_submenu_number = 0;
static uint8_t ui8_lcd_menu_config_submenu_active = 0;

static uint8_t ui8_lcd_menu_counter_100ms = 0;
static uint8_t ui8_lcd_menu_counter_100ms_state = 0;

static uint8_t ui8_lcd_menu_counter_500ms = 0;
static uint8_t ui8_lcd_menu_counter_500ms_state = 0;

static struct_motor_controller_data motor_controller_data;
static struct_configuration_variables configuration_variables;

static uint16_t ui16_battery_soc_watts_hour;

static uint8_t ui8_reset_to_defaults_counter;
static uint8_t ui8_state_temp_field;

uint8_t ui8_lcd_power_off_time_counter_minutes = 0;
static uint16_t ui16_lcd_power_off_time_counter = 0;

static uint8_t offroad_mode_assist_symbol_state = 0;
static uint8_t offroad_mode_assist_symbol_state_blink_counter = 0;

static uint16_t ui16_battery_voltage_soc_x10;

static volatile uint16_t ui16_timer3_counter = 0;

uint8_t ui8_start_odometer_show_field_number = 0;
uint8_t ui8_odometer_show_field_number_counter_0 = 0;
uint8_t ui8_odometer_show_field_number_counter_1 = 1;
uint8_t ui8_odometer_show_field_number_state = 0;
uint8_t ui8_odometer_show_field_number = 0;
uint16_t ui16_odometer_reset_distance_counter = 0;
uint8_t ui8_odometer_reset_distance_counter_state = 1;

uint8_t load_odometer_sub_field_state_from_EEPROM = 1;
static uint8_t ui8_long_click_started = 0;
static uint8_t ui8_long_click_counter = 0;

void low_pass_filter_battery_voltage_current_power (void);
void lcd_enable_motor_symbol (uint8_t ui8_state);
void lcd_enable_lights_symbol (uint8_t ui8_state);
void lcd_enable_walk_symbol (uint8_t ui8_state);
void lcd_enable_km_symbol (uint8_t ui8_state);
void lcd_enable_mil_symbol (uint8_t ui8_state);
void lcd_enable_wheel_speed_point_symbol (uint8_t ui8_state);
void lcd_enable_kmh_symbol (uint8_t ui8_state);
void lcd_enable_mph_symbol (uint8_t ui8_state);
void lcd_enable_odo_symbol (uint8_t ui8_state);
void calc_wh (void);
void assist_level_state (void);
void brake (void);
void odometer (void);
void odometer_increase_field_state (void);
void wheel_speed (void);
void power(void);
void power_off_management (void);
uint8_t first_time_management (void);
void temperature (void);
void battery_soc (void);
void calc_battery_voltage_soc (void);
void low_pass_filter_pedal_torque_and_power (void);
static void low_pass_filter_pedal_cadence (void);
void lights_state (void);
void lcd_set_backlight_intensity (uint8_t ui8_intensity);
void walk_assist_state (void);
void offroad_mode (void);
void lcd_execute_main_screen (void);
void lcd_execute_menu_config (void);
void lcd_execute_menu_config_power (void);
void lcd_execute_menu_config_submenu_wheel_config (void);
void lcd_execute_menu_config_submenu_battery (void);
void lcd_execute_menu_config_submenu_battery_soc (void);
void lcd_execute_menu_config_submenu_assist_level (void);
void lcd_execute_menu_config_submenu_motor_startup_power_boost (void);
void lcd_execute_menu_config_submenu_motor_temperature (void);
void lcd_execute_menu_config_submenu_lcd ();
void lcd_execute_menu_config_submenu_offroad_mode (void);
void lcd_execute_menu_config_submenu_various (void);
void lcd_execute_menu_config_submenu_technical (void);
void update_menu_flashing_state (void);
void advance_on_submenu (uint8_t* ui8_p_state, uint8_t ui8_state_max_number);
void calc_battery_soc_watts_hour (void);
void calc_odometer (void);
static void automatic_power_off_management (void);
void lcd_power_off (uint8_t updateDistanceOdo);
void lcd_enable_vol_symbol (uint8_t ui8_state);
void lcd_enable_w_symbol (uint8_t ui8_state);
void lcd_enable_odometer_point_symbol (uint8_t ui8_state);
void lcd_enable_brake_symbol (uint8_t ui8_state);
void lcd_enable_assist_symbol (uint8_t ui8_state);
void lcd_enable_battery_power_1_symbol (uint8_t ui8_state);
void lcd_enable_temperature_1_symbol (uint8_t ui8_state);
void lcd_enable_kmh_symbol (uint8_t ui8_state);
void lcd_enable_wheel_speed_point_symbol (uint8_t ui8_state);
void lcd_enable_temperature_degrees_symbol (uint8_t ui8_state);
void lcd_enable_dst_symbol (uint8_t ui8_state);
void lcd_enable_tm_symbol (uint8_t ui8_state);
void lcd_update (void);
void lcd_clear (void);
void lcd_set_frame_buffer (void);
void lcd_print (uint32_t ui32_number, uint8_t ui8_lcd_field, uint8_t ui8_options);
void load_odometer_sub_field_state (void);
void update_odometer_sub_field_state (void);
void lcd_configurations_print_number(var_number_t* p_lcd_var_number);

// happens every 1ms
void TIM3_UPD_OVF_BRK_IRQHandler(void) __interrupt(TIM3_UPD_OVF_BRK_IRQHANDLER)
{
  static uint8_t ui8_100ms_timmer_counter;

  ui16_timer3_counter++;

  // calc wh every 100ms
  if (ui8_100ms_timmer_counter++ >= 100)
  {
    ui8_100ms_timmer_counter = 0;

    // must be called every 100ms
    calc_wh();
  }

  TIM3_ClearITPendingBit(TIM3_IT_UPDATE); // clear Interrupt Pending bit
}

uint16_t get_timer3_counter(void)
{
  return ui16_timer3_counter;
}

void lcd_clock (void)
{
  lcd_clear (); // start by clear LCD
  if (first_time_management ())
    return;

  update_menu_flashing_state ();

  // enter menu configurations: UP + DOWN click event
  if (buttons_get_up_down_click_event () && ui8_lcd_menu != 1)
  {
    buttons_clear_up_down_click_event ();
    ui8_lcd_menu = 1;
  }

  // enter in menu set power: ONOFF + UP click event
  if (!configuration_variables.ui8_offroad_feature_enabled && buttons_get_onoff_state () && buttons_get_up_state ())
  {
    buttons_clear_all_events ();
    ui8_lcd_menu = 2;
  }

  // change temperature field state: ONOFF + DOWN click event
  if (!configuration_variables.ui8_offroad_feature_enabled && buttons_get_onoff_state () && buttons_get_down_state ())
  {
    buttons_clear_all_events ();

    if (ui8_state_temp_field == 0)
    {
      // increment to next menu 
      configuration_variables.ui8_temperature_field_config++;
      
      // Battery SOC function
      if (configuration_variables.ui8_temperature_field_config == 1)
      {
        // if function is disabled, increment to next menu
        if (configuration_variables.ui8_show_numeric_battery_soc == 0) { configuration_variables.ui8_temperature_field_config++; }
      }
      
      // Motor temperature function
      if (configuration_variables.ui8_temperature_field_config == 2)
      {
        // if function is disabled, increment to next menu
        if (configuration_variables.ui8_temperature_limit_feature_enabled == 0) { configuration_variables.ui8_temperature_field_config++; }
      }
      
      // Check overflow
      if (configuration_variables.ui8_temperature_field_config > 2)
      {
        configuration_variables.ui8_temperature_field_config = 0;
      }
      
      ui8_state_temp_field = 1;
    }
  }
  else
  {
    ui8_state_temp_field = 0;
  }

  calc_battery_soc_watts_hour ();

  switch (ui8_lcd_menu)
  {
    case 0:
      lcd_execute_main_screen ();
    break;

    case 1:
      lcd_execute_menu_config ();
    break;

    case 2:
      lcd_execute_menu_config_power ();
    break;
  }

  low_pass_filter_battery_voltage_current_power ();
  // filter using only each 500ms values
  if (ui8_lcd_menu_counter_500ms_state)
  {
    low_pass_filter_pedal_cadence ();
  }

  // filter using only each 100ms values
  if (ui8_lcd_menu_counter_100ms_state)
  {
    low_pass_filter_pedal_torque_and_power ();
  }

  calc_battery_voltage_soc ();
  calc_odometer ();
  automatic_power_off_management ();

  // clear the buttons events
  buttons_clear_onoff_click_long_click_event();
  buttons_clear_up_click_long_click_event();
  buttons_clear_down_click_long_click_event();

  lcd_update ();

  // power off system: ONOFF long click event
  power_off_management ();
}

void lcd_execute_main_screen (void)
{
  temperature ();
  odometer ();
  wheel_speed ();
  walk_assist_state ();
  offroad_mode ();
  power ();
  battery_soc ();
  lights_state ();
  brake ();
  assist_level_state (); // needs to be the last because function clears all button events
}

void lcd_execute_menu_config (void)
{
  // button check when submenu is not active
  if (!ui8_lcd_menu_config_submenu_active)
  {
    // leave config menu with a button_onoff_long_click
    if (buttons_get_onoff_long_click_event ())
    {
      buttons_clear_onoff_long_click_event ();
      ui8_lcd_menu = 0;

      // save the updated variables on EEPROM
      eeprom_write_variables ();

      return;
    }

    // advance on submenu on button_onoff_click_event
    advance_on_submenu (&ui8_lcd_menu_config_submenu_number, LCD_MENU_CONFIG_SUBMENU_MAX_NUMBER);

    // check if we should enter a submenu
    if (buttons_get_up_click_event () || buttons_get_down_click_event ())
    {
      buttons_clear_up_click_event ();
      buttons_clear_down_click_event ();

      ui8_lcd_menu_config_submenu_active = 1;
      ui8_config_wh_x10_offset = 1;
    }

    // print submenu number only half of the time
    if (ui8_lcd_menu_flash_state)
    {
      lcd_print(ui8_lcd_menu_config_submenu_number, WHEEL_SPEED_FIELD, 0);
    }
  }
  // ui8_lcd_menu_config_submenu_active == 1
  else
  {
    switch (ui8_lcd_menu_config_submenu_number)
    {
      case 0:
        lcd_execute_menu_config_submenu_wheel_config ();
      break;

      case 1:
        lcd_execute_menu_config_submenu_battery ();
      break;

      case 2:
        lcd_execute_menu_config_submenu_battery_soc ();
      break;

      case 3:
        lcd_execute_menu_config_submenu_assist_level ();
      break;

      case 4:
        lcd_execute_menu_config_submenu_motor_startup_power_boost ();
      break;

      case 5:
        lcd_execute_menu_config_submenu_motor_temperature ();        
      break;

      case 6:
        lcd_execute_menu_config_submenu_lcd ();
      break;

      case 7:
        lcd_execute_menu_config_submenu_offroad_mode ();
      break;

      case 8:
        lcd_execute_menu_config_submenu_various ();
      break;

      case 9:
        lcd_execute_menu_config_submenu_technical ();
      break;

      default:
        ui8_lcd_menu_config_submenu_number = 0;
      break;
    }

    // leave config menu with a button_onoff_long_click
    if (buttons_get_onoff_long_click_event ())
    {
      buttons_clear_onoff_long_click_event ();

      ui8_lcd_menu_config_submenu_active = 0;
      ui8_lcd_menu_config_submenu_state = 0;
    }
  }
}

void lcd_execute_menu_config_submenu_wheel_config(void)
{
  var_number_t lcd_var_number;
  uint8_t ui8_units_type;

  // advance on submenus on button_onoff_click_event
  advance_on_submenu(&ui8_lcd_menu_config_submenu_state, 3);
  
  // if user has chosen imperial units
  if (configuration_variables.ui8_units_type)
  {
    // convert max wheel speed in imperial units to metric units and save to ui8_wheel_max_speed
    configuration_variables.ui8_wheel_max_speed = (uint8_t) (((float) configuration_variables.ui8_wheel_max_speed_imperial) * 1.6);
  }
  
  switch(ui8_lcd_menu_config_submenu_state)
  {
    // menu to choose units type
    case 0:

      ui8_units_type = configuration_variables.ui8_units_type;
      lcd_var_number.p_var_number = &ui8_units_type;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      // clear previous number written on ODOMETER_FIELD
      ui8_lcd_frame_buffer[ui8_lcd_field_offset[ODOMETER_FIELD] - 1] &= NUMBERS_MASK;

      if(ui8_units_type)
      {
        configuration_variables.ui8_units_type |= 1;
        if(ui8_lcd_menu_flash_state)
        {
          lcd_enable_mil_symbol(1);
          lcd_enable_mph_symbol(1);
        }
      }
      else
      {
        configuration_variables.ui8_units_type &= ~1;
        if(ui8_lcd_menu_flash_state)
        {
          lcd_enable_km_symbol(1);
          lcd_enable_kmh_symbol(1);
        }
      }
      
    break;
    
    // menu to choose max wheel speed
    case 1:
      
      // display max wheel speed in either imperial or metric units
      if (configuration_variables.ui8_units_type)
      {
        // imperial
        lcd_var_number.p_var_number = &configuration_variables.ui8_wheel_max_speed_imperial;
        lcd_var_number.ui8_size = 8;
        lcd_var_number.ui8_decimal_digit = 0;
        lcd_var_number.ui32_max_value = 62; // needs to be 1.6 times smaller than metric max value
        lcd_var_number.ui32_min_value = 0;
        lcd_var_number.ui32_increment_step = 1;
        lcd_var_number.ui8_odometer_field = WHEEL_SPEED_FIELD;
        lcd_configurations_print_number(&lcd_var_number);
        
        lcd_enable_mph_symbol (1);
      }
      else
      {
        // metric  
        lcd_var_number.p_var_number = &configuration_variables.ui8_wheel_max_speed;
        lcd_var_number.ui8_size = 8;
        lcd_var_number.ui8_decimal_digit = 0;
        lcd_var_number.ui32_max_value = 99; // this value needs to be smaller than 100 or else value > digits on display
        lcd_var_number.ui32_min_value = 0;
        lcd_var_number.ui32_increment_step = 1;
        lcd_var_number.ui8_odometer_field = WHEEL_SPEED_FIELD;        
        lcd_configurations_print_number(&lcd_var_number);
        
        lcd_enable_kmh_symbol (1);
      }
      
    break;
    
    // menu to choose wheel perimeter in millimeters
    case 2:
    
      lcd_var_number.p_var_number = &configuration_variables.ui16_wheel_perimeter;
      lcd_var_number.ui8_size = 16;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 3000;
      lcd_var_number.ui32_min_value = 750;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    
    break;
  }
}

void lcd_execute_menu_config_submenu_battery (void)
{
  var_number_t lcd_var_number;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu(&ui8_lcd_menu_config_submenu_state, 5);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    // battery max current
    case 0:
      lcd_var_number.p_var_number = &configuration_variables.ui8_battery_max_current;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 100;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // battery low voltage cut-off
    case 1:
      lcd_var_number.p_var_number = &configuration_variables.ui16_battery_low_voltage_cut_off_x10;
      lcd_var_number.ui8_size = 16;
      lcd_var_number.ui8_decimal_digit = 1;
      lcd_var_number.ui32_max_value = 630;
      lcd_var_number.ui32_min_value = 160;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // battery number of cells in series
    case 2:
      lcd_var_number.p_var_number = &configuration_variables.ui8_battery_cells_number;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 15;
      lcd_var_number.ui32_min_value = 7;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // battery internal resistance
    case 3:
      lcd_var_number.p_var_number = &configuration_variables.ui16_battery_pack_resistance_x1000;
      lcd_var_number.ui8_size = 16;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1000;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // battery voltage SOC
    case 4:
      lcd_print (ui16_battery_voltage_soc_x10, ODOMETER_FIELD, 1);
      lcd_enable_vol_symbol(1);
    break;
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_battery_soc (void)
{
  var_number_t lcd_var_number;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, 4);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    // menu to enable/disable show of numeric watt-hour value and type of representation
    case 0:
      lcd_var_number.p_var_number = &configuration_variables.ui8_show_numeric_battery_soc;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 2;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      
      // if user changes this variable we should clear the temperature field on display, it looks nicer that way
      configuration_variables.ui8_temperature_field_config = 0;
    break;

    // menu to set battery_voltage_reset_wh_counter
    case 1:
      lcd_var_number.p_var_number = &configuration_variables.ui16_battery_voltage_reset_wh_counter_x10;
      lcd_var_number.ui8_size = 16;
      lcd_var_number.ui8_decimal_digit = 1;
      lcd_var_number.ui32_max_value = 630;
      lcd_var_number.ui32_min_value = 160;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // menu to choose watts hour value to be equal to 100 % of battery SOC
    case 2:
      lcd_var_number.p_var_number = &configuration_variables.ui32_wh_x10_100_percent;
      lcd_var_number.ui8_size = 32;
      lcd_var_number.ui8_decimal_digit = 1;
      lcd_var_number.ui32_max_value = 100000;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 100;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // menu to set current watt hour value
    case 3:
      // on the very first time, use current value of ui32_wh_x10
      if (ui8_config_wh_x10_offset)
      {
        ui8_config_wh_x10_offset = 0;
        configuration_variables.ui32_wh_x10_offset = ui32_wh_x10;
      }
      
      // keep reseting this values
      ui32_wh_sum_x5 = 0;
      ui32_wh_sum_counter = 0;
      ui32_wh_x10 = 0;

      lcd_var_number.p_var_number = &configuration_variables.ui32_wh_x10_offset;
      lcd_var_number.ui8_size = 32;
      lcd_var_number.ui8_decimal_digit = 1;
      lcd_var_number.ui32_max_value = 100000;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 100;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_assist_level (void)
{
  var_number_t lcd_var_number;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, (configuration_variables.ui8_number_of_assist_levels + 1));

  // number of assist levels: 0 to 9
  if (ui8_lcd_menu_config_submenu_state == 0)
  {
    lcd_var_number.p_var_number = &configuration_variables.ui8_number_of_assist_levels;
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 0;
    lcd_var_number.ui32_max_value = 9;
    lcd_var_number.ui32_min_value = 1;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);
  }
  // value of each assist level factor
  else
  {
    lcd_var_number.p_var_number = &configuration_variables.ui8_assist_level_factor[(ui8_lcd_menu_config_submenu_state - 1)];
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 1;
    lcd_var_number.ui32_max_value = 255;
    lcd_var_number.ui32_min_value = 1;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_motor_startup_power_boost (void)
{
  var_number_t lcd_var_number;
  uint8_t ui8_temp;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, (configuration_variables.ui8_number_of_assist_levels + 5));

  // feature enable or disable
  if (ui8_lcd_menu_config_submenu_state == 0)
  {
    lcd_var_number.p_var_number = &configuration_variables.ui8_startup_motor_power_boost_feature_enabled;
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 0;
    lcd_var_number.ui32_max_value = 1;
    lcd_var_number.ui32_min_value = 0;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);
  }
  // enabled on startup when wheel speed is zero or always when cadence was zero
  else if (ui8_lcd_menu_config_submenu_state == 1)
  {
    ui8_temp = configuration_variables.ui8_startup_motor_power_boost_state & 1;
    lcd_var_number.p_var_number = &ui8_temp;
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 0;
    lcd_var_number.ui32_max_value = 1;
    lcd_var_number.ui32_min_value = 0;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);

    if(ui8_temp) { configuration_variables.ui8_startup_motor_power_boost_state |= 1; }
    else { configuration_variables.ui8_startup_motor_power_boost_state &= ~1; }
  }
  // limit to max power
  else if (ui8_lcd_menu_config_submenu_state == 2)
  {
    ui8_temp = (configuration_variables.ui8_startup_motor_power_boost_state & 2) >> 1;
    lcd_var_number.p_var_number = &ui8_temp;
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 0;
    lcd_var_number.ui32_max_value = 1;
    lcd_var_number.ui32_min_value = 0;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);

    if(ui8_temp) { configuration_variables.ui8_startup_motor_power_boost_state |= 2; }
    else { configuration_variables.ui8_startup_motor_power_boost_state &= ~2; }
  }
  // startup motor power boost time
  else if (ui8_lcd_menu_config_submenu_state == 3)
  {
    lcd_var_number.p_var_number = &configuration_variables.ui8_startup_motor_power_boost_time;
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 1;
    lcd_var_number.ui32_max_value = 255;
    lcd_var_number.ui32_min_value = 0;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);
  }
  // startup motor power boost fade time
  else if (ui8_lcd_menu_config_submenu_state == 4)
  {
    lcd_var_number.p_var_number = &configuration_variables.ui8_startup_motor_power_boost_fade_time;
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 1;
    lcd_var_number.ui32_max_value = 255;
    lcd_var_number.ui32_min_value = 0;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);
  }
  // value of each assist level factor for power boost
  else
  {
    lcd_var_number.p_var_number = &configuration_variables.ui8_startup_motor_power_boost_factor[(ui8_lcd_menu_config_submenu_state - 5)];
    lcd_var_number.ui8_size = 8;
    lcd_var_number.ui8_decimal_digit = 1;
    lcd_var_number.ui32_max_value = 255;
    lcd_var_number.ui32_min_value = 0;
    lcd_var_number.ui32_increment_step = 1;
    lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
    lcd_configurations_print_number(&lcd_var_number);
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_motor_temperature (void)
{
  var_number_t lcd_var_number;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, 3);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    // motor temperature enable
    case 0:
      lcd_var_number.p_var_number = &configuration_variables.ui8_temperature_limit_feature_enabled;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      
      // if user changes this variable we should clear the temperature field on display, it looks nicer that way
      configuration_variables.ui8_temperature_field_config = 0;
    break;

    // motor temperature limit min
    case 1:
      lcd_var_number.p_var_number = &configuration_variables.ui8_motor_temperature_min_value_to_limit;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 110;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // motor temperature limit max
    case 2:
      lcd_var_number.p_var_number = &configuration_variables.ui8_motor_temperature_max_value_to_limit;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 110;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_lcd (void)
{
  var_number_t lcd_var_number;
  uint8_t ui8_temp;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, 4);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    // backlight off brightness
    case 0:
      ui8_temp = configuration_variables.ui8_lcd_backlight_off_brightness * 5;
      lcd_var_number.p_var_number = &ui8_temp;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 100;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 5;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      configuration_variables.ui8_lcd_backlight_off_brightness = ui8_temp / 5;
    break;

    // backlight on brightness
    case 1:
      ui8_temp = configuration_variables.ui8_lcd_backlight_on_brightness * 5;
      lcd_var_number.p_var_number = &ui8_temp;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 100;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 5;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      configuration_variables.ui8_lcd_backlight_on_brightness = ui8_temp / 5;
    break;

    // auto power off
    case 2:
      lcd_var_number.p_var_number = &configuration_variables.ui8_lcd_power_off_time_minutes;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 255;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // reset to defaults
    case 3:
      lcd_var_number.p_var_number = &ui8_reset_to_defaults_counter;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 10;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);

      if (ui8_reset_to_defaults_counter > 9)
      {
        // erase saved EEPROM values (all values will be set to defaults)
        eeprom_erase_key_value ();

        // Turn off LCD
        lcd_power_off (0);
      }
    break;
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_offroad_mode (void)
{
  var_number_t lcd_var_number;
  uint16_t ui16_temp;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, 5);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    // enable/disable offroad functionality
    case 0:
      lcd_var_number.p_var_number = &configuration_variables.ui8_offroad_feature_enabled;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // enable offroad mode on system startup
    case 1:
      lcd_var_number.p_var_number = &configuration_variables.ui8_offroad_enabled_on_startup;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // offroad speed limit (when offroad mode is off)
    case 2:
      lcd_var_number.p_var_number = &configuration_variables.ui8_offroad_speed_limit;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 99;
      lcd_var_number.ui32_min_value = 1;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      lcd_enable_kmh_symbol (1);
    break;

    // enable/disable power limit
    case 3:
      lcd_var_number.p_var_number = &configuration_variables.ui8_offroad_power_limit_enabled;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // power limit (W)
    case 4:
      ui16_temp = ((uint16_t) configuration_variables.ui8_offroad_power_limit_div25) * 25;
      lcd_var_number.p_var_number = &ui16_temp;
      lcd_var_number.ui8_size = 16;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1000;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 25;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
      configuration_variables.ui8_offroad_power_limit_div25 = (uint8_t) (ui16_temp / 25);
    break;
  }
  
  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_various (void)
{
  var_number_t lcd_var_number;
  uint32_t ui32_odometer_x10;
  
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, 3);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    // motor voltage type
    case 0:
      lcd_var_number.p_var_number = &configuration_variables.ui8_motor_type;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 2;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;

    // motor assistance startup without pedal rotation
    case 1:
      lcd_var_number.p_var_number = &configuration_variables.ui8_motor_assistance_startup_without_pedal_rotation;
      lcd_var_number.ui8_size = 8;
      lcd_var_number.ui8_decimal_digit = 0;
      lcd_var_number.ui32_max_value = 1;
      lcd_var_number.ui32_min_value = 0;
      lcd_var_number.ui32_increment_step = 1;
      lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
      lcd_configurations_print_number(&lcd_var_number);
    break;
  
    // set odometer
    case 2:
      ui32_odometer_x10 = configuration_variables.ui32_odometer_x10;

      if (configuration_variables.ui8_units_type)
      {
        // imperial
        ui32_odometer_x10 = (uint16_t) (((float) ui32_odometer_x10) / 1.6);

        lcd_var_number.p_var_number = &ui32_odometer_x10;
        lcd_var_number.ui8_size = 32;
        lcd_var_number.ui8_decimal_digit = 1;
        lcd_var_number.ui32_max_value = 4294967295; // needs to be 1.6 times smaller than metric max value
        lcd_var_number.ui32_min_value = 0;
        lcd_var_number.ui32_increment_step = 25;
        lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
        lcd_configurations_print_number(&lcd_var_number);
        
        configuration_variables.ui32_odometer_x10 = (uint16_t) (((float) ui32_odometer_x10) * 1.6);
        lcd_enable_odo_symbol(1);
        lcd_enable_mil_symbol(1);
      }
      else
      {
        // metric
        lcd_var_number.p_var_number = &ui32_odometer_x10;
        lcd_var_number.ui8_size = 32;
        lcd_var_number.ui8_decimal_digit = 1;
        lcd_var_number.ui32_max_value = 4294967295;
        lcd_var_number.ui32_min_value = 0;
        lcd_var_number.ui32_increment_step = 25;
        lcd_var_number.ui8_odometer_field = ODOMETER_FIELD;
        lcd_configurations_print_number(&lcd_var_number);
        
        configuration_variables.ui32_odometer_x10 = ui32_odometer_x10;
        lcd_enable_odo_symbol(1);
        lcd_enable_km_symbol(1);
      }
    break;
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_submenu_technical (void)
{
  // advance on submenus on button_onoff_click_event
  advance_on_submenu (&ui8_lcd_menu_config_submenu_state, 9);

  switch (ui8_lcd_menu_config_submenu_state)
  {
    case 0:
      lcd_print(motor_controller_data.ui8_adc_throttle, ODOMETER_FIELD, 0);
    break;

    case 1:
      lcd_print(motor_controller_data.ui8_throttle, ODOMETER_FIELD, 0);
    break;

    case 2:
      lcd_print(motor_controller_data.ui8_adc_pedal_torque_sensor, ODOMETER_FIELD, 0);
    break;

    case 3:
      lcd_print(motor_controller_data.ui8_pedal_torque_sensor, ODOMETER_FIELD, 0);
    break;

    case 4:
      lcd_print(motor_controller_data.ui8_pedal_cadence, ODOMETER_FIELD, 0);
    break;

    case 5:
      lcd_print(motor_controller_data.ui8_pedal_human_power, ODOMETER_FIELD, 0);
    break;

    case 6:
      lcd_print(motor_controller_data.ui8_duty_cycle, ODOMETER_FIELD, 0);
    break;

    case 7:
      lcd_print(motor_controller_data.ui16_motor_speed_erps, ODOMETER_FIELD, 0);
    break;

    case 8:
      lcd_print(motor_controller_data.ui8_foc_angle, ODOMETER_FIELD, 0);
    break;
  }

  lcd_print(ui8_lcd_menu_config_submenu_state, WHEEL_SPEED_FIELD, 0);
}

void lcd_execute_menu_config_power (void)
{
  // because this click envent can happen and will block the detection of button_onoff_long_click_event
  var_number_t lcd_var_number;
  uint16_t ui16_temp;

  buttons_clear_onoff_click_event ();

  // leave this menu with a button_onoff_long_click
  if (buttons_get_onoff_long_click_event ())
  {
    buttons_clear_all_events ();
    ui8_lcd_menu = 0;

    // save the updated variables on EEPROM
    eeprom_write_variables ();
  }

  ui16_temp = ((uint16_t) configuration_variables.ui8_target_max_battery_power_div25) * 25;
  lcd_var_number.p_var_number = &ui16_temp;
  lcd_var_number.ui8_size = 16;
  lcd_var_number.ui8_decimal_digit = 1; // need to be for BATTERY_POWER_FIELD
  lcd_var_number.ui32_max_value = 1900;
  lcd_var_number.ui32_min_value = 0;

  if (configuration_variables.ui8_target_max_battery_power_div25 < 10)
  {
    lcd_var_number.ui32_increment_step = 25;
  }
  else
  {
    lcd_var_number.ui32_increment_step = 50;
  }

  lcd_var_number.ui8_odometer_field = BATTERY_POWER_FIELD;
  lcd_configurations_print_number(&lcd_var_number);
  configuration_variables.ui8_target_max_battery_power_div25 = (uint8_t) (ui16_temp / 25);
}

uint8_t first_time_management (void)
{
  uint8_t ui8_status = 0;

  // don't update LCD up to we get first communication package from the motor controller
  if (ui8_motor_controller_init &&
      (uart_received_first_package () == 0))
  {
    ui8_status = 1;
  }
  // this will be executed only 1 time at startup
  else if (ui8_motor_controller_init)
  {
    ui8_motor_controller_init = 0;

    // reset Wh value if battery voltage is over ui16_battery_voltage_reset_wh_counter_x10 (value configured by user)
    if (((uint32_t) motor_controller_data.ui16_adc_battery_voltage *
        ADC_BATTERY_VOLTAGE_PER_ADC_STEP_X10000) > ((uint32_t) configuration_variables.ui16_battery_voltage_reset_wh_counter_x10 * 1000))
    {
      configuration_variables.ui32_wh_x10_offset = 0;
    }

    if (configuration_variables.ui8_offroad_feature_enabled && 
      configuration_variables.ui8_offroad_enabled_on_startup)
    {
      motor_controller_data.ui8_offroad_mode = 1;
    }
  }  

  return ui8_status;
}

void power_off_management (void)
{
  // turn off
  if (buttons_get_onoff_long_click_event ()) { lcd_power_off (1); }
}

void temperature (void)
{
  // if motor current is being limited due to temperature, force showing temperature!!
  if (motor_controller_data.ui8_temperature_current_limiting_value != 255)
  {
    if (ui8_lcd_menu_flash_state_temperature)
    {
      lcd_print(motor_controller_data.ui8_motor_temperature, TEMPERATURE_FIELD, 0);
      lcd_enable_temperature_degrees_symbol (1);
    }
  }
  else
  {
    switch (configuration_variables.ui8_temperature_field_config)
    {
      // show nothing
      case 0:
      break;

      // show battery_soc_watts_hour
      case 1:
        lcd_print(ui16_battery_soc_watts_hour, TEMPERATURE_FIELD, 1);
      break;

      // show motor temperature
      case 2:
        lcd_print(motor_controller_data.ui8_motor_temperature, TEMPERATURE_FIELD, 1);
        lcd_enable_temperature_degrees_symbol (1);
      break;
    }
  }
}

void battery_soc(void)
{
  static uint8_t ui8_timmer_counter;
  static uint8_t ui8_battery_state_of_charge;
  uint8_t ui8_battery_cells_number_x10;

  // update battery level value only at every 100ms / 10 times per second and this helps to visual filter the fast changing values
  if (ui8_timmer_counter++ >= 10)
  {
    ui8_timmer_counter = 0;

    // to keep same scale as voltage of x10
    ui8_battery_cells_number_x10 = configuration_variables.ui8_battery_cells_number * 10;

    if (ui16_battery_voltage_soc_x10 > ((uint16_t) ((float) ui8_battery_cells_number_x10 * LI_ION_CELL_VOLTS_83))) { ui8_battery_state_of_charge = 4; } // 4 bars | full
    else if (ui16_battery_voltage_soc_x10 > ((uint16_t) ((float) ui8_battery_cells_number_x10 * LI_ION_CELL_VOLTS_50))) { ui8_battery_state_of_charge = 3; } // 3 bars
    else if (ui16_battery_voltage_soc_x10 > ((uint16_t) ((float) ui8_battery_cells_number_x10 * LI_ION_CELL_VOLTS_17))) { ui8_battery_state_of_charge = 2; } // 2 bars
    else if (ui16_battery_voltage_soc_x10 > ((uint16_t) ((float) ui8_battery_cells_number_x10 * LI_ION_CELL_VOLTS_0))) { ui8_battery_state_of_charge = 1; } // 1 bar
    else { ui8_battery_state_of_charge = 0; } // flashing
  }

  /*
    ui8_lcd_frame_buffer[23] |= 16;  // empty
    ui8_lcd_frame_buffer[23] |= 128; // bar number 1
    ui8_lcd_frame_buffer[23] |= 1;   // bar number 2
    ui8_lcd_frame_buffer[23] |= 64;  // bar number 3
    ui8_lcd_frame_buffer[23] |= 32;  // bar number 4
    */

  // first clean battery symbols
  ui8_lcd_frame_buffer[23] &= ~241;

  switch (ui8_battery_state_of_charge)
  {
    case 0:
      // empty, so flash the empty battery symbol
      if (ui8_lcd_menu_flash_state)
      {
        ui8_lcd_frame_buffer[23] |= 16;
      }
    break;

    case 1:
      ui8_lcd_frame_buffer[23] |= 144;
    break;

    case 2:
      ui8_lcd_frame_buffer[23] |= 145;
    break;

    case 3:
      ui8_lcd_frame_buffer[23] |= 209;
    break;

    case 4:
      ui8_lcd_frame_buffer[23] |= 241;
    break;
  }
}

void calc_battery_voltage_soc (void)
{
  uint16_t ui16_fluctuate_battery_voltage_x10;

  // update battery level value only at every 100ms / 10 times per second and this helps to visual filter the fast changing values
  if (ui8_lcd_menu_counter_100ms_state)
  {
    // calculate flutuate voltage, that depends on the current and battery pack resistance
    ui16_fluctuate_battery_voltage_x10 = (uint16_t) ((((uint32_t) configuration_variables.ui16_battery_pack_resistance_x1000) * ((uint32_t) ui16_battery_current_filtered_x5)) / ((uint32_t) 500));
    // now add fluctuate voltage value
    ui16_battery_voltage_soc_x10 = ui16_battery_voltage_filtered_x10 + ui16_fluctuate_battery_voltage_x10;
  }
}

void power(void)
{
  lcd_print(ui16_battery_power_filtered, BATTERY_POWER_FIELD, 0);
  lcd_enable_motor_symbol(1);
  lcd_enable_w_symbol(1);
}

void assist_level_state (void)
{
  var_number_t lcd_var_number;

  lcd_var_number.p_var_number = &configuration_variables.ui8_assist_level;
  lcd_var_number.ui8_size = 8;
  lcd_var_number.ui8_decimal_digit = 1;
  lcd_var_number.ui32_max_value = configuration_variables.ui8_number_of_assist_levels;
  lcd_var_number.ui32_min_value = 0;
  lcd_var_number.ui32_increment_step = 1;
  lcd_var_number.ui8_odometer_field = ASSIST_LEVEL_FIELD;
  lcd_configurations_print_number(&lcd_var_number);

  if (motor_controller_data.ui8_offroad_mode == 0)
  {
    lcd_enable_assist_symbol (1);
  }
}

void lights_state (void)
{
  if (buttons_get_up_long_click_event ())
  {
    buttons_clear_up_long_click_event ();

    if (ui8_lights_state == 0)
    {
      ui8_lights_state = 1;
      lcd_set_backlight_intensity (configuration_variables.ui8_lcd_backlight_on_brightness); 
      motor_controller_data.ui8_lights = 1;
    }
    else
    {
      ui8_lights_state = 0;
      lcd_set_backlight_intensity (configuration_variables.ui8_lcd_backlight_off_brightness); 
      motor_controller_data.ui8_lights = 0;
    }
  }
  
  lcd_enable_lights_symbol(ui8_lights_state);
}

void walk_assist_state (void)
{
  if (buttons_get_down_long_click_event ())
  {
    // user need to keep pressing the button to have walk assist
    if (buttons_get_down_state ())
    {
      motor_controller_data.ui8_walk_assist_level = 1;
      lcd_enable_walk_symbol (1);
    }
    else
    {
      motor_controller_data.ui8_walk_assist_level = 0;
      buttons_clear_down_long_click_event ();
    }
  }
}

void offroad_mode (void)
{
  if (configuration_variables.ui8_offroad_feature_enabled) 
  {
    if (buttons_get_onoff_state () && buttons_get_up_state ())
    {
      buttons_clear_all_events ();
      motor_controller_data.ui8_offroad_mode = 1;
    }

    if (buttons_get_onoff_state () && buttons_get_down_state ())
    {
      buttons_clear_all_events ();
      motor_controller_data.ui8_offroad_mode = 0;
    }

    if (motor_controller_data.ui8_offroad_mode == 1) 
    {
      if (offroad_mode_assist_symbol_state_blink_counter++ > 50)
      {
        offroad_mode_assist_symbol_state_blink_counter = 0;
        offroad_mode_assist_symbol_state = !offroad_mode_assist_symbol_state;
      }

      lcd_enable_assist_symbol (offroad_mode_assist_symbol_state);
    }
  }
}

void brake (void)
{
  if (motor_controller_data.ui8_braking) { lcd_enable_brake_symbol (1); }
  else { lcd_enable_brake_symbol (0); }
}

void odometer_increase_field_state (void)
{
  configuration_variables.ui8_odometer_field_state++;
  
  if (configuration_variables.ui8_odometer_field_state >= 5) 
  {
    configuration_variables.ui8_odometer_field_state = 0;
    
    // set flag to read saved value in EEPROM
    load_odometer_sub_field_state_from_EEPROM = 1;
  }
}

void odometer_start_show_field_number (void)
{
  ui8_start_odometer_show_field_number = 1;
  ui8_odometer_show_field_number_counter_0 = 0;
  ui8_odometer_show_field_number_counter_1 = 0;
  ui8_odometer_show_field_number_state = 1;
}

void load_odometer_sub_field_state (void)
{
  switch (configuration_variables.ui8_odometer_field_state)
  {
    case 0:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_0;
    break;
    
    case 1:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_1;
    break;
    
    case 2:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_2;
    break;
    
    case 3:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_3;
    break;
    
    case 4:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_4;
    break;
    
    case 5:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_5;
    break;
    
    case 6:
      configuration_variables.ui8_odometer_sub_field_state = configuration_variables.ui8_odometer_sub_field_state_6;
    break;
  }
}

void update_odometer_sub_field_state (void)
{
  switch (configuration_variables.ui8_odometer_field_state)
  {
    case 0:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_0 = configuration_variables.ui8_odometer_sub_field_state;
    break;
    
    case 1:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_1 = configuration_variables.ui8_odometer_sub_field_state;
    break;
    
    case 2:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_2 = configuration_variables.ui8_odometer_sub_field_state;
    break;
    
    case 3:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_3 = configuration_variables.ui8_odometer_sub_field_state;
    break;
    
    case 4:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_4 = configuration_variables.ui8_odometer_sub_field_state;
    break;
    
    case 5:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_5 = configuration_variables.ui8_odometer_sub_field_state;
    break;
    
    case 6:
      // update sub menu state
      configuration_variables.ui8_odometer_sub_field_state_6 = configuration_variables.ui8_odometer_sub_field_state;
    break;
  }
}

void odometer (void)
{
  uint32_t ui32_temp;
  
  if (load_odometer_sub_field_state_from_EEPROM)
  {
    // clear flag
    load_odometer_sub_field_state_from_EEPROM = 0;
    
    // load last odometer sub menu states
    load_odometer_sub_field_state ();
  }

  // if user clicks onoff click event
  if (buttons_get_onoff_click_event ())
  {
    buttons_clear_onoff_click_event ();
    
    // Update the sub menu states
    update_odometer_sub_field_state ();
    
    // increment odometer field state
    odometer_increase_field_state ();
    
    // load last odometer menu states
    load_odometer_sub_field_state ();
  
    odometer_start_show_field_number ();
  }

  // if there are errors, show the error number on odometer field instead of any other information
  if (motor_controller_data.ui8_error_states != ERROR_STATE_NO_ERRORS)
  {
    if (ui8_lcd_menu_flash_state)
    {
      lcd_print(motor_controller_data.ui8_error_states, ODOMETER_FIELD, 0);
    }
  }
  else
  {
    switch (configuration_variables.ui8_odometer_field_state)
    {
      // distance 
      case 0:
        if(buttons_get_up_click_long_click_event())
        {
          buttons_clear_up_click_long_click_event();
          
          // increment menu state with one
          configuration_variables.ui8_odometer_sub_field_state++;
          
          // check overflow, if true -> reset to first menu state
          if(configuration_variables.ui8_odometer_sub_field_state >= 3)
          {
            configuration_variables.ui8_odometer_sub_field_state = 0;
          }
          
          odometer_start_show_field_number ();
        }

        switch (configuration_variables.ui8_odometer_sub_field_state)
        {
          // trip distance
          case 0:
        
            // pre calculation
            ui32_temp = configuration_variables.ui32_trip_x10 + ((uint32_t) configuration_variables.ui16_odometer_distance_x10);

            // as soon there is one down_click_long_click_event
            if (buttons_get_down_click_long_click_event())
            {
              ui8_odometer_reset_distance_counter_state = 1;
            }

            if (ui8_odometer_reset_distance_counter_state)
            {
              if (buttons_get_down_state ())
              {
                ui8_odometer_reset_distance_counter_state = 1;

                // clear the down button possible event
                buttons_clear_down_click_event();
                buttons_clear_down_long_click_event();

                // count time, after limit, reset
                ui16_odometer_reset_distance_counter++;
                if (ui16_odometer_reset_distance_counter >= 300)
                {
                  // reset counter
                  ui16_odometer_reset_distance_counter = 0;
                  
                  // reset trip
                  configuration_variables.ui32_trip_x10 = 0;
            
                  // add and save traveled distance to odometer variable before resetting distance
                  configuration_variables.ui32_odometer_x10 += ((uint32_t) configuration_variables.ui16_odometer_distance_x10);
                  eeprom_write_variables ();
                  
                  // reset distance - set the offset to current value, that is the way to zero our always incrementing value (up to motor controller power reset)
                  motor_controller_data.ui32_wheel_speed_sensor_tick_counter_offset = motor_controller_data.ui32_wheel_speed_sensor_tick_counter;
                }

                if (ui8_lcd_menu_flash_state)
                {
                  // display trip distance in either imperial or metric units
                  if (configuration_variables.ui8_units_type)
                  {
                    // imperial units
                    lcd_print(((float) ui32_temp / 1.6), ODOMETER_FIELD, 1);
                    lcd_enable_mil_symbol (1);
                  }
                  else
                  {
                    // metric units
                    lcd_print(ui32_temp, ODOMETER_FIELD, 1);
                    lcd_enable_km_symbol (1);
                  }
                }
              }
              else // user is not pressing the down button anymore
              {
                ui8_odometer_reset_distance_counter_state = 0;
              }
            }
            else
            {
              // reset counter
              ui16_odometer_reset_distance_counter = 0;
              
              // display trip distance in either imperial or metric units
              if (configuration_variables.ui8_units_type)
              {
                // imperial units
                lcd_print (((float) ui32_temp / 1.6), ODOMETER_FIELD, 1);
                lcd_enable_mil_symbol (1);
              }
              else
              {
                // metric units
                lcd_print (ui32_temp, ODOMETER_FIELD, 1);
                lcd_enable_km_symbol (1);
              }
            }
          
          break;

          // distance since power on
          case 1:
            // as soon there is one down_click_long_click_event
            if (buttons_get_down_click_long_click_event())
            {
              ui8_odometer_reset_distance_counter_state = 1;
            }

            if (ui8_odometer_reset_distance_counter_state)
            {
              if (buttons_get_down_state ())
              {
                ui8_odometer_reset_distance_counter_state = 1;

                // clear the down button possible event
                buttons_clear_down_click_event();
                buttons_clear_down_long_click_event();

                // count time, after limit, reset
                ui16_odometer_reset_distance_counter++;
                if (ui16_odometer_reset_distance_counter >= 300)
                {
                  // reset counter
                  ui16_odometer_reset_distance_counter = 0;
                  
                  // before reset of distance, we should update and save trip and odo values to EEPROM
                  configuration_variables.ui32_odometer_x10 += ((uint32_t) configuration_variables.ui16_odometer_distance_x10);
                  configuration_variables.ui32_trip_x10 += ((uint32_t) configuration_variables.ui16_odometer_distance_x10);
                  eeprom_write_variables ();
                  
                  // reset distance - set the offset to current value, that is the way to set to zero our always incrementing value (up to motor controller power reset)
                  motor_controller_data.ui32_wheel_speed_sensor_tick_counter_offset = motor_controller_data.ui32_wheel_speed_sensor_tick_counter;
                }

                if (ui8_lcd_menu_flash_state)
                {
                  // display distance in either imperial or metric units
                  if (configuration_variables.ui8_units_type)
                  {
                    // imperial units
                    uint32_t ui32_odometer_distance_x10 = (uint32_t) configuration_variables.ui16_odometer_distance_x10;
                    lcd_print (((float) ui32_odometer_distance_x10 / 1.6), ODOMETER_FIELD, 1);
                    // lcd_enable_dst_symbol (1); TODO: this fails, the symbol just work when we set it to 1 AND speed field number is equal or higher than 10.0. Seems the 3rd digit at left is needed.
                    lcd_enable_mil_symbol (1);
                  }
                  else
                  {
                    // metric units
                    lcd_print ((uint32_t) configuration_variables.ui16_odometer_distance_x10, ODOMETER_FIELD, 1);
                    // lcd_enable_dst_symbol (1); TODO: this fails, the symbol just work when we set it to 1 AND speed field number is equal or higher than 10.0. Seems the 3rd digit at left is needed.
                    lcd_enable_km_symbol (1);
                  }
                }
              }
              else // user is not pressing the down button anymore
              {
                ui8_odometer_reset_distance_counter_state = 0;
              }
            }
            else
            {
              // reset counter
              ui16_odometer_reset_distance_counter = 0;
              
              // display distance in either imperial or metric units
              if (configuration_variables.ui8_units_type)
              {
                // imperial units
                uint32_t ui32_odometer_distance_x10 = (uint32_t) configuration_variables.ui16_odometer_distance_x10;
                lcd_print(((float) ui32_odometer_distance_x10 / 1.6), ODOMETER_FIELD, 1);
                // lcd_enable_dst_symbol (1); TODO: this fails, the symbol just work when we set it to 1 AND speed field number is equal or higher than 10.0. Seems the 3rd digit at left is needed.
                lcd_enable_mil_symbol (1);
              }
              else
              {
                // metric units
                lcd_print((uint32_t) configuration_variables.ui16_odometer_distance_x10, ODOMETER_FIELD, 1);
                // lcd_enable_dst_symbol (1); TODO: this fails, the symbol just work when we set it to 1 AND speed field number is equal or higher than 10.0. Seems the 3rd digit at left is needed.
                lcd_enable_km_symbol (1);
              }
            }

          break;

          // odometer
          case 2:
            // pre calculation
            ui32_temp = configuration_variables.ui32_odometer_x10 + ((uint32_t) configuration_variables.ui16_odometer_distance_x10);

            // as soon there is one down_click_long_click_event
            if (buttons_get_down_click_long_click_event())
            {
              ui8_odometer_reset_distance_counter_state = 1;
            }

            if (ui8_odometer_reset_distance_counter_state)
            {
              if (buttons_get_down_state ())
              {
                ui8_odometer_reset_distance_counter_state = 1;

                // clear the down button possible event
                buttons_clear_down_click_event();
                buttons_clear_down_long_click_event();

                // count time, after limit, reset
                ui16_odometer_reset_distance_counter++;
                if (ui16_odometer_reset_distance_counter >= 300)
                {
                  // reset counter
                  ui16_odometer_reset_distance_counter = 0;
                  
                  // reset odometer
                  configuration_variables.ui32_odometer_x10 = 0;
                  
                  // add and save traveled distance to trip variable before resetting distance
                  configuration_variables.ui32_trip_x10 += ((uint32_t) configuration_variables.ui16_odometer_distance_x10);
                  eeprom_write_variables ();
                  
                  // reset distance - set the offset to current value, that is the way to zero our always incrementing value (up to motor controller power reset)
                  motor_controller_data.ui32_wheel_speed_sensor_tick_counter_offset = motor_controller_data.ui32_wheel_speed_sensor_tick_counter;
                }

                if (ui8_lcd_menu_flash_state)
                {
                  // display odometer distance in either imperial or metric units
                  if (configuration_variables.ui8_units_type)
                  {
                    // imperial units
                    lcd_print(((float) ui32_temp / 1.6), ODOMETER_FIELD, 1);
                    lcd_enable_odo_symbol (1);
                    lcd_enable_mil_symbol (1);
                  }
                  else
                  {
                    // metric units
                    lcd_print(ui32_temp, ODOMETER_FIELD, 1);
                    lcd_enable_odo_symbol (1);
                    lcd_enable_km_symbol (1);
                  }
                }
              }
              else // user is not pressing the down button anymore
              {
                ui8_odometer_reset_distance_counter_state = 0;
              }
            }
            else
            {
              // reset counter
              ui16_odometer_reset_distance_counter = 0;
              
              // display odometer distance in either imperial or metric units
              if (configuration_variables.ui8_units_type)
              {
                // imperial units
                lcd_print(((float) ui32_temp / 1.6), ODOMETER_FIELD, 1);
                lcd_enable_odo_symbol (1);
                lcd_enable_mil_symbol (1);
              }
              else
              {
                // metric units
                lcd_print(ui32_temp, ODOMETER_FIELD, 1);
                lcd_enable_odo_symbol (1);
                lcd_enable_km_symbol (1);
              }
            }
          
          break;
        }

      break; // end of distance

      // battery SOC
      case 1:
      
        if (configuration_variables.ui8_show_numeric_battery_soc == 0)
        {
          odometer_increase_field_state ();
          break;
        }

        if (buttons_get_up_click_long_click_event ())
        {
          buttons_clear_up_click_long_click_event ();
          
          // increment menu state with one
          configuration_variables.ui8_odometer_sub_field_state++;
          
          // check if overflow, if true -> reset to first menu state
          if (configuration_variables.ui8_odometer_sub_field_state >= 2)
          {
            configuration_variables.ui8_odometer_sub_field_state = 0;
          }

          odometer_start_show_field_number ();
        }

        switch (configuration_variables.ui8_odometer_sub_field_state)
        {
          // battery SOC in watts/hour
          case 0:
            lcd_print(ui16_battery_soc_watts_hour, ODOMETER_FIELD, 0);
          break;

          // Wh value
          case 1:
            lcd_print(ui32_wh_x10, ODOMETER_FIELD, 1);
          break;
        }
      
      break; // end of battery SOC

      // battery voltage and current
      case 2:
        if (buttons_get_up_click_long_click_event ())
        {
          buttons_clear_up_click_long_click_event ();
          
          // increment menu state with one
          configuration_variables.ui8_odometer_sub_field_state++;
          
          // check overflow, if true -> reset to first menu state
          if (configuration_variables.ui8_odometer_sub_field_state >= 2)
          {
            configuration_variables.ui8_odometer_sub_field_state = 0;
          }

          odometer_start_show_field_number ();
        }

        switch (configuration_variables.ui8_odometer_sub_field_state)
        {
          // voltage value
          case 0:
          lcd_print(ui16_battery_voltage_filtered_x10, ODOMETER_FIELD, 1);
          lcd_enable_vol_symbol (1);
          break;

          // current value
          case 1:
            lcd_print(ui16_battery_current_filtered_x5 << 1, ODOMETER_FIELD, 1);
          break;
        }
      
      break; // end of battery voltage and current

      // pedals
      case 3:
        if (buttons_get_up_click_long_click_event ())
        {
          buttons_clear_up_click_long_click_event ();
          
          // increment sub menu state with one
          configuration_variables.ui8_odometer_sub_field_state++;
          
          // check if overflow, if true -> reset to first menu state
          if (configuration_variables.ui8_odometer_sub_field_state >= 3)
          {
            configuration_variables.ui8_odometer_sub_field_state = 0;
          }
          
          odometer_start_show_field_number ();
        }

        switch (configuration_variables.ui8_odometer_sub_field_state)
        {
          // pedal power
          case 0:
            lcd_print (ui16_pedal_power_filtered, ODOMETER_FIELD, 0);
          break;

          // pedal cadence value
          case 1:
            lcd_print (ui8_pedal_cadence_filtered, ODOMETER_FIELD, 0);
          break;

          // pedal torque
          case 2:
            lcd_print (ui16_pedal_torque_filtered, ODOMETER_FIELD, 0);
          break;
        }
      break; // end of pedals

      // motor temperature
      case 4:
        if (buttons_get_up_click_long_click_event ())
        {
          buttons_clear_up_click_long_click_event ();
          odometer_start_show_field_number ();
        }

        if (!(configuration_variables.ui8_temperature_limit_feature_enabled))
        {       
          odometer_increase_field_state ();
          break;
        }

        lcd_print (motor_controller_data.ui8_motor_temperature, ODOMETER_FIELD, 0);
      
      break; // end of motor temperature
    }

    if (ui8_start_odometer_show_field_number)
    {
      // for flashing
      ui8_odometer_show_field_number_counter_0++;
      if (ui8_odometer_show_field_number_counter_0 >= 40)
      {
        ui8_odometer_show_field_number_counter_0 = 0;

        // time limit for flashing
        ui8_odometer_show_field_number_counter_1++;
        if (ui8_odometer_show_field_number_counter_1 >= 5)
        {
          ui8_odometer_show_field_number_counter_0 = 0;
          ui8_odometer_show_field_number_counter_1 = 0;
          ui8_odometer_show_field_number_state = 1;
          ui8_start_odometer_show_field_number = 0; // this disables the flashing
        }
          
        if (ui8_odometer_show_field_number_state)
        {
          ui8_odometer_show_field_number_state = 0; 
        }
        else 
        {
          ui8_odometer_show_field_number_state = 1;
        }
      }
    
      if (ui8_odometer_show_field_number_state)
      {
        ui8_odometer_show_field_number = ((configuration_variables.ui8_odometer_field_state + 1) * 10); // add units for show (x10)
        ui8_odometer_show_field_number += configuration_variables.ui8_odometer_sub_field_state;

        lcd_print (ui8_odometer_show_field_number, WHEEL_SPEED_FIELD, 1);
      }
    }
  }
}

void wheel_speed (void)
{
  // show wheel speed only when we should not start show odometer field number
  if (ui8_start_odometer_show_field_number == 0)
  {
    if (configuration_variables.ui8_units_type)
    {
      lcd_print(((float) motor_controller_data.ui16_wheel_speed_x10 / 1.6), WHEEL_SPEED_FIELD, 1);
      lcd_enable_mph_symbol (1);
    }
    else
    {
      lcd_print(motor_controller_data.ui16_wheel_speed_x10, WHEEL_SPEED_FIELD, 1);
      lcd_enable_kmh_symbol (1);
    }
  }
}


void lcd_clear (void)
{
  memset(ui8_lcd_frame_buffer, 0, LCD_FRAME_BUFFER_SIZE);
}


void lcd_set_frame_buffer (void)
{
  memset(ui8_lcd_frame_buffer, 255, LCD_FRAME_BUFFER_SIZE);
}


void lcd_update (void)
{
  ht1622_send_frame_buffer (ui8_lcd_frame_buffer);
}


void lcd_print(uint32_t ui32_number, uint8_t ui8_lcd_field, uint8_t ui8_options)
{
  uint8_t ui8_digit;
  uint8_t ui8_counter;

  // multiply the value by 10 to not show decimal digit if ...
  if( (ui8_options == 0) && (ui8_lcd_field != ASSIST_LEVEL_FIELD) && (ui8_lcd_field != BATTERY_POWER_FIELD) )
  {
    ui32_number *= 10;
  }
  
  // first delete the field
  for (ui8_counter = 0; ui8_counter < 5; ui8_counter++)
  {
    if (ui8_lcd_field == ASSIST_LEVEL_FIELD || ui8_lcd_field == ODOMETER_FIELD || ui8_lcd_field == TEMPERATURE_FIELD)
    {
      ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] - ui8_counter] &= NUMBERS_MASK;
    }

    // because the LCD mask/layout is different on some field, like numbers would be inverted
    if (ui8_lcd_field == WHEEL_SPEED_FIELD || ui8_lcd_field == BATTERY_POWER_FIELD)
    {
      ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] + ui8_counter] &= NUMBERS_MASK;
    }

    // limit the number of printed digits for each field
    if (ui8_counter == 0 && ui8_lcd_field == ASSIST_LEVEL_FIELD) break;
    if (ui8_counter == 4 && ui8_lcd_field == ODOMETER_FIELD) break;
    if (ui8_counter == 1 && ui8_lcd_field == TEMPERATURE_FIELD) break;
    if (ui8_counter == 2 && ui8_lcd_field == WHEEL_SPEED_FIELD) break;
    if (ui8_counter == 2 && ui8_lcd_field == BATTERY_POWER_FIELD) break;
  }

  // enable only the "1" if power is >= 1000
  if (ui8_lcd_field == BATTERY_POWER_FIELD)
  {
    if (ui32_number >= 1000) { lcd_enable_battery_power_1_symbol (1); }
    else { lcd_enable_battery_power_1_symbol (0); }
  }

  // enable only the "1" if temperature is >= 100
  if (ui8_lcd_field == TEMPERATURE_FIELD)
  {
    if (ui32_number >= 100) { lcd_enable_temperature_1_symbol (1); }
    else { lcd_enable_temperature_1_symbol (0); }
  }

  // do not show the point symbol if number*10 is integer
  if (ui8_options == 0)
  {
    if (ui8_lcd_field == ODOMETER_FIELD) { lcd_enable_odometer_point_symbol (0); }
    else if (ui8_lcd_field == WHEEL_SPEED_FIELD) { lcd_enable_wheel_speed_point_symbol (0); }
  }
  else
  {
    if (ui8_lcd_field == ODOMETER_FIELD) { lcd_enable_odometer_point_symbol (1); }
    else if (ui8_lcd_field == WHEEL_SPEED_FIELD) { lcd_enable_wheel_speed_point_symbol (1); }
  }

  for (ui8_counter = 0; ui8_counter < 5; ui8_counter++)
  {
    ui8_digit = ui32_number % 10;

    if (ui8_lcd_field == ASSIST_LEVEL_FIELD || ui8_lcd_field == ODOMETER_FIELD || ui8_lcd_field == TEMPERATURE_FIELD)
    {

      if ((ui8_options == 0) && (ui8_counter == 0))
      {
        ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] - ui8_counter] &= ui8_lcd_digit_mask[NUMBERS_MASK];
      }
      // print empty (NUMBERS_MASK) when ui32_number = 0
      else if ((ui8_counter > 1 && ui32_number == 0) ||
          // TEMPERATURE_FIELD: print 1 zero only when value is less than 10
          (ui8_lcd_field == TEMPERATURE_FIELD && ui8_counter > 0 && ui32_number == 0))
      {
        ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] - ui8_counter] &= ui8_lcd_digit_mask[NUMBERS_MASK];
      }
      else
      {
        ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] - ui8_counter] |= ui8_lcd_digit_mask[ui8_digit];
      }
    }

    // because the LCD mask/layout is different on some field, like numbers would be inverted
    if (ui8_lcd_field == WHEEL_SPEED_FIELD || ui8_lcd_field == BATTERY_POWER_FIELD)
    {
      if (ui8_lcd_field == WHEEL_SPEED_FIELD)
      {
        if ((ui8_options == 0) && (ui8_counter == 0))
        {
          ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] + ui8_counter] &= ui8_lcd_digit_mask[NUMBERS_MASK];
        }
        // print only first 2 zeros
        else if (ui8_counter > 1 && ui32_number == 0)
        {
          ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] + ui8_counter] &= ui8_lcd_digit_mask[NUMBERS_MASK];
        }
        else
        {
          ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] + ui8_counter] |= ui8_lcd_digit_mask_inverted[ui8_digit];
        }
      }

      if (ui8_lcd_field == BATTERY_POWER_FIELD)
      {
        // print only first zero
        if (ui8_counter > 0 && ui32_number == 0)
        {
          ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] + ui8_counter] &= ui8_lcd_digit_mask[NUMBERS_MASK];
        }
        else
        {
          ui8_lcd_frame_buffer[ui8_lcd_field_offset[ui8_lcd_field] + ui8_counter] |= ui8_lcd_digit_mask_inverted[ui8_digit];
        }
      }
    }

    // limit the number of printed digits for each field
    if (ui8_counter == 0 && ui8_lcd_field == ASSIST_LEVEL_FIELD) break;
    if (ui8_counter == 4 && ui8_lcd_field == ODOMETER_FIELD) break;
    if (ui8_counter == 1 && ui8_lcd_field == TEMPERATURE_FIELD) break;
    if (ui8_counter == 2 && ui8_lcd_field == WHEEL_SPEED_FIELD) break;
    if (ui8_counter == 2 && ui8_lcd_field == BATTERY_POWER_FIELD) break;

    ui32_number /= 10;
  }
}


void lcd_enable_w_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[9] |= 128;
  else
    ui8_lcd_frame_buffer[9] &= ~128;
}


void lcd_enable_odometer_point_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[6] |= 8;
  else
    ui8_lcd_frame_buffer[6] &= ~8;
}


void lcd_enable_brake_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[23] |= 4;
  else
    ui8_lcd_frame_buffer[23] &= ~4;
}

void lcd_enable_lights_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[23] |= 2;
  else
    ui8_lcd_frame_buffer[23] &= ~2;
}

void lcd_enable_cruise_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[0] |= 16;
  else
    ui8_lcd_frame_buffer[0] &= ~16;
}

void lcd_enable_assist_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[1] |= 8;
  else
    ui8_lcd_frame_buffer[1] &= ~8;
}

void lcd_enable_vol_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[2] |= 8;
  else
    ui8_lcd_frame_buffer[2] &= ~8;
}

void lcd_enable_odo_symbol(uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[3] |= 8;
  else
    ui8_lcd_frame_buffer[3] &= ~8;
}

void lcd_enable_km_symbol(uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[4] |= 8;
  else
    ui8_lcd_frame_buffer[4] &= ~8;
}

void lcd_enable_mil_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[5] |= 8;
  else
    ui8_lcd_frame_buffer[5] &= ~8;
}

void lcd_enable_temperature_1_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[7] |= 8;
  else
    ui8_lcd_frame_buffer[7] &= ~8;
}

void lcd_enable_battery_power_1_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[12] |= 8;
  else
    ui8_lcd_frame_buffer[12] &= ~8;
}

void lcd_enable_temperature_minus_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[8] |= 8;
  else
    ui8_lcd_frame_buffer[8] &= ~8;
}

void lcd_enable_temperature_degrees_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[9] |= 16;
  else
    ui8_lcd_frame_buffer[9] &= ~16;
}

void lcd_enable_temperature_farneight_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[9] |= 32;
  else
    ui8_lcd_frame_buffer[9] &= ~32;
}

void lcd_enable_farneight_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[9] |= 1;
  else
    ui8_lcd_frame_buffer[9] &= ~1;
}

void lcd_enable_motor_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[9] |= 2;
  else
    ui8_lcd_frame_buffer[9] &= ~2;
}

void lcd_enable_degrees_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[9] |= 64;
  else
    ui8_lcd_frame_buffer[9] &= ~64;
}

void lcd_enable_kmh_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[13] |= 1;
  else
    ui8_lcd_frame_buffer[13] &= ~1;
}

void lcd_enable_wheel_speed_point_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[13] |= 8;
  else
    ui8_lcd_frame_buffer[13] &= ~8;
}

void lcd_enable_avs_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[13] |= 16;
  else
    ui8_lcd_frame_buffer[13] &= ~16;
}

void lcd_enable_mxs_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[13] |= 32;
  else
    ui8_lcd_frame_buffer[13] &= ~32;
}

void lcd_enable_walk_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[13] |= 64;
  else
    ui8_lcd_frame_buffer[13] &= ~64;
}

void lcd_enable_mph_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[13] |= 128;
  else
    ui8_lcd_frame_buffer[13] &= ~128;
}

void lcd_enable_dst_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[16] |= 8;
  else
    ui8_lcd_frame_buffer[16] &= ~8;
}

void lcd_enable_tm_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[17] |= 16;
  else
    ui8_lcd_frame_buffer[17] &= ~16;
}

void lcd_enable_ttm_symbol (uint8_t ui8_state)
{
  if (ui8_state)
    ui8_lcd_frame_buffer[17] |= 32;
  else
    ui8_lcd_frame_buffer[17] &= ~32;
}

void low_pass_filter_battery_voltage_current_power (void)
{
  // low pass filter battery voltage
  ui32_battery_voltage_accumulated_x10000 -= ui32_battery_voltage_accumulated_x10000 >> BATTERY_VOLTAGE_FILTER_COEFFICIENT;
  ui32_battery_voltage_accumulated_x10000 += (uint32_t) motor_controller_data.ui16_adc_battery_voltage * ADC_BATTERY_VOLTAGE_PER_ADC_STEP_X10000;
  ui16_battery_voltage_filtered_x10 = ((uint32_t) (ui32_battery_voltage_accumulated_x10000 >> BATTERY_VOLTAGE_FILTER_COEFFICIENT)) / 1000;

  // low pass filter batery current
  ui16_battery_current_accumulated_x5 -= ui16_battery_current_accumulated_x5 >> BATTERY_CURRENT_FILTER_COEFFICIENT;
  ui16_battery_current_accumulated_x5 += (uint16_t) motor_controller_data.ui8_battery_current_x5;
  ui16_battery_current_filtered_x5 = ui16_battery_current_accumulated_x5 >> BATTERY_CURRENT_FILTER_COEFFICIENT;

  // battery power
  ui16_battery_power_filtered_x50 = ui16_battery_current_filtered_x5 * ui16_battery_voltage_filtered_x10;
  ui16_battery_power_filtered = ui16_battery_power_filtered_x50 / 50;

  // loose resolution under 200 W
  if (ui16_battery_power_filtered < 200)
  {
    ui16_battery_power_filtered /= 10;
    ui16_battery_power_filtered *= 10;
  }
  // loose resolution under 400 W
  else if (ui16_battery_power_filtered < 400)
  {
    ui16_battery_power_filtered /= 20;
    ui16_battery_power_filtered *= 20;
  }
  // loose resolution all other values
  else
  {
    ui16_battery_power_filtered /= 25;
    ui16_battery_power_filtered *= 25;
  }
}

void low_pass_filter_pedal_torque_and_power (void)
{
  // low pass filter
  ui32_pedal_torque_accumulated -= ui32_pedal_torque_accumulated >> PEDAL_TORQUE_FILTER_COEFFICIENT;
  ui32_pedal_torque_accumulated += (uint32_t) motor_controller_data.ui16_pedal_torque_x10 / 10;
  ui16_pedal_torque_filtered = ((uint32_t) (ui32_pedal_torque_accumulated >> PEDAL_TORQUE_FILTER_COEFFICIENT));

  // low pass filter
  ui32_pedal_power_accumulated -= ui32_pedal_power_accumulated >> PEDAL_POWER_FILTER_COEFFICIENT;
  ui32_pedal_power_accumulated += (uint32_t) motor_controller_data.ui16_pedal_power_x10 / 10;
  ui16_pedal_power_filtered = ((uint32_t) (ui32_pedal_power_accumulated >> PEDAL_POWER_FILTER_COEFFICIENT));

  if (ui16_pedal_torque_filtered > 200)
  {
    ui16_pedal_torque_filtered /= 20;
    ui16_pedal_torque_filtered *= 20;
  }
  else if (ui16_pedal_torque_filtered > 100)
  {
    ui16_pedal_torque_filtered /= 10;
    ui16_pedal_torque_filtered *= 10;
  }
  else
  {
    // do nothing to roginal values
  }

  if (ui16_pedal_power_filtered > 500)
  {
    ui16_pedal_power_filtered /= 25;
    ui16_pedal_power_filtered *= 25;
  }
  else if (ui16_pedal_power_filtered > 200)
  {
    ui16_pedal_power_filtered /= 20;
    ui16_pedal_power_filtered *= 20;
  }
  else if (ui16_pedal_power_filtered > 10)
  {
    ui16_pedal_power_filtered /= 10;
    ui16_pedal_power_filtered *= 10;
  }
  else
  {
    ui16_pedal_power_filtered = 0; // no point to show less than 10W
  }
}

static void low_pass_filter_pedal_cadence (void)
{
  // low pass filter
  ui16_pedal_cadence_accumulated -= (ui16_pedal_cadence_accumulated >> PEDAL_CADENCE_FILTER_COEFFICIENT);
  ui16_pedal_cadence_accumulated += (uint16_t) motor_controller_data.ui8_pedal_cadence;

  // consider the filtered value only for medium and high values of the unfiltered value
  if (motor_controller_data.ui8_pedal_cadence > 20)
  {
    ui8_pedal_cadence_filtered = (uint8_t) (ui16_pedal_cadence_accumulated >> PEDAL_CADENCE_FILTER_COEFFICIENT);
  }
  else
  {
    ui8_pedal_cadence_filtered = motor_controller_data.ui8_pedal_cadence;
  }
}

void calc_wh (void)
{
  static uint8_t ui8_1s_timmer_counter;
  uint32_t ui32_temp = 0;

  if (ui16_battery_power_filtered_x50 > 0)
  {
    ui32_wh_sum_x5 += ui16_battery_power_filtered_x50 / 10;
    ui32_wh_sum_counter++;
  }

  // calc at 1s rate
  if (ui8_1s_timmer_counter++ >= 10)
  {
    ui8_1s_timmer_counter = 0;

    // avoid  zero divisison
    if (ui32_wh_sum_counter != 0)
    {
      ui32_temp = ui32_wh_sum_counter / 36;
      ui32_temp = (ui32_temp * (ui32_wh_sum_x5 / ui32_wh_sum_counter)) / 500;
    }

    ui32_wh_x10 = configuration_variables.ui32_wh_x10_offset + ui32_temp;
  }
}

void calc_odometer (void)
{
  uint32_t uint32_temp;
  static uint8_t ui8_1s_timmer_counter;

  // calc at 1s rate
  if (ui8_1s_timmer_counter++ >= 100)
  {
    ui8_1s_timmer_counter = 0;

    uint32_temp = (motor_controller_data.ui32_wheel_speed_sensor_tick_counter - motor_controller_data.ui32_wheel_speed_sensor_tick_counter_offset)
        * ((uint32_t) configuration_variables.ui16_wheel_perimeter);
    // avoid division by 0
    if (uint32_temp > 100000) { uint32_temp /= 100000;}  // milimmeters to 0.1kms
    else { uint32_temp = 0; }

    // now store the value in a global variable
    configuration_variables.ui16_odometer_distance_x10 = (uint16_t) uint32_temp;
  }
}

static void automatic_power_off_management (void)
{
  if (configuration_variables.ui8_lcd_power_off_time_minutes != 0)
  {
    // see if we should reset the automatic power off minutes counter
    if ((motor_controller_data.ui16_wheel_speed_x10 > 0) ||   // wheel speed > 0
        (motor_controller_data.ui8_battery_current_x5 > 0) || // battery current > 0
        (motor_controller_data.ui8_braking) ||                // braking
        buttons_get_events ())                                 // any button active
    {
      ui16_lcd_power_off_time_counter = 0;
      ui8_lcd_power_off_time_counter_minutes = 0;
    }

    // increment the automatic power off minutes counter
    ui16_lcd_power_off_time_counter++;

    // check if we should power off the LCD
    if (ui16_lcd_power_off_time_counter >= (100 * 60)) // 1 minute passed
    {
      ui16_lcd_power_off_time_counter = 0;

      ui8_lcd_power_off_time_counter_minutes++;
      if (ui8_lcd_power_off_time_counter_minutes >= configuration_variables.ui8_lcd_power_off_time_minutes)
      {
        lcd_power_off (1);
      }
    }
  }
  else
  {
    ui16_lcd_power_off_time_counter = 0;
    ui8_lcd_power_off_time_counter_minutes = 0;
  }
}

struct_configuration_variables* get_configuration_variables (void)
{
  return &configuration_variables;
}

struct_motor_controller_data* lcd_get_motor_controller_data (void)
{
  return &motor_controller_data;
}

void lcd_init (void)
{
  ht1622_init ();
  lcd_set_frame_buffer ();
  lcd_update();

  // init variables with the stored value on EEPROM
  eeprom_init_variables ();
}

void lcd_set_backlight_intensity (uint8_t ui8_intensity)
{
  if (ui8_intensity == 0)
  {
    TIM1_CCxCmd (TIM1_CHANNEL_4, DISABLE);
  }
  else if (ui8_intensity <= 20)
  {
    TIM1_SetCompare4 ((uint16_t) ui8_intensity);
    TIM1_CCxCmd (TIM1_CHANNEL_4, ENABLE);
  }
}

void update_menu_flashing_state(void)
{
  // ***************************************************************************************************
  // For flashing on menus
  if(ui8_lcd_menu_flash_counter == 0)
  {
    if(ui8_lcd_menu_flash_state)
    {
      ui8_lcd_menu_flash_state = 0;
      ui8_lcd_menu_flash_counter = 20;
    }
    else
    {
      ui8_lcd_menu_flash_state = 1;
      ui8_lcd_menu_flash_counter = 80;
    }
  }
  ui8_lcd_menu_flash_counter--;
  // ***************************************************************************************************

  // ***************************************************************************************************
  ui8_lcd_menu_counter_100ms_state = 0;
  if (ui8_lcd_menu_counter_100ms++ > 10)
  {
    ui8_lcd_menu_counter_100ms = 0;
    ui8_lcd_menu_counter_100ms_state = 1;
  }

  ui8_lcd_menu_counter_500ms_state = 0;
  if (ui8_lcd_menu_counter_500ms++ > 50)
  {
    ui8_lcd_menu_counter_500ms = 0;
    ui8_lcd_menu_counter_500ms_state = 1;
  }
  // ***************************************************************************************************

  // ***************************************************************************************************
  // For flashing the temperature field when the current is being limited due to motor over temperature
  // flash only if current is being limited: ui8_temperature_current_limiting_value != 255
  if (motor_controller_data.ui8_temperature_current_limiting_value != 255)
  {
    if (ui8_lcd_menu_flash_state_temperature == 0) // state 0: disabled
    {
      if (ui16_lcd_menu_flash_counter_temperature > 0)
      {
        ui16_lcd_menu_flash_counter_temperature--;
      }

      if (ui16_lcd_menu_flash_counter_temperature == 0)
      {
        // if motor_controller_data.ui8_temperature_current_limiting_value == 0, flash quicker meaning motor is shutoff
        if (motor_controller_data.ui8_temperature_current_limiting_value > 0)
        {
          ui16_lcd_menu_flash_counter_temperature = 50 + ((uint16_t) motor_controller_data.ui8_temperature_current_limiting_value);
        }
        else
        {
          ui16_lcd_menu_flash_counter_temperature = 25;
        }

        ui8_lcd_menu_flash_state_temperature = 1;
      }
    }

    if (ui8_lcd_menu_flash_state_temperature == 1) // state 1: enabled
    {
      if (ui16_lcd_menu_flash_counter_temperature > 0)
      {
        ui16_lcd_menu_flash_counter_temperature--;
      }

      if (ui16_lcd_menu_flash_counter_temperature == 0)
      {
        ui16_lcd_menu_flash_counter_temperature = 25; // 0.25 second
        ui8_lcd_menu_flash_state_temperature = 0;
      }
    }
  }
  else
  {
    ui8_lcd_menu_flash_state_temperature = 1;
  }
  // ***************************************************************************************************
}

void advance_on_submenu (uint8_t* ui8_p_state, uint8_t ui8_state_max_number)
{
  // advance on submenus on button_onoff_click_event
  if (buttons_get_onoff_click_event ())
  {
    buttons_clear_onoff_click_event ();

    *ui8_p_state = (*ui8_p_state + 1) % ui8_state_max_number;
  }
}

void calc_battery_soc_watts_hour(void)
{
  uint32_t ui32_temp;
  ui32_temp = ui32_wh_x10 * 100;
  
  if (configuration_variables.ui32_wh_x10_100_percent > 0)
  {
    ui32_temp /= configuration_variables.ui32_wh_x10_100_percent;
  }
  else
  {
    ui32_temp = 0;
  }

  if (configuration_variables.ui8_show_numeric_battery_soc == 1)
  {
    // SOC from 100 to 0 percent (remaining capacity in percent)
    if (ui32_temp > 100)
    {
      ui32_temp = 100;
    }
    
    ui16_battery_soc_watts_hour = 100 - ui32_temp;
  }
  else if (configuration_variables.ui8_show_numeric_battery_soc == 2)
  {
    // SOC from 0 to 100 percent (consumed capacity in percent)
    ui16_battery_soc_watts_hour = ui32_temp;
  }
}

void lcd_power_off (uint8_t updateDistanceOdo)
{
  if (updateDistanceOdo)
  {
    // add used watthour to watthour variable 
    configuration_variables.ui32_wh_x10_offset = ui32_wh_x10;
    
    // add the traveled distance to odometer variable
    configuration_variables.ui32_odometer_x10 += ((uint32_t) configuration_variables.ui16_odometer_distance_x10);
    
    // add the traveled distance to trip variable
    configuration_variables.ui32_trip_x10 += ((uint32_t) configuration_variables.ui16_odometer_distance_x10);
    
    // update sub menu field states
    update_odometer_sub_field_state ();
    
    // save variables in EEPROM
    eeprom_write_variables ();
  } 

  // clear LCD so it is clear to user what is happening
  lcd_clear ();
  lcd_update ();

  // now disable the power to all the system
  GPIO_WriteLow(LCD3_ONOFF_POWER__PORT, LCD3_ONOFF_POWER__PIN);

  // block here
  while (1) ;
}

void lcd_configurations_print_number(var_number_t* p_lcd_var_number)
{
  uint8_t *ui8_p_var = 0;
  uint16_t *ui16_p_var = 0;
  uint32_t *ui32_p_var = 0;
  uint32_t ui32_value = 0;
  uint8_t ui8_long_click_trigger = 0;

  if(p_lcd_var_number->ui8_size == 8)
  {
    ui8_p_var = ((uint8_t *) p_lcd_var_number->p_var_number);
  }
  else if(p_lcd_var_number->ui8_size == 16)
  {
    ui16_p_var = ((uint16_t *) p_lcd_var_number->p_var_number);
  }
  else if(p_lcd_var_number->ui8_size == 32)
  {
    ui32_p_var = ((uint32_t *) p_lcd_var_number->p_var_number);
  }

  // if LONG CLICK, keep track of long click so variable is increased automatically 10x every second
  if(buttons_get_up_long_click_event() || buttons_get_down_long_click_event())
  {
    ui8_long_click_started = 1;
  }

  // trigger at ever 100ms if UP/DOWN LONG CLICK
  if((ui8_long_click_started == 1) && (buttons_get_up_state() || buttons_get_down_state()))
  {
    ui8_long_click_counter++;

    if(ui8_long_click_counter >= 10)
    {
      ui8_long_click_counter = 0;
      ui8_long_click_trigger = 1;
    }
  }
  else
  {
    ui8_long_click_started = 0;
    ui8_long_click_counter = 0;
  }

  // increase
  if(buttons_get_up_click_event() || (buttons_get_up_state() && ui8_long_click_trigger))
  {
    if(p_lcd_var_number->ui8_size == 8)
    {
      if((*ui8_p_var) <= (p_lcd_var_number->ui32_max_value - p_lcd_var_number->ui32_increment_step)) { (*ui8_p_var) += p_lcd_var_number->ui32_increment_step; }
      else { (*ui8_p_var) = (uint8_t) p_lcd_var_number->ui32_max_value; }
    }
    else if(p_lcd_var_number->ui8_size == 16)
    {
      if((*ui16_p_var) <= (p_lcd_var_number->ui32_max_value - p_lcd_var_number->ui32_increment_step)) { (*ui16_p_var) += p_lcd_var_number->ui32_increment_step; }
      else { (*ui16_p_var) = (uint16_t) p_lcd_var_number->ui32_max_value; }
    }
    else if(p_lcd_var_number->ui8_size == 32)
    {
      if((*ui32_p_var) <= (p_lcd_var_number->ui32_max_value - p_lcd_var_number->ui32_increment_step)) { (*ui32_p_var) += p_lcd_var_number->ui32_increment_step; }
      else { (*ui32_p_var) = p_lcd_var_number->ui32_max_value; }
    }
  }

  // decrease
  if(buttons_get_down_click_event() || (buttons_get_down_state() && ui8_long_click_trigger))
  {
    if(p_lcd_var_number->ui8_size == 8)
    {
      if((*ui8_p_var) >= (p_lcd_var_number->ui32_min_value + p_lcd_var_number->ui32_increment_step)) { (*ui8_p_var) -= p_lcd_var_number->ui32_increment_step; }
      else { (*ui8_p_var) = (uint8_t) p_lcd_var_number->ui32_min_value; }
    }
    else if(p_lcd_var_number->ui8_size == 16)
    {
      if((*ui16_p_var) >= (p_lcd_var_number->ui32_min_value + p_lcd_var_number->ui32_increment_step)) { (*ui16_p_var) -= p_lcd_var_number->ui32_increment_step; }
      else { (*ui16_p_var) = (uint16_t) p_lcd_var_number->ui32_min_value; }
    }
    else if(p_lcd_var_number->ui8_size == 32)
    {
      if((*ui32_p_var) >= (p_lcd_var_number->ui32_min_value + p_lcd_var_number->ui32_increment_step)) { (*ui32_p_var) -= p_lcd_var_number->ui32_increment_step; }
      else { (*ui32_p_var) = p_lcd_var_number->ui32_min_value; }
    }
  }

  if(p_lcd_var_number->ui8_size == 8)
  {
    ui32_value = (uint32_t) (*ui8_p_var);
  }
  else if(p_lcd_var_number->ui8_size == 16)
  {
    ui32_value = (uint32_t) (*ui16_p_var);
  }
  else if(p_lcd_var_number->ui8_size == 32)
  {
    ui32_value = (*ui32_p_var);
  }

  // draw only at every ui8_lcd_menu_flash_state -- will flash the number on the LCD
  if((ui8_lcd_menu_flash_state) || (p_lcd_var_number->ui8_odometer_field == ASSIST_LEVEL_FIELD)) // do not flash on ASSIST_LEVEL_FIELD
  {
    lcd_print(ui32_value, p_lcd_var_number->ui8_odometer_field, p_lcd_var_number->ui8_decimal_digit);
  }

  buttons_clear_up_click_event();
  buttons_clear_up_click_long_click_event();
  buttons_clear_up_long_click_event();
  buttons_clear_down_click_event();
  buttons_clear_down_click_long_click_event();
  buttons_clear_down_long_click_event();
}
