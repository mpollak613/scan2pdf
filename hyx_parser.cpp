/* hyx_parser.cpp
 *
 * Copyright 2023 Michael Pollak. All rights reserved.
 */

#include "hyx_parser.h"

int hyx::parser::get_month_number(std::string_view month)
{
    static const std::unordered_map<std::string_view, int> months {
        { "Jan", 1 },
        { "Feb", 2 },
        { "Mar", 3 },
        { "Apr", 4 },
        { "May", 5 },
        { "Jun", 6 },
        { "Jul", 7 },
        { "Aug", 8 },
        { "Sep", 9 },
        { "Oct", 10 },
        { "Nov", 11 },
        { "Dec", 12 }
    };

    if (months.contains(month))
    {
        return months.at(month);
    }

    return -1;
}

std::string hyx::parser::parse_total(const std::string &text, const std::string &default_return)
{
    const std::regex reg_total(R"=((?:(?:\btotal\b(?:\s\bsale\b)?)|(?:\bbalance\sdue\b)|(?:\bpurchase\b)|(?:\bamount\b))\s*\:?\s*\$?\s*(\d+\.?\d*))=", std::regex_constants::icase);

    if (std::regex_search(text, this->matches, reg_total)) {
        return this->matches.str(1);
    }

    return default_return;
}

std::string hyx::parser::parse_date(const std::string &text, const std::string &default_return)
{
    const std::regex reg_date(R"=((?:(?:(?:0?[13578]|1[02])(\/|-|\.)31)\1|(?:(?:0?[1,3-9]|1[0-2])(\/|-|\.)(?:29|30)\2)|(?:(?:Jan(?:uary)?|Mar(?:ch)?|May|Jul(?:y)?|Aug(?:ust)?|Oct(?:ober)?|Dec(?:ember)?)(?:\ )31(?:,?\ ))|(?:(?:Jan(?:uary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)(?:\ )(?:29|30)(?:,?\ )))(?:(?:1[6-9]|[2-9]\d)?\d{2})|(?:(?:0?2(\/|-|\.)29\3|Feb(?:ruary)?(?:\ )29(?:,?\ ))(?:(?:(?:1[6-9]|[2-9]\d)?(?:0[48]|[2468][048]|[13579][26])|(?:(?:16|[2468][048]|[3579][26])00))))|(?:(?:(?:0?[1-9])|(?:1[0-2]))(\/|-|\.)(?:0?[1-9]|1\d|2[0-8])\4|(?:Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)(?:\ )(?:0?[1-9]|1\d|2[0-8])(?:,?\ ))(?:(?:1[6-9]|[2-9]\d)?\d{2}))=");

    if (std::regex_search(text, this->matches, reg_date)) {
        int year, month, day;
        char cmonth[10], cyear[10];

        std::string delim = *std::find_if(this->matches.begin()+1, this->matches.end(), [] (const std::string &match) { return !match.empty(); });

        // month/day/year
        if (std::sscanf(this->matches.str(0).c_str(), std::string("%d" + delim + "%d" + delim + "%d").c_str(), &month, &day, &year) == 3) {
            // ignore
        }
        // month day, year || month day year
        else if (std::sscanf(this->matches.str(0).c_str(), std::string("%s %d%*[, ] %d").c_str(), cmonth, &day, &year) == 3) {
            month = get_month_number(cmonth);

            if (month == -1)
            {
                return "";
            }
        }

        // TODO: should find a better way to fix the year being two digit
        if (year < 100) {
            year += 2000;
        }

        // we are reusing cmonth var here to get the final formatted date
        std::sprintf(cyear, "%d-%.02d-%.02d", year, month, day);

        return std::string(cyear);
    }

    return default_return;
}

std::string hyx::parser::parse_store(const std::string &text, const std::string &default_return)
{
    const std::regex reg_store(R"=((?:st(?:ore)?)\s*[\#\:]?\s*[\#\:]?\s*(\d+))=", std::regex_constants::icase);

    if (std::regex_search(text, this->matches, reg_store)) {
        return this->matches.str(1);
    }

    return default_return;
}

std::string hyx::parser::parse_transaction(const std::string &text, const std::string &default_return)
{
    const std::regex reg_transaction(R"=((?:(?:tr(?:n|(?:(?:an)(?:saction)?))?(?:\s*number)?)|(?:invoice))\s*[\:\#]+\s*[\:\#]?\s*([a-z\d\-]+))=", std::regex_constants::icase);

    if (std::regex_search(text, this->matches, reg_transaction)) {
        return this->matches.str(1);
    }

    return default_return;
}

