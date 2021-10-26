#pragma once

void serial_setup()
{
  Serial.begin(115200);
  Serial.println();
}

void serial_printf(const char* format, ...)
{
  va_list args;
  va_start(args, format);

  char buf[256];
  vsnprintf(buf, 256, format, args);
  Serial.print(buf);

  va_end(args);
}

// #ifndef DEBUG
// #define serial_printf(...)
// #endif
