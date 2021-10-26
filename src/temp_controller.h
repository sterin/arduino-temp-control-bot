#pragma once

#include "temperature.h"
#include "serial_setup.h"

struct temp_controller
{
    temp_controller(temperature& temp, uint8_t relay, float delta) :
        relay{ relay },
        temp( temp ),
        delta{ delta }
    {
    }

    void setup()
    {
        pinMode(relay, OUTPUT);
        digitalWrite(relay, LOW);
    }

    void loop()
    {
        if( control )
        {
            if( temp.temp() > stop_temp )
            {
                set_heater(false);
            }
            else if ( temp.temp() < start_temp )
            {
                set_heater(true);
            }
        }
    }

    void start(float t)
    {
        serial_printf("Temperature Controller: start, target=%.2f, current=%.2f\n", t, temp.temp());

        set_target(t);
        control = true;
        loop();
    }

    void stop()
    {
        serial_printf("Temperature Controller: stop, current=%.2f\n", temp.temp());

        control = false;
        set_heater(false);
    }

    void set_heater(bool heat)
    {
        if( heat != heating )
        {
            serial_printf("Temperature Controller: heat=%s, current=%.2f\n", heat ? "ON" : "OFF", temp.temp());

            digitalWrite(relay, heat ? HIGH : LOW);
            heating = heat;
        }
    }

    void set_target(float target)
    {
        serial_printf("Temperature Controller: set new target=%.2f, current=%.2f\n", target, temp.temp());

        stop_temp = target;
        start_temp = target - delta;
    }

    bool control{ false };
    bool heating{ false };

    float start_temp;
    float stop_temp;

    const uint8_t relay;
    temperature& temp;
    const float delta;
};
