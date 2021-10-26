#pragma once

#include <OneWire.h>
#include <DallasTemperature.h>

#include "serial_setup.h"

struct temperature
{
  temperature(uint8_t gpio, int resolution) :
    gpio{ gpio },
    resolution{ resolution }
  {
  }

  void setup()
  {
    sensors.begin();
    sensors.getAddress(tempDeviceAddress, 0);
    sensors.setResolution(tempDeviceAddress, resolution);
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    
    prev_time = millis();
    delay(sampling_delay+1);
    loop();
  }

  void loop()
  {
    if( millis() > prev_time + sampling_delay )
    {
      prev = sensors.getTempCByIndex(0);
      serial_printf("Temperature sensor: sample - %.2f\n", prev);
      sensors.requestTemperatures();    
      prev_time = millis();
    }
  }

  float temp()
  {
    return prev;
  }

  const uint8_t gpio;
  const int resolution;
  const unsigned long sampling_delay{ 750 / (1 << (12 - resolution)) };

  OneWire oneWire{gpio};
  DallasTemperature sensors{&oneWire};
  DeviceAddress tempDeviceAddress;

  float prev{-1e10};
  unsigned long prev_time;
};
