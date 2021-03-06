/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
  driver for PX4Flow optical flow sensor
 */

#include <AP_HAL/AP_HAL.h>
#include "AP_OpticalFlow_PX4Flow.h"
#include <AP_Math/edc.h>
#include <AP_AHRS/AP_AHRS.h>
#include <AP_HAL/I2CDevice.h>
#include <utility>
#include "OpticalFlow.h"
#include <stdio.h>

extern const AP_HAL::HAL& hal;

#define PX4FLOW_BASE_I2C_ADDR   0x42
#define PX4FLOW_INIT_RETRIES    10      // attempt to initialise the sensor up to 10 times at startup

// constructor
AP_OpticalFlow_PX4Flow::AP_OpticalFlow_PX4Flow(OpticalFlow &_frontend) :
    OpticalFlow_backend(_frontend)
{
}

/**************************************************************************************************************
*函数原型：AP_OpticalFlow_PX4Flow *AP_OpticalFlow_PX4Flow::detect(OpticalFlow &_frontend)
*函数功能：识别光流传感器
*修改日期：2019-2-18
*修改作者：cihang_uav
*备注信息：detect the device
****************************************************************************************************************/

AP_OpticalFlow_PX4Flow *AP_OpticalFlow_PX4Flow::detect(OpticalFlow &_frontend)
{
    AP_OpticalFlow_PX4Flow *sensor = new AP_OpticalFlow_PX4Flow(_frontend);
    if (!sensor)
    {
        return nullptr;
    }
    if (!sensor->setup_sensor())
    {
    	hal.console->printf("***^^^***1234567\r\n");
        delete sensor;
        return nullptr;
    }
    return sensor;
}

/**************************************************************************************************************
*函数原型：bool AP_OpticalFlow_PX4Flow::scan_buses(void)
*函数功能：查看总线
*修改日期：2019-2-18
*修改作者：cihang_uav
*备注信息： look for the sensor on different buses
****************************************************************************************************************/


bool AP_OpticalFlow_PX4Flow::scan_buses(void)
{
    bool success = false;
    uint8_t retry_attempt = 0;

    while (!success && retry_attempt < PX4FLOW_INIT_RETRIES)
    {
    	hal.console->printf("***^^^***000\r\n");
        for (uint8_t bus = 0; bus < 4; bus++)
        {
    #ifdef HAL_OPTFLOW_PX4FLOW_I2C_BUS
            // only one bus from HAL
            if (bus != HAL_OPTFLOW_PX4FLOW_I2C_BUS)
            {
                continue;
            }
    #endif
            AP_HAL::OwnPtr<AP_HAL::Device> tdev = hal.i2c_mgr->get_device(bus, PX4FLOW_BASE_I2C_ADDR + get_address());
            if (!tdev)
            {
                continue;
            }
            if (!tdev->get_semaphore()->take(HAL_SEMAPHORE_BLOCK_FOREVER))
            {
                continue;
            }
            struct i2c_integral_frame frame;
            success = tdev->read_registers(REG_INTEGRAL_FRAME, (uint8_t *)&frame, sizeof(frame));
            tdev->get_semaphore()->give();
            if (success)
            {
                printf("Found PX4Flow on bus %u\n", bus);
                dev = std::move(tdev);
                break;
            }
        }
        retry_attempt++;
        if (!success) {
            hal.scheduler->delay(10);
        }
    }
    hal.console->printf("success=%d\r\n",success);
    return success;
}



// setup the device
bool AP_OpticalFlow_PX4Flow::setup_sensor(void)
{
    if (!scan_buses())
    {
    	hal.console->printf("***^^^***12345678\r\n");
        return false;
    }
    // read at 10Hz
    dev->register_periodic_callback(100000, FUNCTOR_BIND_MEMBER(&AP_OpticalFlow_PX4Flow::timer, void));
    return true;
}


// update - read latest values from sensor and fill in x,y and totals.
void AP_OpticalFlow_PX4Flow::update(void)
{
}

// timer to read sensor
void AP_OpticalFlow_PX4Flow::timer(void)
{
    struct i2c_integral_frame frame;
    if (!dev->read_registers(REG_INTEGRAL_FRAME, (uint8_t *)&frame, sizeof(frame))) {
        return;
    }
    struct OpticalFlow::OpticalFlow_state state {};
    state.device_id = get_address();
//    hal.console->printf("state.device_id=%d\r\n",state.device_id);
    if (frame.integration_timespan > 0)
    {
        const Vector2f flowScaler = _flowScaler();
//        hal.console->printf("state.device_id=%d\r\n",state.device_id);
//        hal.console->printf("state.device_id=%d\r\n",state.device_id);
        float flowScaleFactorX = 1.0f + 0.001f * flowScaler.x;
        float flowScaleFactorY = 1.0f + 0.001f * flowScaler.y;
        float integralToRate = 1.0e6 / frame.integration_timespan;
        
        state.surface_quality = frame.qual;
        state.flowRate = Vector2f(frame.pixel_flow_x_integral * flowScaleFactorX,
                                  frame.pixel_flow_y_integral * flowScaleFactorY) * 1.0e-4 * integralToRate;
        state.bodyRate = Vector2f(frame.gyro_x_rate_integral, frame.gyro_y_rate_integral) * 1.0e-4 * integralToRate;
        
//        hal.console->printf("state.flowRate=%d\r\n",state.flowRate);
//        hal.console->printf("state.bodyRate=%d\r\n",state.bodyRate);

        _applyYaw(state.flowRate);
        _applyYaw(state.bodyRate);
    }
//    hal.uartC->printf("^^^^^^^^^^^^^\r\n");
//    hal.uartC->printf("state.flowRate.x=%d\r\n",state.flowRate.x);
//    hal.uartC->printf("state.flowRate.y=%d\r\n",state.flowRate.y);
//    hal.uartC->printf("state.bodyRate.x=%d\r\n",state.bodyRate.x);
//    hal.uartC->printf("state.bodyRate.y=%d\r\n",state.bodyRate.y);
//    hal.uartC->printf("**************\r\n");
    _update_frontend(state);
}
