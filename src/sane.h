// <sane.h> -*- C++ -*-
// Copyright (C) 2023-2024 Michael Pollak

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#ifndef HYX_SANE_H
#define HYX_SANE_H

#include <memory>
#include <ranges>
#include <sane/sane.h>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <string_view>

namespace hyx {
    /**
     * @brief A look up table for sane unit strings.
     */
    static const std::unordered_map<SANE_Unit, SANE_String_Const> sane_units_lut{
        {SANE_UNIT_NONE, ""},
        {SANE_UNIT_PIXEL, "px"},
        {SANE_UNIT_BIT, "bit"},
        {SANE_UNIT_MM, "mm"},
        {SANE_UNIT_DPI, "dpi"},
        {SANE_UNIT_PERCENT, "%"},
        {SANE_UNIT_MICROSECOND, "μs"}};

    class sane_init;
    class sane_device;

    /**
     * @brief Wraps sane_init and sane_exit.
     * Acts as an owning factory for sane devices. When an object of this class is destroyed, so are all objects created by this class (like a nesting doll).
     */
    class sane_init {
    public:
        /**
         * @brief Calls curl_global_cleanup only once (as if by std::call_once).
         */
        ~sane_init() noexcept;

        // singletons should not copy
        sane_init(const sane_init& si) = delete;
        sane_init(sane_init& si) = delete;
        sane_init& operator=(const sane_init& si) = delete;
        sane_init& operator=(sane_init&& si) = delete;

        [[nodiscard]] static sane_init& get_instance(SANE_Auth_Callback authorize = nullptr);

        /**
         * @brief Finds the first sane device labeled as a "scanner."
         * @return A scanner sane device or nullptr.
         */
        [[nodiscard]] const SANE_Device* find_scanner();

        /**
         * @brief Opens and returns a pointer to a sane device for scanning.
         * @param device The sane device to open (nullptr will open the first device).
         * @return The opened device pointer.
         */
        [[nodiscard]] sane_device* open_device(const SANE_Device* device = nullptr);

        /**
         * @brief Returns if the sane backend is in a good state.
         * @return If the sane backend is in a good state.
         */
        [[nodiscard]] bool is_good() noexcept;

        /**
         * @brief Returns a string of the sane backend's status.
         * @return A string of the sane backend's status.
         */
        [[nodiscard]] std::string_view get_status() noexcept;

        /**
         * @brief Returns the version of the sane backend.
         * @return The sane backend version as a string.
         */
         [[nodiscard]] SANE_Int get_version() noexcept;

    private:
        /**
         * @brief Calls sane's sane_init.
         * @param version_code a variable to hold sane's backend version.
         * @param authorize an auth function to be called if needed.
         */
        explicit sane_init(SANE_Int* version_code = nullptr, SANE_Auth_Callback authorize = nullptr);

        [[nodiscard]] std::span<const SANE_Device*> get_devices(SANE_Bool is_local_only = false);

        SANE_Int version;
        SANE_Status status;
        std::vector<std::unique_ptr<sane_device>> devices_open;
    };

    class sane_device {
    public:
        struct option;
        struct bool_option;
        struct int_option;
        struct fixed_option;
        struct string_option;

        ~sane_device();

        // singleton-like should not copy
        sane_device(const sane_device& si) = delete;
        sane_device(sane_device& si) = delete;
        sane_device& operator=(const sane_device& si) = delete;
        sane_device& operator=(sane_device&& si) = delete;

        /**
         * @brief Returns a list of valid device options.
         * @return A list of valid device options.
         */
        [[nodiscard]] const std::vector<option*> get_options();

        SANE_Int set_option(const bool_option* bop, SANE_Bool new_value);

        SANE_Int set_option(const fixed_option* fop, SANE_Fixed new_value);

        SANE_Int set_option(const int_option* iop, SANE_Int new_value);

        SANE_Int set_option(const string_option* sop, SANE_String new_value);

        /**
         * @brief Returns the scan parameters.
         * @return The scan parameters.
         */
        SANE_Parameters get_parameters();

        /**
         * @brief Starts a scan and returns the current status
         */
        bool start();

        /**
         * @brief Reads image data from the scanner into data.
         * @param data The buffer to hold image data.
         * @param max_length The length of the buffer.
         * @return If the read is EOF.
         */
        bool read(SANE_Byte* data, SANE_Int max_length);

        /**
         * @brief Cancels the current scan.
         */
        void cancel() noexcept;

        /**
         * @brief Returns if the device's status is good.
         * @return If the device's status is good.
         */
        [[nodiscard]] bool is_good() noexcept;

    public:
        SANE_String_Const name;

    private:
        // hide normal constructor so only sane_init can create new ones
        explicit sane_device(SANE_String_Const device_name);

        SANE_Int set_option_helper(const option* opt, void* new_value);

        SANE_Status status;
        SANE_Handle handle;
        std::vector<std::unique_ptr<option>> options;

        // let sane_init to call private constructor
        friend sane_init;
    };

    struct sane_device::option {
        SANE_String_Const name;
        SANE_String_Const title;
        SANE_String_Const desc;

        virtual ~option() {}

        constexpr SANE_Bool is_soft_selectable()
        {
            return (this->capabilities & SANE_CAP_SOFT_SELECT);
        }
        constexpr SANE_Bool is_hard_selectable()
        {
            return (this->capabilities & SANE_CAP_HARD_SELECT);
        }
        constexpr SANE_Bool is_soft_detectable()
        {
            return (this->capabilities & SANE_CAP_SOFT_DETECT);
        }
        constexpr SANE_Bool is_emulated()
        {
            return (this->capabilities & SANE_CAP_EMULATED);
        }
        constexpr SANE_Bool is_automatic()
        {
            return (this->capabilities & SANE_CAP_AUTOMATIC);
        }
        constexpr SANE_Bool is_inactve()
        {
            return (this->capabilities & SANE_CAP_INACTIVE);
        }
        constexpr SANE_Bool is_advanced()
        {
            return (this->capabilities & SANE_CAP_ADVANCED);
        }

    protected:
        // this is an abstract base class; hide contructor
        explicit option(const SANE_Option_Descriptor* opt, SANE_Int idx)
            : name(opt->name),
              title(opt->title),
              desc(opt->desc),
              capabilities(opt->cap),
              index(idx) {}

    private:
        // only accessed through is_* functions
        SANE_Int capabilities;

    public:
        SANE_Int index;
    };

    struct sane_device::bool_option final : sane_device::option {
        SANE_String_Const units;
        SANE_Bool value;

    private:
        explicit bool_option(const SANE_Option_Descriptor* opt, SANE_Int idx)
            : option(opt, idx),
              units(sane_units_lut.at(opt->unit)) {}

        // let sane_device to call private constructor
        friend sane_device;
    };

    struct sane_device::int_option final : sane_device::option {
        SANE_String_Const units;
        SANE_Int value;
        const SANE_Range* legal_values;

    private:
        explicit int_option(const SANE_Option_Descriptor* opt, SANE_Int idx)
            : option(opt, idx),
              units(sane_units_lut.at(opt->unit))
        {
            if (opt->constraint_type == SANE_CONSTRAINT_RANGE) {
                this->legal_values = opt->constraint.range;
            }
        }

        // let sane_device to call private constructor
        friend sane_device;
    };

    struct sane_device::fixed_option final : sane_device::option {
        SANE_String_Const units;
        SANE_Fixed value;
        const SANE_Range* legal_values;

    private:
        explicit fixed_option(const SANE_Option_Descriptor* opt, SANE_Int idx)
            : option(opt, idx),
              units(sane_units_lut.at(opt->unit))
        {
            if (opt->constraint_type == SANE_CONSTRAINT_RANGE) {
                this->legal_values = opt->constraint.range;
            }
        }

        // let sane_device to call private constructor
        friend sane_device;
    };

    struct sane_device::string_option final : sane_device::option {
        SANE_String value;
        std::span<const SANE_String_Const> legal_values;

        ~string_option()
        {
            delete[] value;
        }

    private:
        explicit string_option(const SANE_Option_Descriptor* opt, SANE_Int idx)
            : option(opt, idx),
              value(new SANE_Char[opt->size])
        {
            if (opt->constraint_type == SANE_CONSTRAINT_STRING_LIST) {
                std::size_t nopts{std::distance(opt->constraint.string_list, std::ranges::find(opt->constraint.string_list, opt->constraint.string_list + INTMAX_MAX, nullptr))};
                this->legal_values = std::span<const SANE_String_Const>(opt->constraint.string_list, nopts);
            }
        }

        // let sane_device to call private constructor
        friend sane_device;
    };

} // namespace hyx
#endif // !HYX_SANE_H
