/* hyx_logger.h
 *
 * Copyright 2023 Michael Pollak. All rights reserved.
 */

#ifndef HYX_LOGGER_H
#define HYX_LOGGER_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string_view>

namespace hyx
{
    class logger
    {
    private:
        static std::ofstream log_stream;

        struct trace_t{};
        struct debug_t{};
        struct info_t{};
        struct warning_t{};
        struct error_t{};
        struct fatal_t{};

        class header
        {
        private:
            static const header &get_date(const header &a);

            void create_header(const std::string_view &level, const std::source_location &sl);

        public:
            /*implicit*/ header([[maybe_unused]] const trace_t &caller, const std::source_location &sl = std::source_location::current());

            /*implicit*/ header([[maybe_unused]] const debug_t &caller, const std::source_location &sl = std::source_location::current());

            /*implicit*/ header([[maybe_unused]] const info_t &caller, const std::source_location &sl = std::source_location::current());

            /*implicit*/ header([[maybe_unused]] const warning_t &caller, const std::source_location &sl = std::source_location::current());

            /*implicit*/ header([[maybe_unused]] const error_t &caller, const std::source_location &sl = std::source_location::current());

            /*implicit*/ header([[maybe_unused]] const fatal_t &caller, const std::source_location &sl = std::source_location::current());
        };

        logger(){};
        ~logger(){};

    public:
        trace_t trace;
        debug_t debug;
        info_t info;
        warning_t warning;
        error_t error;
        fatal_t fatal;

        logger(const logger &l) = delete;
        logger(logger &l) = delete;
        logger &operator=(const logger &l) = delete;
        logger &operator=(logger &&l) = delete;

        static logger &get_instance()
        {
            static logger instance;
            return instance;
        }

        void open(const std::filesystem::path &log_path)
        {
            log_stream.rdbuf()->pubsetbuf(0, 0); // remove buffering
            log_stream.open(log_path, std::ios::app);
        }

        void close()
        {
            log_stream.close();
        }

        bool is_open()
        {
            return log_stream.is_open();
        }

        template<class T>
        friend const header &operator<<(const header &a, const T &msg)
        {
            log_stream << msg;
            return a;
        }
        friend const header &operator<<(const header &a, const header &(*func)(const header &))
        {
            return func(a);
        }
    };
} // namespace hyx

#endif // !HYX_LOGGER_H
