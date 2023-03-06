/* hyx_parser.h
 *
 * Copyright 2023 Michael Pollak. All rights reserved.
 */

#ifndef HYX_PARSER_H
#define HYX_PARSER_H

#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hyx
{
    class parser
    {
    private:
        std::smatch matches;

        int get_month_number(std::string_view month);

    public:
        parser() = default;

        std::string parse_total(const std::string &text, const std::string &default_return);

        std::string parse_date(const std::string &text, const std::string &default_return);

        std::string parse_store(const std::string &text, const std::string &default_return);

        std::string parse_transaction(const std::string &text, const std::string &default_return);
    };
} // namespace hyx

#endif // !HYX_PARSER_H