#pragma once

#include <ESP8266WiFi.h>
#include "serial_setup.h"

void wifi_setup(const char* ssid, const char* password)
{
    serial_printf("WiFi: Connecting to SSID %s ", ssid);

    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED)
    {
        serial_printf(".");
        delay(500);
    }

    serial_printf(" connected. IP address: %s\n", WiFi.localIP().toString().c_str());
}
