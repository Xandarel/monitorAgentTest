#pragma once

#include <string>

void log_line(const std::string& text);
void install_shutdown_handler();
void signal_shutdown_complete();

// true, если интервал истёк и работу нужно продолжать.
// false, если запрошена остановка.
bool wait_for_stop(int milliseconds);
