/*
 * TongSheng TSDZ2 motor controller firmware
 *
 * Copyright (C) Casainho and EndlessCadence, 2018.
 *
 * Released under the GPL License, Version 3
 */

#include "ebike_app.h"
#include <stdint.h>
#include <stdio.h>
#include "stm8s.h"
#include "stm8s_gpio.h"
#include "main.h"
#include "interrupts.h"
#include "adc.h"
#include "motor.h"
#include "pwm.h"
#include "uart.h"
#include "brake.h"
#include "eeprom.h"
#include "lights.h"
#include "common.h"

volatile struct_configuration_variables m_configuration_variables;

// variables for various system functions
static uint8_t    ui8_riding_mode = OFF_MODE;
static uint8_t    ui8_riding_mode_parameter = 0;
static uint8_t    ui8_system_state = NO_ERROR;
static uint8_t    ui8_brakes_enabled = 0;
static uint8_t    ui8_motor_enabled = 0;


// variables for power control
static uint16_t   ui16_duty_cycle_ramp_up_inverse_step = PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_DEFAULT;
static uint16_t   ui16_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT;
static uint16_t   ui16_battery_voltage_filtered_x1000 = 0;
static uint8_t    ui8_battery_current_filtered_x10 = 0;
static uint8_t    ui8_adc_battery_current_max = ADC_10_BIT_BATTERY_CURRENT_MAX;
static uint8_t    ui8_adc_battery_current_target = 0;
static uint8_t    ui8_duty_cycle_target = 0;


// variables for the cadence sensor
static uint8_t    ui8_pedal_cadence_RPM = 0;


// variables for the torque sensor
volatile uint16_t ui16_adc_pedal_torque = 0;
static uint16_t   ui16_adc_pedal_torque_delta = 0;
static uint16_t   ui16_pedal_power_x10 = 0;
static uint16_t   ui16_pedal_torque_x100 = 0;


// variables for the throttle function
volatile uint8_t  ui8_adc_throttle = 0;


// variables for wheel speed sensor
static uint16_t   ui16_wheel_speed_x10 = 0;


// boost
uint8_t   ui8_startup_boost_enable = 0;
uint8_t   ui8_startup_boost_fade_enable = 0;
uint8_t   ui8_m_startup_boost_state_machine = 0;
uint8_t   ui8_startup_boost_no_torque = 0;
uint8_t   ui8_startup_boost_timer = 0;
uint8_t   ui8_startup_boost_fade_steps = 0;
uint16_t  ui16_startup_boost_fade_variable_x256;
uint16_t  ui16_startup_boost_fade_variable_step_amount_x256;
static void     boost_run_statemachine (void);
static uint8_t  boost(uint8_t ui8_max_current_boost_state);
static void     apply_boost_fade_out();
uint8_t ui8_boost_enabled_and_applied = 0;
static void apply_boost();


// UART
#define UART_NUMBER_DATA_BYTES_TO_RECEIVE   7   // change this value depending on how many data bytes there is to receive ( Package = one start byte + data bytes + two bytes 16 bit CRC )
#define UART_NUMBER_DATA_BYTES_TO_SEND      24  // change this value depending on how many data bytes there is to send ( Package = one start byte + data bytes + two bytes 16 bit CRC )

volatile uint8_t ui8_received_package_flag = 0;
volatile uint8_t ui8_rx_buffer[UART_NUMBER_DATA_BYTES_TO_RECEIVE + 3];
volatile uint8_t ui8_rx_counter = 0;
volatile uint8_t ui8_tx_buffer[UART_NUMBER_DATA_BYTES_TO_SEND + 3];
volatile uint8_t ui8_i;
volatile uint8_t ui8_byte_received;
volatile uint8_t ui8_state_machine = 0;
static uint16_t  ui16_crc_rx;
static uint16_t  ui16_crc_tx;
volatile uint8_t ui8_message_ID = 0;

static void communications_controller (void);
static void uart_receive_package (void);
static void uart_send_package (void);


// system functions
static void ebike_control_motor(void);
static void check_system(void);
static void check_brakes(void);


static void get_battery_voltage_filtered(void);
static void get_battery_current_filtered(void);
static void get_adc_pedal_torque(void);
static void calc_wheel_speed(void);
static void calc_cadence(void);
static void calc_crank_power(void);


static void apply_power_assist();
static void apply_torque_assist();
static void apply_cadence_assist();
static void apply_emtb_assist();
static void apply_virtual_throttle();
static void apply_walk_assist();
static void apply_cruise();
static void apply_cadence_sensor_calibration();
static void apply_throttle();
static void apply_speed_limit();
static void apply_temperature_limiting();


void ebike_app_controller (void)
{
  get_battery_voltage_filtered();   // get filtered voltage from FOC calculations
  get_battery_current_filtered();   // get filtered current from FOC calculations
  get_adc_pedal_torque();           // get 10 bit ADC pedal torque value
  
  calc_wheel_speed();               // calculate the wheel speed
  calc_cadence();                   // calculate the cadence and set limits from wheel speed
  calc_crank_power();               // calculate the crank power
  
  check_system();                   // check if there are any errors for motor control 
  check_brakes();                   // check if brakes are enabled for motor control
  
  communications_controller();      // get data to use for motor control and also send new data
  ebike_control_motor();            // use received data and sensor input to control motor 
}


static void ebike_control_motor (void)
{
  // reset control variables (safety)
  ui16_controller_duty_cycle_ramp_up_inverse_step = PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_DEFAULT;
  ui16_controller_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT;
  ui8_adc_battery_current_target = 0;
  ui8_duty_cycle_target = 0;
  
  // select riding mode
  switch (ui8_riding_mode)
  {
    case POWER_ASSIST_MODE: apply_power_assist(); break;
    
    case TORQUE_ASSIST_MODE: apply_torque_assist(); break;
    
    case CADENCE_ASSIST_MODE: apply_cadence_assist(); break;
    
    case eMTB_ASSIST_MODE: apply_emtb_assist(); break;
    
    case WALK_ASSIST_MODE: apply_walk_assist(); break;
    
    case CRUISE_MODE: apply_cruise(); break;

    case CADENCE_SENSOR_CALIBRATION_MODE: apply_cadence_sensor_calibration(); break;
  }
  
  // select optional ADC function
  switch (m_configuration_variables.ui8_optional_ADC_function)
  {
    case THROTTLE_CONTROL: apply_throttle(); break;
    
    case TEMPERATURE_CONTROL: apply_temperature_limiting(); break;
  }
  
  // speed limit
  apply_speed_limit();

  // force target current to 0 if brakes are enabled or if there are errors
  if (ui8_brakes_enabled || ui8_system_state != NO_ERROR) { ui8_adc_battery_current_target = 0; }

  // check if to enable the motor
  if (!ui8_motor_enabled &&
      ui16_motor_get_motor_speed_erps() == 0 && // only enable motor if stopped, other way something bad can happen due to high currents/regen or similar
      ui8_adc_battery_current_target)
  {
    ui8_motor_enabled = 1;
    ui8_g_duty_cycle = 0;
    motor_enable_pwm();
  }

  // check if to disable the motor
  if (ui8_motor_enabled &&
      ui16_motor_get_motor_speed_erps() == 0 &&
      ui8_adc_battery_current_target == 0 &&
      ui8_g_duty_cycle == 0)
  {
    ui8_motor_enabled = 0;
    motor_disable_pwm();
  }

  // set control parameters
  if (ui8_motor_enabled && !ui8_brakes_enabled)
  {
    // limit max current if higher than configured hardware limit (safety)
    if (ui8_adc_battery_current_max > ADC_10_BIT_BATTERY_CURRENT_MAX) { ui8_adc_battery_current_max = ADC_10_BIT_BATTERY_CURRENT_MAX; }
    
    // limit target current if higher than max value (safety)
    if (ui8_adc_battery_current_target > ui8_adc_battery_current_max) { ui8_adc_battery_current_target = ui8_adc_battery_current_max; }
    
    // limit target duty cycle if higher than max value
    if (ui8_duty_cycle_target > PWM_DUTY_CYCLE_MAX) { ui8_duty_cycle_target = PWM_DUTY_CYCLE_MAX; }
    
    // limit target duty cycle ramp up inverse step if lower than min value (safety)
    if (ui16_duty_cycle_ramp_up_inverse_step < PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_MIN) { ui16_duty_cycle_ramp_up_inverse_step = PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_MIN; } 
    
    // limit target duty cycle ramp down inverse step if lower than min value (safety)
    if (ui16_duty_cycle_ramp_down_inverse_step < PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_MIN) { ui16_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_MIN; } 
    
    // set duty cycle ramp up in controller
    ui16_controller_duty_cycle_ramp_up_inverse_step = ui16_duty_cycle_ramp_up_inverse_step;
    
    // set duty cycle ramp down in controller
    ui16_controller_duty_cycle_ramp_down_inverse_step = ui16_duty_cycle_ramp_down_inverse_step;
    
    // set target battery current in controller
    ui8_controller_adc_battery_current_target = ui8_adc_battery_current_target;
    
    // set target duty cycle in controller
    ui8_controller_duty_cycle_target = ui8_duty_cycle_target;
  }
  else
  {
    // reset control variables (safety)
    ui16_controller_duty_cycle_ramp_up_inverse_step = PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_DEFAULT;
    ui16_controller_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT;
    ui8_controller_adc_battery_current_target = 0;
    ui8_controller_duty_cycle_target = 0;
    ui8_g_duty_cycle = 0;
  }
}



static void calc_crank_power(void)
{
  // calculate torque on pedals
  ui16_pedal_torque_x100 = ui16_adc_pedal_torque_delta * m_configuration_variables.ui8_pedal_torque_per_10_bit_ADC_step_x100;

  // calculate crank power
  ui16_pedal_power_x10 = ((uint32_t) ui16_pedal_torque_x100 * ui8_pedal_cadence_RPM) / 105; // see note below

  /*---------------------------------------------------------

    NOTE: regarding the human power calculation
    
    Formula: power  =  force  *  rotations per second  *  2  *  pi
    Formula: power  =  force  *  rotations per minute  *  2  *  pi / 60
    
    (100 * 2 * pi) / 60 ≈ 1.047 -> 105
  ---------------------------------------------------------*/
}



static void apply_power_assist()
{
  uint8_t ui8_power_assist_multiplier_x10 = ui8_riding_mode_parameter;
  
  // calculate power assist
  uint32_t ui32_power_assist_x100 = (uint32_t) ui16_pedal_power_x10 * ui8_power_assist_multiplier_x10;
  
  // calculate target current
  uint16_t ui16_battery_current_target_x10 = (ui32_power_assist_x100 * 100) / ui16_battery_voltage_filtered_x1000;
  
  // set battery current target in ADC steps
  uint16_t ui16_adc_battery_current_target = ui16_battery_current_target_x10 / BATTERY_CURRENT_PER_10_BIT_ADC_STEP_X10;
  
  // set motor acceleration
  ui16_duty_cycle_ramp_up_inverse_step = map((uint32_t) ui16_wheel_speed_x10,
                                             (uint32_t) 40, // 40 -> 4 kph
                                             (uint32_t) 200, // 200 -> 20 kph
                                             (uint32_t) PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_DEFAULT,
                                             (uint32_t) PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_MIN);
                                             
  ui16_duty_cycle_ramp_down_inverse_step = map((uint32_t) ui16_wheel_speed_x10,
                                               (uint32_t) 40, // 40 -> 4 kph
                                               (uint32_t) 200, // 200 -> 20 kph
                                               (uint32_t) PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT,
                                               (uint32_t) PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_MIN);
                                               
  // set battery current target
  if (ui16_adc_battery_current_target > ui8_adc_battery_current_max) { ui8_adc_battery_current_target = ui8_adc_battery_current_max; }
  else { ui8_adc_battery_current_target = ui16_adc_battery_current_target; }
  
  // set duty cycle target
  if (ui8_adc_battery_current_target) { ui8_duty_cycle_target = PWM_DUTY_CYCLE_MAX; }
  else { ui8_duty_cycle_target = 0; }
}



static void apply_torque_assist()
{
  #define ADC_PEDAL_TORQUE_THRESHOLD          6     // minimum ADC torque to be applied for torque assist
  #define TORQUE_ASSIST_FACTOR_DENOMINATOR    110   // scale the torque assist factor
  
  uint8_t ui8_torque_assist_factor = ui8_riding_mode_parameter;
  uint16_t ui16_adc_battery_current_target_torque_assist = 0;
  
  // calculate torque assist target current
  if ((ui16_adc_pedal_torque_delta > ADC_PEDAL_TORQUE_THRESHOLD) && (ui8_pedal_cadence_RPM))
  {
    ui16_adc_battery_current_target_torque_assist = ((uint16_t) (ui16_adc_pedal_torque_delta - ADC_PEDAL_TORQUE_THRESHOLD) * ui8_torque_assist_factor) / TORQUE_ASSIST_FACTOR_DENOMINATOR;
  }
  
  // set motor acceleration
  ui16_duty_cycle_ramp_up_inverse_step = map((uint32_t) ui16_wheel_speed_x10,
                                             (uint32_t) 40, // 40 -> 4 kph
                                             (uint32_t) 200, // 200 -> 20 kph
                                             (uint32_t) PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_DEFAULT,
                                             (uint32_t) PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_MIN);
                                             
  ui16_duty_cycle_ramp_down_inverse_step = map((uint32_t) ui16_wheel_speed_x10,
                                               (uint32_t) 40, // 40 -> 4 kph
                                               (uint32_t) 200, // 200 -> 20 kph
                                               (uint32_t) PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT,
                                               (uint32_t) PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_MIN);
                                               
  // set battery current target
  if (ui16_adc_battery_current_target_torque_assist > ui8_adc_battery_current_max) { ui8_adc_battery_current_target = ui8_adc_battery_current_max; }
  else { ui8_adc_battery_current_target = ui16_adc_battery_current_target_torque_assist; }

  // set duty cycle target
  if (ui8_adc_battery_current_target) { ui8_duty_cycle_target = PWM_DUTY_CYCLE_MAX; }
  else { ui8_duty_cycle_target = 0; }
}



static void apply_cadence_assist()
{
  #define CADENCE_ASSIST_DUTY_CYCLE_RAMP_UP_INVERSE_STEP   200
  
  uint8_t ui8_cadence_assist_duty_cycle_target = ui8_riding_mode_parameter;
  
  // limit cadence assist duty cycle target
  if (ui8_cadence_assist_duty_cycle_target > PWM_DUTY_CYCLE_MAX) { ui8_cadence_assist_duty_cycle_target = PWM_DUTY_CYCLE_MAX; }
  
  // set motor acceleration
  ui16_duty_cycle_ramp_up_inverse_step = map((uint32_t) ui16_wheel_speed_x10,
                                             (uint32_t) 40, // 40 -> 4 kph
                                             (uint32_t) 200, // 200 -> 20 kph
                                             (uint32_t) CADENCE_ASSIST_DUTY_CYCLE_RAMP_UP_INVERSE_STEP,
                                             (uint32_t) PWM_DUTY_CYCLE_RAMP_UP_INVERSE_STEP_MIN);
                                             
  ui16_duty_cycle_ramp_down_inverse_step = map((uint32_t) ui16_wheel_speed_x10,
                                               (uint32_t) 40, // 40 -> 4 kph
                                               (uint32_t) 200, // 200 -> 20 kph
                                               (uint32_t) PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT,
                                               (uint32_t) PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_MIN);
                                               
  // set battery and duty cycle target
  if (ui8_pedal_cadence_RPM)
  {
    // set battery current target
    ui8_adc_battery_current_target = ui8_adc_battery_current_max;
    
    // set duty cycle target
    ui8_duty_cycle_target = ui8_cadence_assist_duty_cycle_target;
  }
  else
  {
    // set battery current target
    ui8_adc_battery_current_target = 0;
    
    // set duty cycle target
    ui8_duty_cycle_target = 0;
  }
}



static void apply_emtb_assist()
{
  // TESTING MOTOR CURRENT CONTROL THIS IS NOT CORRECT


}



static void apply_walk_assist()
{
  if (ui16_wheel_speed_x10 < WALK_ASSIST_THRESHOLD_SPEED_X10)
  {
    #define WALK_ASSIST_DUTY_CYCLE_RAMP_UP_INVERSE_STEP   200
    #define WALK_ASSIST_DUTY_CYCLE_MAX                    80
    
    uint8_t ui8_walk_assist_duty_cycle_target = ui8_riding_mode_parameter;
    
    // check so that walk assist level factor is not too large (too powerful), if it is -> limit the value
    if (ui8_walk_assist_duty_cycle_target > WALK_ASSIST_DUTY_CYCLE_MAX) { ui8_walk_assist_duty_cycle_target = WALK_ASSIST_DUTY_CYCLE_MAX; }
    
    // set motor acceleration
    ui16_duty_cycle_ramp_up_inverse_step = WALK_ASSIST_DUTY_CYCLE_RAMP_UP_INVERSE_STEP;
    ui16_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT;
    
    // set battery current target
    ui8_adc_battery_current_target = ui8_adc_battery_current_max;
    
    // set duty cycle target
    ui8_duty_cycle_target = ui8_walk_assist_duty_cycle_target;
  }
}



static void apply_cruise()
{
  static uint8_t ui8_cruise_PID_initialized;
  
  if (ui16_wheel_speed_x10 > CRUISE_THRESHOLD_SPEED_X10)
  {
    #define CRUISE_PID_KP                             12    // 48 volt motor: 12, 36 volt motor: 14
    #define CRUISE_PID_KI                             0.7   // 48 volt motor: 1, 36 volt motor: 0.7
    #define CRUISE_PID_INTEGRAL_LIMIT                 1000
    #define CRUISE_PID_KD                             0
    #define CRUISE_DUTY_CYCLE_RAMP_UP_INVERSE_STEP    80
    
    static int16_t i16_error;
    static int16_t i16_last_error;
    static int16_t i16_integral;
    static int16_t i16_derivative;
    static int16_t i16_control_output;
    static uint16_t ui16_wheel_speed_target_x10;
    
    // initialize cruise PID controller
    if (!ui8_cruise_PID_initialized)
    {
      // reset flag to save current speed to maintain (for cruise function)
      ui8_cruise_PID_initialized = 1;
      
      // reset PID variables
      i16_error = 0;          // error should be 0 when cruise function starts
      i16_last_error = 0;     // last error should be 0 when cruise function starts 
      i16_integral = 250;     // integral can start at around 250 when cruise function starts ( 250 = around 64 target PWM = around 8 km/h depending on gear and bike )
      i16_derivative = 0;     // derivative should be 0 when cruise function starts 
      i16_control_output = 0; // control signal/output should be 0 when cruise function starts
      
      // check what target wheel speed to use (received or current)
      uint16_t ui16_wheel_speed_target_received_x10 = (uint16_t) ui8_riding_mode_parameter * 10;
      
      if (ui16_wheel_speed_target_received_x10 > 0)
      {
        // set received target wheel speed to target wheel speed
        ui16_wheel_speed_target_x10 = ui16_wheel_speed_target_received_x10;
      }
      else
      {
        // set current wheel speed to maintain
        ui16_wheel_speed_target_x10 = ui16_wheel_speed_x10;
      }
    }
    
    // calculate error
    i16_error = (ui16_wheel_speed_target_x10 - ui16_wheel_speed_x10);
    
    // calculate integral
    i16_integral = i16_integral + i16_error;
    
    // limit integral
    if (i16_integral > CRUISE_PID_INTEGRAL_LIMIT)
    {
      i16_integral = CRUISE_PID_INTEGRAL_LIMIT; 
    }
    else if (i16_integral < 0)
    {
      i16_integral = 0;
    }
    
    // calculate derivative
    i16_derivative = i16_error - i16_last_error;

    // save error to last error
    i16_last_error = i16_error;
    
    // calculate control output ( output =  P I D )
    i16_control_output = (CRUISE_PID_KP * i16_error) + (CRUISE_PID_KI * i16_integral) + (CRUISE_PID_KD * i16_derivative);
    
    // limit control output to just positive values
    if (i16_control_output < 0) { i16_control_output = 0; }
    
    // limit control output to the maximum value
    if (i16_control_output > 1000) { i16_control_output = 1000; }
    
    // set motor acceleration
    ui16_duty_cycle_ramp_up_inverse_step = CRUISE_DUTY_CYCLE_RAMP_UP_INVERSE_STEP;
    ui16_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT;
    
    // set battery current target
    ui8_adc_battery_current_target = ui8_adc_battery_current_max;
    
    // set duty cycle target  |  map the control output to an appropriate target PWM value
    ui8_duty_cycle_target = (uint8_t) (map((uint32_t) i16_control_output,
                                           (uint32_t) 0,                     // minimum control output from PID
                                           (uint32_t) 1000,                  // maximum control output from PID
                                           (uint32_t) 0,                     // minimum duty cycle
                                           (uint32_t) PWM_DUTY_CYCLE_MAX));  // maximum duty cycle
  }
  else
  {
    // set flag to initialize cruise PID if user later activates function
    ui8_cruise_PID_initialized = 0;
  }
}



static void apply_cadence_sensor_calibration()
{
  #define CADENCE_SENSOR_CALIBRATION_DUTY_CYCLE_RAMP_UP_INVERSE_STEP   200
  
  // limit acceleration
  ui16_duty_cycle_ramp_up_inverse_step = CADENCE_SENSOR_CALIBRATION_DUTY_CYCLE_RAMP_UP_INVERSE_STEP;

  // set battery current target
  ui8_adc_battery_current_target = 5; // 5 -> 5 * 0.2 = 1 A
  
  // set duty cycle target
  ui8_duty_cycle_target = 22;
}



static void apply_throttle()
{
  if ((ui8_riding_mode != WALK_ASSIST_MODE) && (ui8_riding_mode != CRUISE_MODE))
  {
    #define THROTTLE_DUTY_CYCLE_RAMP_UP_INVERSE_STEP    80
    
    // map value from 0 up to duty cycle max
    ui8_adc_throttle = map((uint8_t) UI8_ADC_THROTTLE,
                           (uint8_t) ADC_THROTTLE_MIN_VALUE,
                           (uint8_t) ADC_THROTTLE_MAX_VALUE,
                           (uint8_t) 0,
                           (uint8_t) 255);
                            
    // map ADC throttle value from 0 to max battery current
    uint8_t ui8_adc_battery_current_target_throttle = map((uint8_t) ui8_adc_throttle,
                                                          (uint8_t) 0,
                                                          (uint8_t) 255,
                                                          (uint8_t) 0,
                                                          (uint8_t) ui8_adc_battery_current_max);
                                                           
    if (ui8_adc_battery_current_target_throttle > ui8_adc_battery_current_target)
    {
      // set motor acceleration
      ui16_duty_cycle_ramp_up_inverse_step = THROTTLE_DUTY_CYCLE_RAMP_UP_INVERSE_STEP;
      ui16_duty_cycle_ramp_down_inverse_step = PWM_DUTY_CYCLE_RAMP_DOWN_INVERSE_STEP_DEFAULT;
      
      // set battery current target
      ui8_adc_battery_current_target = ui8_adc_battery_current_target_throttle;
      
      // set duty cycle target
      ui8_duty_cycle_target = PWM_DUTY_CYCLE_MAX;
    }
  }
}



static void apply_temperature_limiting()
{
  static uint16_t ui16_adc_motor_temperature_filtered;
  
  // calculate motor temperature
  volatile uint16_t ui16_temp = UI16_ADC_10_BIT_THROTTLE;
  ui16_filter(&ui16_temp, &ui16_adc_motor_temperature_filtered, 5);
  
  m_configuration_variables.ui16_motor_temperature_x2 = (uint16_t) ui16_adc_motor_temperature_filtered / 1.024;
  m_configuration_variables.ui8_motor_temperature = (uint8_t) (m_configuration_variables.ui16_motor_temperature_x2 >> 1);
  
  // min temperature value can't be equal or higher than max temperature value...
  if (m_configuration_variables.ui8_motor_temperature_min_value_to_limit >= m_configuration_variables.ui8_motor_temperature_max_value_to_limit)
  {
    ui8_adc_battery_current_target = 0;
    m_configuration_variables.ui8_temperature_current_limiting_value = 0;
  }
  else
  {
    // reduce motor current if over temperature
    ui8_adc_battery_current_target = (map((uint32_t) m_configuration_variables.ui16_motor_temperature_x2,
                                          (uint32_t) (((uint16_t) m_configuration_variables.ui8_motor_temperature_min_value_to_limit) << 1),
                                          (uint32_t) (((uint16_t) m_configuration_variables.ui8_motor_temperature_max_value_to_limit) << 1),
                                          (uint32_t) ui8_adc_battery_current_target,
                                          (uint32_t) 0));
                                           
    // get a value linear to the current limitation, just to show to user
    m_configuration_variables.ui8_temperature_current_limiting_value = (map((uint32_t) m_configuration_variables.ui16_motor_temperature_x2,
                                                                            (uint32_t) (((uint16_t) m_configuration_variables.ui8_motor_temperature_min_value_to_limit) << 1),
                                                                            (uint32_t) (((uint16_t) m_configuration_variables.ui8_motor_temperature_max_value_to_limit) << 1),
                                                                            (uint32_t) 255,
                                                                            (uint32_t) 0));
  }
}



static void apply_speed_limit()
{
  if (m_configuration_variables.ui8_wheel_speed_max > 0)
  {
    // set battery current target 
    ui8_adc_battery_current_target = (uint8_t) (map((uint32_t) ui16_wheel_speed_x10,
                                                    (uint32_t) ((m_configuration_variables.ui8_wheel_speed_max * 10) - 20),
                                                    (uint32_t) ((m_configuration_variables.ui8_wheel_speed_max * 10) + 20),
                                                    (uint32_t) ui8_adc_battery_current_target,
                                                    (uint32_t) 0));
  }
}



static void calc_wheel_speed(void)
{ 
  // calc wheel speed in km/h
  if (ui16_wheel_speed_sensor_ticks)
  {
    float f_wheel_speed_x10 = (float) PWM_CYCLES_SECOND / ui16_wheel_speed_sensor_ticks; // rps
    ui16_wheel_speed_x10 = (uint16_t) (f_wheel_speed_x10 * m_configuration_variables.ui16_wheel_perimeter * 0.036); // rps * millimeters per second * ((3600 / (1000 * 1000)) * 10) kms per hour * 10
  }
  else
  {
    ui16_wheel_speed_x10 = 0;
  }
}



static void calc_cadence(void)
{
  #define CADENCE_SENSOR_TICKS_COUNTER_MIN_AT_SPEED    1000
  
  // scale cadence sensor ticks counter min depending on wheel speed
  uint16_t ui16_cadence_sensor_ticks_counter_min_temp = map((uint32_t) ui16_wheel_speed_x10,
                                                            (uint32_t) 40,
                                                            (uint32_t) 200,
                                                            (uint32_t) CADENCE_SENSOR_TICKS_COUNTER_MIN,
                                                            (uint32_t) CADENCE_SENSOR_TICKS_COUNTER_MIN_AT_SPEED);
                                                             
  // set parameters for cadence calculation
  if (ui8_cadence_sensor_magnet_pulse_width > 199)
  {
    // set the magnet pulse width in ticks
    ui16_cadence_sensor_high_ticks_counter_min = ui16_cadence_sensor_ticks_counter_min_temp * 2;
    ui16_cadence_sensor_low_ticks_counter_min = ui16_cadence_sensor_ticks_counter_min_temp * 2;
    
    // set the conversion ratio
    ui16_cadence_sensor_high_conversion_x100 = 100;
    ui16_cadence_sensor_low_conversion_x100 = 100;
  }
  else
  {
    // limit magnet pulse width and avoid zero division
    if (ui8_cadence_sensor_magnet_pulse_width < 1) { ui8_cadence_sensor_magnet_pulse_width = 1; }
    
    // set the magnet pulse width in ticks
    ui16_cadence_sensor_high_ticks_counter_min = ((uint32_t) ui8_cadence_sensor_magnet_pulse_width * ui16_cadence_sensor_ticks_counter_min_temp) / 100;
    ui16_cadence_sensor_low_ticks_counter_min = ((uint32_t) (200 - ui8_cadence_sensor_magnet_pulse_width) * ui16_cadence_sensor_ticks_counter_min_temp) / 100;
    
    // set the conversion ratio adjusting for double the transitions and magnet pulse width
    ui16_cadence_sensor_high_conversion_x100 = (uint16_t) 20000 / ui8_cadence_sensor_magnet_pulse_width;
    ui16_cadence_sensor_low_conversion_x100 = (uint16_t) 20000 / (200 - ui8_cadence_sensor_magnet_pulse_width);
  }
  
  // calculate cadence in RPM and avoid zero division
  if (ui16_cadence_sensor_ticks)
  {
    ui8_pedal_cadence_RPM = 4687500 / ((uint32_t) ui16_cadence_sensor_ticks * ui16_cadence_sensor_conversion_x100); // see note below
  }
  else
  {
    ui8_pedal_cadence_RPM = 0;
  }
  
  /*-------------------------------------------------------------------------------
  
    NOTE: regarding the cadence calculation
    
    Cadence is calculated by counting how many ticks there are between two 
    transitions. Usually it is measured from 1 -> 1 or 0 -> 0. But to double the 
    resoultion and system response it is possible to use the 1 -> 0 and 0 -> 1 
    transitions. But when doing so it is important to adjust for the different 
    spacings between the transitions. This is why there is a conversion factor.
    
    Formula for calculating the cadence in RPM:
    
    (1) Cadence in RPM = 6000 / (ticks * conversion_x100 * CADENCE_SENSOR_NUMBER_MAGNETS * 0.000064)
    
    (2) Cadence in RPM = 6000 / (ticks * conversion_x100 * 0.00128)
    
    (3) Cadence in RPM = 4687500 / (ticks * conversion_x100)
    
  -------------------------------------------------------------------------------*/
}



static void get_battery_voltage_filtered(void)
{
  ui16_battery_voltage_filtered_x1000 = ui16_adc_battery_voltage_filtered * BATTERY_VOLTAGE_PER_10_BIT_ADC_STEP_X1000;
}



static void get_battery_current_filtered(void)
{
  ui8_battery_current_filtered_x10 = ui8_adc_battery_current_filtered * BATTERY_CURRENT_PER_10_BIT_ADC_STEP_X10;
}



static void get_adc_pedal_torque(void)
{
  // get adc pedal torque
  ui16_adc_pedal_torque = UI16_ADC_10_BIT_TORQUE_SENSOR;
  
  // calculate the delta value of adc pedal torque and the adc pedal torque offset from calibration
  if (ui16_adc_pedal_torque > ui16_adc_pedal_torque_offset)
  {
    ui16_adc_pedal_torque_delta = ui16_adc_pedal_torque - ui16_adc_pedal_torque_offset;
  }
  else
  {
    ui16_adc_pedal_torque_delta = 0;
  }
}



struct_configuration_variables* get_configuration_variables (void)
{
  return &m_configuration_variables;
}



static void check_brakes()
{
  // check if brakes are installed
  
  // set brake state
  ui8_brakes_enabled = brake_is_set();
}



static void check_system()
{
  #define MOTOR_BLOCKED_COUNTER_THRESHOLD               10    // 10  =>  1.0 second
  #define MOTOR_BLOCKED_BATTERY_CURRENT_THRESHOLD_X10   50    // 50  =>  5.0 amps
  #define MOTOR_BLOCKED_ERPS_THRESHOLD                  10    // 10 ERPS
  #define MOTOR_BLOCKED_RESET_COUNTER_THRESHOLD         100   // 100  =>  10 seconds
  
  static uint8_t ui8_motor_blocked_counter;
  static uint8_t ui8_motor_blocked_reset_counter;

  // if the motor blocked error is enabled start resetting it
  if (ui8_system_state == ERROR_MOTOR_BLOCKED)
  {
    // increment motor blocked reset counter with 100 milliseconds
    ui8_motor_blocked_reset_counter++;
    
    // check if the counter has counted to the set threshold for reset
    if (ui8_motor_blocked_reset_counter > MOTOR_BLOCKED_RESET_COUNTER_THRESHOLD)
    {
      // reset motor blocked error code
      if (ui8_system_state == ERROR_MOTOR_BLOCKED) { ui8_system_state = NO_ERROR; }
      
      // reset the counter that clears the motor blocked error
      ui8_motor_blocked_reset_counter = 0;
    }
  }
  else
  {
    // if battery current is over the current threshold and the motor ERPS is below threshold start setting motor blocked error code
    if ((ui8_battery_current_filtered_x10 > MOTOR_BLOCKED_BATTERY_CURRENT_THRESHOLD_X10) && (ui16_motor_get_motor_speed_erps() < MOTOR_BLOCKED_ERPS_THRESHOLD))
    {
      // increment motor blocked counter with 100 milliseconds
      ++ui8_motor_blocked_counter;
      
      // check if motor is blocked for more than some safe threshold
      if (ui8_motor_blocked_counter > MOTOR_BLOCKED_COUNTER_THRESHOLD)
      {
        // set error code
        ui8_system_state = ERROR_MOTOR_BLOCKED;
        
        // reset motor blocked counter as the error code is set
        ui8_motor_blocked_counter = 0;
      }
    }
    else
    {
      // current is below the threshold and/or motor ERPS is above the threshold so reset the counter
      ui8_motor_blocked_counter = 0;
    }
  }
  
  
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  
  // check torque sensor
  if (((ui16_adc_pedal_torque_offset > 300) || (ui16_adc_pedal_torque_offset < 5)) &&
      ((ui8_riding_mode == POWER_ASSIST_MODE) || (ui8_riding_mode == TORQUE_ASSIST_MODE) || (ui8_riding_mode == eMTB_ASSIST_MODE)))
  {
    // set error code
    ui8_system_state = ERROR_TORQUE_SENSOR;
  }
  else if (ui8_system_state == ERROR_TORQUE_SENSOR)
  {
    // reset error code
    ui8_system_state = NO_ERROR;
  }
}



// This is the interrupt that happens when UART2 receives data. We need it to be the fastest possible and so
// we do: receive every byte and assembly as a package, finally, signal that we have a package to process (on main slow loop)
// and disable the interrupt. The interrupt should be enable again on main loop, after the package being processed
void UART2_IRQHandler(void) __interrupt(UART2_IRQHANDLER)
{
  if (UART2_GetFlagStatus(UART2_FLAG_RXNE) == SET)
  {
    UART2->SR &= (uint8_t)~(UART2_FLAG_RXNE); // this may be redundant

    ui8_byte_received = UART2_ReceiveData8 ();

    switch (ui8_state_machine)
    {
      case 0:
      if (ui8_byte_received == 0x59) // see if we get start package byte
      {
        ui8_rx_buffer [ui8_rx_counter] = ui8_byte_received;
        ui8_rx_counter++;
        ui8_state_machine = 1;
      }
      else
      {
        ui8_rx_counter = 0;
        ui8_state_machine = 0;
      }
      break;

      case 1:
      ui8_rx_buffer [ui8_rx_counter] = ui8_byte_received;
      
      // increment index for next byte
      ui8_rx_counter++;

      // reset if it is the last byte of the package and index is out of bounds
      if (ui8_rx_counter >= UART_NUMBER_DATA_BYTES_TO_RECEIVE + 3)
      {
        ui8_rx_counter = 0;
        ui8_state_machine = 0;
        ui8_received_package_flag = 1; // signal that we have a full package to be processed
        UART2->CR2 &= ~(1 << 5); // disable UART2 receive interrupt
      }
      break;

      default:
      break;
    }
  }
}

static void communications_controller (void)
{
#ifndef DEBUG_UART
  
  // reset riding mode (safety)
  ui8_riding_mode = OFF_MODE;
  
  uart_receive_package ();

  uart_send_package ();

#endif
}

static void uart_receive_package(void)
{
  if (ui8_received_package_flag)
  {
    // validation of the package data
    ui16_crc_rx = 0xffff;
    
    for (ui8_i = 0; ui8_i <= UART_NUMBER_DATA_BYTES_TO_RECEIVE; ui8_i++)
    {
      crc16 (ui8_rx_buffer[ui8_i], &ui16_crc_rx);
    }

    // if CRC is correct read the package (16 bit value and therefore last two bytes)
    if (((((uint16_t) ui8_rx_buffer [UART_NUMBER_DATA_BYTES_TO_RECEIVE + 2]) << 8) + ((uint16_t) ui8_rx_buffer [UART_NUMBER_DATA_BYTES_TO_RECEIVE + 1])) == ui16_crc_rx)
    {
      // message ID
      ui8_message_ID = ui8_rx_buffer [1];
      
      // riding mode
      ui8_riding_mode = ui8_rx_buffer [2];
      
      // riding mode parameter
      ui8_riding_mode_parameter = ui8_rx_buffer [3];
      
      // lights state
      m_configuration_variables.ui8_lights = ui8_rx_buffer [4];
      
      // set lights
      lights_set_state(m_configuration_variables.ui8_lights);

      switch (ui8_message_ID)
      {
        case 0:
        
          // battery low voltage cut off x10
          m_configuration_variables.ui16_battery_low_voltage_cut_off_x10 = (((uint16_t) ui8_rx_buffer [6]) << 8) + ((uint16_t) ui8_rx_buffer [5]);
          
          // set low voltage cut off
          ui8_adc_battery_voltage_cut_off = (uint8_t) (((uint32_t) m_configuration_variables.ui16_battery_low_voltage_cut_off_x10 << 8) / (BATTERY_VOLTAGE_PER_8_BIT_ADC_STEP_X256 * 10));
          
          // wheel max speed
          m_configuration_variables.ui8_wheel_speed_max = ui8_rx_buffer [7];
          
        break;

        case 1:
        
          // wheel perimeter
          m_configuration_variables.ui16_wheel_perimeter = (((uint16_t) ui8_rx_buffer [6]) << 8) + ((uint16_t) ui8_rx_buffer [5]);
          
          // motor temperature limit function or throttle
          m_configuration_variables.ui8_optional_ADC_function = ui8_rx_buffer [7];

        break;

        case 2:
        
          // type of motor (36 volt, 48 volt or some experimental type)
          m_configuration_variables.ui8_motor_type = ui8_rx_buffer [5];
          
          // motor over temperature min value limit
          m_configuration_variables.ui8_motor_temperature_min_value_to_limit = ui8_rx_buffer [6];
          
          // motor over temperature max value limit
          m_configuration_variables.ui8_motor_temperature_max_value_to_limit = ui8_rx_buffer [7];

        break;

        case 3:
        
          // boost assist level
          m_configuration_variables.ui8_startup_motor_power_boost_assist_level = ui8_rx_buffer [5];
          
          // boost state
          m_configuration_variables.ui8_startup_motor_power_boost_state = (ui8_rx_buffer [6] & 1);
          
          // boost max power limit enabled
          m_configuration_variables.ui8_startup_motor_power_boost_limit_to_max_power = (ui8_rx_buffer [6] & 2) >> 1;
          
          // boost runtime
          m_configuration_variables.ui8_startup_motor_power_boost_time = ui8_rx_buffer [7];
          
        break;

        case 4:

          // boost fade time
          m_configuration_variables.ui8_startup_motor_power_boost_fade_time = ui8_rx_buffer [5];
          
          // boost enabled
          m_configuration_variables.ui8_startup_motor_power_boost_feature_enabled = ui8_rx_buffer [6];
          
          // motor acceleration
          uint8_t ui8_motor_acceleration = ui8_rx_buffer [7];
          
          // limit motor acceleration
          //if (ui8_motor_acceleration > 100) { ui8_motor_acceleration = 100; }
          
          // set duty cycle ramp up inverse step
          //ui16_duty_cycle_ramp_up_inverse_step = 200;//120 - ui8_motor_acceleration;                                                                                    //FIXFIXFIXFIXFIX
        
        break;

        case 5:
        
          // pedal torque conversion
          m_configuration_variables.ui8_pedal_torque_per_10_bit_ADC_step_x100 = ui8_rx_buffer [5];
          
          // max battery current
          m_configuration_variables.ui8_battery_max_current = ui8_rx_buffer [6];
          
          // battery power limit
          m_configuration_variables.ui8_target_battery_max_power_div25 = ui8_rx_buffer [7];
          
          // calculate max battery current in ADC steps from the received battery current limit
          uint8_t ui8_adc_battery_current_max_temp_1 = ((m_configuration_variables.ui8_battery_max_current * 10) / BATTERY_CURRENT_PER_10_BIT_ADC_STEP_X10);
          
          // calculate max battery current in ADC steps from the received power limit
          uint32_t ui32_battery_current_max_x10 = ((uint32_t) m_configuration_variables.ui8_target_battery_max_power_div25 * 250000) / ui16_battery_voltage_filtered_x1000;
          uint8_t ui8_adc_battery_current_max_temp_2 = ui32_battery_current_max_x10 / BATTERY_CURRENT_PER_10_BIT_ADC_STEP_X10;
          
          // set max battery current
          ui8_adc_battery_current_max = ui8_min(ui8_adc_battery_current_max_temp_1, ui8_adc_battery_current_max_temp_2);
        
        break;
        
        case 6:
          
          // cadence sensor magnet pulse width
          ui8_cadence_sensor_magnet_pulse_width = ui8_rx_buffer [5];
          
          uint8_t ui8_temp_1 = ui8_rx_buffer [6];
          
          uint8_t ui8_temp_2 = ui8_rx_buffer [7];
        
        break;

        default:
          // nothing, should display error code
        break;
      }

      // check if any configuration_variables did change and if so, save all of them in the EEPROM
      eeprom_write_if_values_changed();

      // signal that we processed the full package
      ui8_received_package_flag = 0;
    }

    // enable UART2 receive interrupt as we are now ready to receive a new package
    UART2->CR2 |= (1 << 5);
  }
}

static void uart_send_package(void)
{
  uint16_t ui16_temp;

  // start up byte
  ui8_tx_buffer[0] = 0x43;

  // battery voltage filtered x1000
  ui16_temp = ui16_battery_voltage_filtered_x1000;
  ui8_tx_buffer[1] = (uint8_t) (ui16_temp & 0xff);;
  ui8_tx_buffer[2] = (uint8_t) (ui16_temp >> 8);
  
  // battery current filtered x10
  ui8_tx_buffer[3] = ui8_battery_current_filtered_x10;

  // wheel speed x10
  ui8_tx_buffer[4] = (uint8_t) (ui16_wheel_speed_x10 & 0xff);
  ui8_tx_buffer[5] = (uint8_t) (ui16_wheel_speed_x10 >> 8);

  // brake state
  ui8_tx_buffer[6] = ui8_brakes_enabled;

  // optional ADC channel value
  ui8_tx_buffer[7] = UI8_ADC_THROTTLE;
  
  switch (m_configuration_variables.ui8_optional_ADC_function)
  {
    case THROTTLE_CONTROL:
      
      // throttle value with offset applied and mapped from 0 to 255
      ui8_tx_buffer[8] = ui8_adc_throttle;
    
    break;
    
    case TEMPERATURE_CONTROL:
    
      // current limiting mapped from 0 to 255
      ui8_tx_buffer[8] = m_configuration_variables.ui8_temperature_current_limiting_value;
    
    break;
  }

  // ADC torque sensor
  ui16_temp = ui16_adc_pedal_torque;
  ui8_tx_buffer[9] = (uint8_t) (ui16_temp & 0xff);
  ui8_tx_buffer[10] = (uint8_t) (ui16_temp >> 8);

  // pedal cadence
  ui8_tx_buffer[11] = ui8_pedal_cadence_RPM;
  
  // PWM duty_cycle
  ui8_tx_buffer[12] = ui8_g_duty_cycle;
  
  // motor speed in ERPS
  ui16_temp = ui16_adc_pedal_torque_offset; //ui16_motor_get_motor_speed_erps();                          // CHANGE CHANGE CHANGE
  ui8_tx_buffer[13] = (uint8_t) (ui16_temp & 0xff);
  ui8_tx_buffer[14] = (uint8_t) (ui16_temp >> 8);
  
  // FOC angle
  ui8_tx_buffer[15] = ui8_g_foc_angle;
  
  // system state
  ui8_tx_buffer[16] = ui8_system_state;
  
  // motor temperature
  ui8_tx_buffer[17] = m_configuration_variables.ui8_motor_temperature;
  
  // wheel_speed_sensor_tick_counter
  ui8_tx_buffer[18] = (uint8_t) (ui32_wheel_speed_sensor_ticks_total & 0xff);
  ui8_tx_buffer[19] = (uint8_t) ((ui32_wheel_speed_sensor_ticks_total >> 8) & 0xff);
  ui8_tx_buffer[20] = (uint8_t) ((ui32_wheel_speed_sensor_ticks_total >> 16) & 0xff);

  // pedal torque x100
  ui16_temp = ui16_pedal_torque_x100;
  ui8_tx_buffer[21] = (uint8_t) (ui16_temp & 0xff);
  ui8_tx_buffer[22] = (uint8_t) (ui16_temp >> 8);

  // pedal power x10
  ui8_tx_buffer[23] = (uint8_t) (ui16_pedal_power_x10 & 0xff);
  ui8_tx_buffer[24] = (uint8_t) (ui16_pedal_power_x10 >> 8);

  // prepare crc of the package
  ui16_crc_tx = 0xffff;
  
  for (ui8_i = 0; ui8_i <= UART_NUMBER_DATA_BYTES_TO_SEND; ui8_i++)
  {
    crc16 (ui8_tx_buffer[ui8_i], &ui16_crc_tx);
  }
  
  ui8_tx_buffer[UART_NUMBER_DATA_BYTES_TO_SEND + 1] = (uint8_t) (ui16_crc_tx & 0xff);
  ui8_tx_buffer[UART_NUMBER_DATA_BYTES_TO_SEND + 2] = (uint8_t) (ui16_crc_tx >> 8) & 0xff;

  // send the full package to UART
  for (ui8_i = 0; ui8_i <= UART_NUMBER_DATA_BYTES_TO_SEND + 2; ui8_i++)
  {
    putchar (ui8_tx_buffer[ui8_i]);
  }
}





/* static void apply_boost()
{
  ui8_boost_enabled_and_applied = 0;
  uint8_t ui8_adc_max_battery_current_boost_state = 0;

  // 1.6 = 1 / 0.625 (each adc step for current)
  // 25 * 1.6 = 40
  // 40 * 4 = 160
  if(m_configuration_variables.ui8_startup_motor_power_boost_assist_level > 0)
  {
    uint32_t ui32_temp;
    ui32_temp = (uint32_t) ui16_pedal_torque_x100 * (uint32_t) m_configuration_variables.ui8_startup_motor_power_boost_assist_level;
    ui32_temp /= 100;

    // 1.6 = 1 / 0.625 (each adc step for current)
    // 1.6 * 8 = ~13
    ui32_temp = (ui32_temp * 13000) / ((uint32_t) ui16_battery_voltage_filtered_x1000);
    ui8_adc_max_battery_current_boost_state = ui32_temp >> 3;
    ui8_limit_max(&ui8_adc_max_battery_current_boost_state, 255);
  }
  
  // apply boost and boost fade out
  if(m_configuration_variables.ui8_startup_motor_power_boost_feature_enabled)
  {
    boost_run_statemachine();
    ui8_boost_enabled_and_applied = boost(ui8_adc_max_battery_current_boost_state);
    apply_boost_fade_out();
  }
}


static uint8_t boost(uint8_t ui8_max_current_boost_state)
{
  uint8_t ui8_boost_enable = ui8_startup_boost_enable && ui8_riding_mode_parameter && ui8_pedal_cadence_RPM > 0 ? 1 : 0;

  if (ui8_boost_enable)
  {
    ui8_adc_battery_current_target = ui8_max_current_boost_state;
  }

  return ui8_boost_enable;
}


static void apply_boost_fade_out()
{
  if (ui8_startup_boost_fade_enable)
  {
    // here we try to converge to the regular value, ramping down or up step by step
    uint16_t ui16_adc_battery_target_current_x256 = ((uint16_t) ui8_adc_battery_current_target) << 8;
    if (ui16_startup_boost_fade_variable_x256 > ui16_adc_battery_target_current_x256)
    {
      ui16_startup_boost_fade_variable_x256 -= ui16_startup_boost_fade_variable_step_amount_x256;
    }
    else if (ui16_startup_boost_fade_variable_x256 < ui16_adc_battery_target_current_x256)
    {
      ui16_startup_boost_fade_variable_x256 += ui16_startup_boost_fade_variable_step_amount_x256;
    }

    ui8_adc_battery_current_target = (uint8_t) (ui16_startup_boost_fade_variable_x256 >> 8);
  }
}

static void boost_run_statemachine(void)
{
  #define BOOST_STATE_BOOST_DISABLED        0
  #define BOOST_STATE_BOOST                 1
  #define BOOST_STATE_FADE                  2
  #define BOOST_STATE_BOOST_WAIT_TO_RESTART 3
  
  uint8_t ui8_torque_sensor = ui16_adc_pedal_torque_delta;

  if(m_configuration_variables.ui8_startup_motor_power_boost_time > 0)
  {
    switch(ui8_m_startup_boost_state_machine)
    {
      // ebike is stopped, wait for throttle signal to startup boost
      case BOOST_STATE_BOOST_DISABLED:
      
        if (ui8_torque_sensor > 12 && (ui8_brakes_enabled == 0))
        {
          ui8_startup_boost_enable = 1;
          ui8_startup_boost_timer = m_configuration_variables.ui8_startup_motor_power_boost_time;
          ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST;
        }
        
      break;

      case BOOST_STATE_BOOST:
      
        // braking means reseting
        if(ui8_brakes_enabled)
        {
          ui8_startup_boost_enable = 0;
          ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST_DISABLED;
        }

        // end boost if
        if(ui8_torque_sensor < 12)
        {
          ui8_startup_boost_enable = 0;
          ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST_WAIT_TO_RESTART;
        }

        // decrement timer
        if(ui8_startup_boost_timer > 0) { ui8_startup_boost_timer--; }

        // end boost and start fade if
        if(ui8_startup_boost_timer == 0)
        {
          ui8_m_startup_boost_state_machine = BOOST_STATE_FADE;
          ui8_startup_boost_enable = 0;

          // setup variables for fade
          ui8_startup_boost_fade_steps = m_configuration_variables.ui8_startup_motor_power_boost_fade_time;
          ui16_startup_boost_fade_variable_x256 = ((uint16_t) ui8_adc_battery_current_target << 8);
          ui16_startup_boost_fade_variable_step_amount_x256 = (ui16_startup_boost_fade_variable_x256 / ((uint16_t) ui8_startup_boost_fade_steps));
          ui8_startup_boost_fade_enable = 1;
        }
      break;

      case BOOST_STATE_FADE:
        // braking means reseting
        if(ui8_brakes_enabled)
        {
          ui8_startup_boost_fade_enable = 0;
          ui8_startup_boost_fade_steps = 0;
          ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST_DISABLED;
        }

        if(ui8_startup_boost_fade_steps > 0) { ui8_startup_boost_fade_steps--; }

        // disable fade if
        if(ui8_torque_sensor < 12 ||
            ui8_startup_boost_fade_steps == 0)
        {
          ui8_startup_boost_fade_enable = 0;
          ui8_startup_boost_fade_steps = 0;
          ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST_WAIT_TO_RESTART;
        }
      break;

      // restart when user is not pressing the pedals AND/OR wheel speed = 0
      case BOOST_STATE_BOOST_WAIT_TO_RESTART:
        // wheel speed must be 0 as also torque sensor
        if((m_configuration_variables.ui8_startup_motor_power_boost_state & 1) == 0)
        {
          if(ui16_wheel_speed_x10 == 0 &&
              ui8_torque_sensor < 12)
          {
            ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST_DISABLED;
          }
        }
        // torque sensor must be 0
        if((m_configuration_variables.ui8_startup_motor_power_boost_state & 1) > 0)
        {
          if(ui8_torque_sensor < 12 ||
              ui8_pedal_cadence_RPM == 0)
          {
            ui8_m_startup_boost_state_machine = BOOST_STATE_BOOST_DISABLED;
          }
        }
      break;

      default:
      break;
    }
  }
} */