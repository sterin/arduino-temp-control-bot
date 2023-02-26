#pragma once

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

#include "serial_setup.h"

const unsigned long BOT_MTBS = 1000; // mean time between scan messages

struct telegram_bot_api
{
    telegram_bot_api(const char* token) :
        token{ token }
    {
    }
        
    void setup()
    {
        secured_client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org

        serial_printf("Telegram Bot API: Retrieving time ");
        
        configTime(0, 0, "pool.ntp.org");

        time_t now = time(nullptr);
        while (now < 24 * 3600)
        {
            serial_printf(".");
            delay(500);
            now = time(nullptr);
        }
    
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);

        serial_printf(" %s\n", asctime(&timeinfo));
    }

    bool send(const String& chat_id, const String& text, const String& keyboard={}, int reply_to_message_id=0) 
    {
        DynamicJsonDocument payload(bot.maxMessageLength);

        payload["chat_id"] = chat_id;
        payload["text"] = text;

        if( reply_to_message_id )
        {
            payload["reply_to_message_id"] = reply_to_message_id;
        }

        if( !keyboard.isEmpty() )
        {
            JsonObject replyMarkup = payload.createNestedObject("reply_markup");
            replyMarkup["keyboard"] = serialized(keyboard);
        }

        String out;
        serializeJson(payload, out);

        return bot.sendPostMessage(payload.as<JsonObject>(), 0);
    }    

    bool sendf(const String& chat_id, const String& keyboard, int reply_to_message_id, const char* format, ...)
    {
        va_list args;
        va_start(args, format);

        bool rc = sendvf(chat_id, keyboard, reply_to_message_id, format, args);

        va_end(args);

        return rc;
    }

    bool sendvf(const String& chat_id, const String& keyboard, int reply_to_message_id, const char* format, va_list args)
    {
        char buf[256];
        vsnprintf(buf, 256, format, args);
        return send(chat_id, buf, keyboard, reply_to_message_id);
    }

    void confirm()
    {
        bot.getUpdates(bot.last_message_received + 1);
    }

    template<typename F>
    void foreach_message(F f)
    {
        if (millis() - bot_lasttime > BOT_MTBS)
        {
            int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

            while (numNewMessages)
            {
                for(int i=0; i<numNewMessages; i++)
                {
                    f(bot.messages[i]);
                }
                numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            }

            bot_lasttime = millis();
        }
    }

    const char* token;

    X509List cert{TELEGRAM_CERTIFICATE_ROOT};
    WiFiClientSecure secured_client;
    UniversalTelegramBot bot{token, secured_client};
    unsigned long bot_lasttime;
};
