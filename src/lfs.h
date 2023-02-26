#pragma once

#include <LittleFS.h>

struct littlefs
{
    littlefs()
    {
        LittleFS.begin();
    }

    ~littlefs()
    {
        LittleFS.end();
    }

    template<typename T>
    T load(const char* fname)
    {
        if( auto f = LittleFS.open(fname, "r") )
        {
            T data;

            if( f.readBytes(reinterpret_cast<char*>(&data), sizeof(T)) == sizeof(T) )
            {
                return data;
            }
        }

        return {};
    }

    template<typename T>
    void save(const char* fname, const T& data)
    {
        if( auto f = LittleFS.open(fname, "w") )
        {
            f.write(reinterpret_cast<const char*>(&data), sizeof(T));
        }
    }
};

