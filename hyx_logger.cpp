/* hyx_logger.cpp
 *
 * Copyright 2023 Michael Pollak. All rights reserved.
 */

#include "hyx_logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string_view>

std::ofstream hyx::logger::log_stream;

inline const hyx::logger::header &hyx::logger::header::get_date(const header &a)
{
    std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    return (a << std::put_time(std::gmtime(&time), "%h %d %T %Y"));
}

inline void hyx::logger::header::create_header(const std::string_view &level, const std::source_location &sl)
{
    *this << get_date
            << "[" << std::setw(10) << std::left << level << "]: " << std::setw(0) << std::right
            << sl.file_name() << "@" << sl.line() << ": ";
}

hyx::logger::header::header([[maybe_unused]] const trace_t &caller, const std::source_location &sl)
{
    create_header("TRACE", sl);
}

hyx::logger::header::header([[maybe_unused]] const debug_t &caller, const std::source_location &sl)
{
    create_header("DEBUG", sl);
}

hyx::logger::header::header([[maybe_unused]] const info_t &caller, const std::source_location &sl)
{
    create_header("INFO", sl);
}

hyx::logger::header::header([[maybe_unused]] const warning_t &caller, const std::source_location &sl)
{
    create_header("WARNING", sl);
}

hyx::logger::header::header([[maybe_unused]] const error_t &caller, const std::source_location &sl)
{
    create_header("ERROR", sl);
}

hyx::logger::header::header([[maybe_unused]] const fatal_t &caller, const std::source_location &sl)
{
    create_header("FATAL", sl);
}
