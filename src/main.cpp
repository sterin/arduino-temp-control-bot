#include "telegram_bot_api.h"
#include "temperature.h"
#include "temp_controller.h"
#include "serial_setup.h"
#include "lfs.h"

#include <esp_wifi.h>
#include <WiFiManager.h>

#include <memory>

#define RELAY_GPIO 19
#define ONEWIRE_GPIO 6

void erase_wifi_config()
{
    wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wicfg);

    wifi_config_t wcfg{0};
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
}

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

    static constexpr float DEFAULT_HOT_TEMP{102.0};
    static constexpr float DEFAULT_COOL_TEMP{72.0};
    static constexpr float DEFAULT_MAX_TEMP{120.0};
    
    static constexpr unsigned long DEFAULT_LUBRICATION_TIME{ 1*3600*1000 };
    static constexpr unsigned long DEFAULT_TIMEOUT{ 3*3600*1000 };

    static float HOT_TEMP;
    static float COOL_TEMP;
    static float MAX_TEMP;
    
    static unsigned long LUBRICATION_TIME;
    static unsigned long TIMEOUT;

    state_machine(const char* token, const char* chat_id, uint8_t onewire_gpio, uint8_t relay_gpio) :
        temp{ onewire_gpio, 12 },
        tctrl{ temp, relay_gpio, CONTROL_DELTA },
        api{ token },
        chat_id{ chat_id }
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
            if( m.chat_id != chat_id )
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
            else if( m.text == "/reconfigure")
            {
                serial_printf("Reconfigure!");
                api.confirm();
                
                delay(3000);

                erase_wifi_config();

                delay(1000);
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

        notify(message_id, "Lubricating: [%.2f C]", temp.temp());
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

        api.sendvf(chat_id, keyboard, message_id, format, args);

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
    const char* chat_id;

    static const String keyboard;
};

float state_machine::HOT_TEMP{state_machine::DEFAULT_HOT_TEMP};
float state_machine::COOL_TEMP{state_machine::DEFAULT_COOL_TEMP};
float state_machine::MAX_TEMP{state_machine::DEFAULT_MAX_TEMP};

unsigned long state_machine::LUBRICATION_TIME{state_machine::DEFAULT_LUBRICATION_TIME};
unsigned long state_machine::TIMEOUT{state_machine::DEFAULT_TIMEOUT};

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

std::unique_ptr<state_machine> fsm;

const uint32_t MAGIC = 0x01234567;

struct configuration
{
    uint32_t magic;
    uint16_t version;

    char token[64];
    char chat_id[16];

    float max_temp;
    float hot_temp;
    float cool_temp;

    float lubrication_time;
    float timeout;
};

template<int N>
WiFiManagerParameter add_param(WiFiManager& wm, const char* id, const char* label, const char (&val)[N])
{
    WiFiManagerParameter param(id, label, val, N-1);
    wm.addParameter(&param);
    return param;
}

WiFiManagerParameter add_param(WiFiManager& wm, const char* id, const char* label, float val)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", val);

    return add_param(wm, id, label, buf);
}

template<int N>
void param_value(WiFiManagerParameter& param, char (&cur_val)[N], bool& changed)
{
    const char* val = param.getValue();

    if( strncmp(val, cur_val, N) != 0 )
    {
        strncpy(cur_val, val, N);
        changed = true;
    }
}

void param_value(WiFiManagerParameter& param, float& cur_val, bool& changed)
{
    const char* sval = param.getValue();
    float val;
    
    if( sscanf(sval, "%f", &val) == 1 && val != cur_val )
    {
        cur_val = val;
        changed = true;
    }
}

configuration conf;

void setup()
{
    serial_setup();

    littlefs lfs;
    bool should_save = false;

    conf = lfs.load<configuration>("/config.data");

    if( conf.magic != MAGIC )
    {
        conf = configuration{
            .magic=MAGIC,
            .version=0,
            .token{0},
            .chat_id{0},
            .max_temp=120,
            .hot_temp=102,
            .cool_temp=72,
            .lubrication_time=1,
            .timeout=3
        };

        should_save = true;
    }

    WiFiManager wifiManager;

    auto token = add_param(wifiManager, "token", "Telegram Bot Token", conf.token);
    auto chat_id = add_param(wifiManager, "chat_id", "Telegram Chat ID", conf.chat_id);
    auto max_temp = add_param(wifiManager, "max_temp", "Max  Temperature", conf.max_temp);
    auto hot_temp = add_param(wifiManager, "hot_temp", "Hot  Temperature", conf.hot_temp);
    auto cool_temp = add_param(wifiManager, "cool_temp", "Cool  Temperature", conf.cool_temp);
    auto lubrication_time = add_param(wifiManager, "lubrication_time", "Lubrication Timeout", conf.lubrication_time);
    auto timeout = add_param(wifiManager, "timeout", "Timeout", conf.timeout);

    wifiManager.setSaveConfigCallback([&]()
    {
        param_value(token, conf.token, should_save);
        param_value(chat_id, conf.chat_id, should_save);
        param_value(max_temp, conf.max_temp, should_save);
        param_value(hot_temp, conf.hot_temp, should_save);
        param_value(cool_temp, conf.cool_temp, should_save);
        param_value(lubrication_time, conf.lubrication_time, should_save);
        param_value(timeout, conf.timeout, should_save);
    });

    if( !wifiManager.autoConnect("TEMP_CTRL") )
    {
        Serial.println("ERROR: failed to connect and hit timeout");
        delay(3000);
        ESP.restart();
    }

    if( should_save )
    {
      lfs.save("/config.data", conf);
    }

    Serial.println("Web Server started:" + WiFi.localIP().toString());

    state_machine::MAX_TEMP = conf.max_temp;
    state_machine::HOT_TEMP = conf.hot_temp;
    state_machine::COOL_TEMP = conf.cool_temp;

    state_machine::LUBRICATION_TIME = conf.lubrication_time * 3600 * 1000;
    state_machine::TIMEOUT = conf.timeout * 3600 * 1000;

    fsm = std::make_unique<state_machine>(conf.token, conf.chat_id, ONEWIRE_GPIO, RELAY_GPIO);
    fsm->setup();
}

void loop()
{
    fsm->loop();
    delay(50);
}
