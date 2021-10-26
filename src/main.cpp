
#include "telegram_bot_api.h"
#include "temperature.h"
#include "temp_controller.h"
#include "wifi_setup.h"
#include "serial_setup.h"

#include "secrets.h"

// secrets.h should contain the following:
//
// // WiFi
//
// #define WIFI_SSID "SSID"
// #define WIFI_PASSWORD "PASSPHRASE"
//
// // Telegram
//
// #define TELEGRAM_BOT_TOKEN "TOKEN"
// #define TELEGRAM_CHAT_ID "CHAT_ID"

// GPIOs

#ifdef BOARD_TH10
    #define RELAY_GPIO 12
    #define ONEWIRE_GPIO 9
#else
    #define RELAY_GPIO 5
    #define ONEWIRE_GPIO 10
#endif

// Temperature

struct state_machine
{
    enum class state_type
    {
        IDLE,
        HEATING,
        HOT,
        LUBRICATING,
        COOLING,
        COOL
    };

    static const char* STATE_NAMES[];

    static constexpr float CONTROL_DELTA{1.0};

#if 1
    static constexpr float HOT_TEMP{102.0};
    static constexpr float COOL_TEMP{65.0};
    static constexpr float MAX_TEMP{120.0};
    
    static constexpr unsigned long LUBRICATION_TIME{ 1*3600*1000 };
    static constexpr unsigned long TIMEOUT{ 3*3600*1000 };
#else
    static constexpr float HOT_TEMP{40.0};
    static constexpr float COOL_TEMP{25.0};
    static constexpr float MAX_TEMP{75.0};
    
    static constexpr unsigned long LUBRICATION_TIME{ 30*1000 };
    static constexpr unsigned long TIMEOUT{ 120*1000 };
#endif

    state_machine(const char* token, uint8_t onewire_gpio, uint8_t relay_gpio) :
        temp{ onewire_gpio, 12 },
        tctrl{ temp, relay_gpio, CONTROL_DELTA },
        api{ token }
    {
    }

    void setup()
    {
        temp.setup();
        tctrl.setup();
        api.setup();

        notify(0, "Temperature monitor started: [%.2f C]", temp.temp());
    }

    void loop()
    {
        temp.loop();
        tctrl.loop();

        api.foreach_message([&](const telegramMessage& m)
        {
            if( m.chat_id != TELEGRAM_CHAT_ID )
            {
                return;
            }

            if( m.text == "/status")
            {
                status( m.message_id );
            }
            else if( m.text == "/start" )
            {
                start( m.message_id );
            }
            else if( m.text == "/stop" )
            {
                stop( m.message_id );
            }
            else if( m.text == "/lubricate" )
            {
                lubricate( m.message_id );
            }
            else if( m.text == "/cooldown" )
            {
                cooldown( m.message_id );
            }
        });

        update_temperature();
    }

    void start(int message_id)
    {
        notify(message_id, "Starting: [%.2f C]", temp.temp());
        state = state_type::HEATING;
        set_timeout(TIMEOUT);
        tctrl.start(HOT_TEMP);
    }

    void  lubricate(int message_id)
    {
        if( state != state_type::HOT )
        {
            notify(message_id, "Warning: state is not HOT [%.2f]", temp.temp());
        }

        notify(message_id, "Lubricating.");
        state = state_type::LUBRICATING;
        set_timeout(LUBRICATION_TIME);
        tctrl.start(HOT_TEMP);
    }

    void stop(int message_id)
    {
        if( message_id )
        {
            notify(message_id, "Stopping. [%.2f]", temp.temp());
        }

        state = state_type::IDLE;
        cancel_timeout();
        tctrl.stop();
    }

    void cooldown(int message_id)
    {
        if( state != state_type::LUBRICATING)
        {
            notify(message_id, "Warning: state is not LUBRICATING [%.2f]", temp.temp());
        }

        notify(message_id, "Cooling down.");
        state = state_type::COOLING;
        set_timeout(TIMEOUT);
        tctrl.start(COOL_TEMP);
    }

    void update_temperature()
    {
        if( state != state_type::IDLE && temp.temp() > MAX_TEMP )
        {
            stop(0);
            notify(0, "Max temperature exceeded, stopping. [%.2f]", temp.temp());
        }
        else if( state != state_type::LUBRICATING && timeout_expired() )
        {
            stop(0);
            notify(0, "Timout expired, stopping. [%.2f]", temp.temp());
        }
        else if( state == state_type::LUBRICATING && timeout_expired() )
        {
            cooldown(0);
            notify(0, "Finished lubricating, started cooling down. [%.2f]", temp.temp());
        }
        else if( state == state_type::HEATING && temp.temp() > HOT_TEMP )
        {
            state = state_type::HOT;
            set_timeout(TIMEOUT);
            notify(0, "Lubrication target temperature reached. [%.2f]", temp.temp());
        }
        else if( state == state_type::COOLING && temp.temp() < COOL_TEMP )
        {
            state = state_type::COOL;
            set_timeout(TIMEOUT);
            notify(0, "Cooldown temperature reached. [%.2f]", temp.temp());
        }
    }

    bool timeout_expired()
    {
        return has_event && millis() >= next_event;
    }

    void status(int message_id)
    {
        notify(message_id, "state=%s [%.2f]", STATE_NAMES[static_cast<int>(state)], temp.temp());
    }

    void notify(int message_id, const char* format, ...)
    {
        va_list args;
        va_start(args, format);

        api.sendvf(TELEGRAM_CHAT_ID, keyboard, message_id, format, args);

        va_end(args);
    }

    void cancel_timeout()
    {
        has_event = false;
    }

    void set_timeout(unsigned long ms)
    {
        has_event = true;
        next_event = millis() + ms;
    }

    state_type state{ state_type::IDLE };
    
    bool has_event{ false };
    unsigned long next_event;

    temperature temp;
    temp_controller tctrl;
    telegram_bot_api api;

    static const String keyboard;
};

const String state_machine::keyboard = R"JSON(
[
    [
        "/status"
    ],
    [
        "/start",
        "/stop"
    ],
    [
        "/lubricate",
        "/cooldown"
    ]
]
)JSON";

const char* state_machine::STATE_NAMES[] = { 
        "IDLE",
        "HEATING",
        "HOT",
        "LUBRICATING",
        "COOLING",
        "COOL"
    };

state_machine fsm{TELEGRAM_BOT_TOKEN, ONEWIRE_GPIO, RELAY_GPIO};

void setup()
{
    serial_setup();
    wifi_setup(WIFI_SSID, WIFI_PASSWORD);
    fsm.setup();

}

void loop()
{
    fsm.loop();
    delay(50);
}
