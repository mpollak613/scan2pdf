/**
 * @file hyx_sane.cpp
 * @copyright
 * Copyright 2023 Michael Pollak.
 * All rights reserved.
 */

#include <algorithm>
#include <memory>
#include <ranges>
#include <sane/sane.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sane.h"

std::span<const SANE_Device*> hyx::sane_init::get_devices(SANE_Bool is_local_only)
{
    const SANE_Device** devices;

    if (this->status = sane_get_devices(&devices, is_local_only); !this->is_good()) {
        throw std::runtime_error("Could not get SANE devices: " + std::string(sane_strstatus(this->status)));
    }

    std::size_t ndevices = std::distance(devices, std::ranges::find(devices, devices + INTMAX_MAX, nullptr));

    return std::span<const SANE_Device*>(devices, ndevices);
}

hyx::sane_init::sane_init(SANE_Int* version_code, SANE_Auth_Callback authorize)
    : status(::sane_init(version_code, authorize))
{
    if (!this->is_good() || !version_code) {
        throw std::runtime_error(std::string("Could not initialize SANE: ").append(this->get_status()));
    }

    this->version = *version_code;
}

hyx::sane_init::~sane_init() noexcept
{
    // ensure devices close *before* calling exit
    for (auto& dev : this->devices_open) {
        dev.reset();
    }

    sane_exit();
}

hyx::sane_init& hyx::sane_init::get_instance(SANE_Auth_Callback authorize)
{
    static SANE_Int version_code;
    static sane_init instance{&version_code, authorize};
    return instance;
}

const SANE_Device* hyx::sane_init::find_scanner()
{
    const SANE_Device* scanner{*std::ranges::find_if(this->get_devices(), [](const SANE_Device* dev) {
        return std::string_view(dev->type).ends_with("scanner");
    })};

    return scanner;
}

hyx::sane_device* hyx::sane_init::open_device(const SANE_Device* device)
{
    // try to find a device if one is not given
    if (!device && !(device = this->get_devices().front())) {
        // don't try to open a device that doesn't exist
        throw std::runtime_error("Could not find a SANE device to open");
    }

    this->devices_open.push_back(std::unique_ptr<sane_device>(new sane_device(device->name)));
    return this->devices_open.back().get();
}

bool hyx::sane_init::is_good() noexcept
{
    return (this->status == SANE_STATUS_GOOD);
}

std::string_view hyx::sane_init::get_status() noexcept
{
    return sane_strstatus(this->status);
}

SANE_Int hyx::sane_init::get_version() noexcept
{
    return this->version;
}


hyx::sane_device::sane_device(SANE_String_Const device_name)
    : name(device_name)
{
    if (this->status = sane_open(this->name, &this->handle); !this->is_good()) {
        throw std::runtime_error(std::string("Could not open SANE device \'").append(this->name).append("\': ").append(sane_strstatus(this->status)));
    }
}

SANE_Int hyx::sane_device::set_option_helper(const option* opt, void* new_value)
{
    SANE_Int info;

    this->status = sane_control_option(this->handle, opt->index, SANE_ACTION_SET_VALUE, new_value, &info);

    return info;
}

hyx::sane_device::~sane_device()
{
    sane_close(this->handle);
}

const std::vector<hyx::sane_device::option*> hyx::sane_device::get_options()
{
    SANE_Int noptions;
    sane_control_option(this->handle, 0, SANE_ACTION_GET_VALUE, &noptions, nullptr);

    this->options.clear();
    this->options.reserve(noptions);

    for (SANE_Int i : std::ranges::iota_view(1, noptions)) {
        const SANE_Option_Descriptor* opt{sane_get_option_descriptor(this->handle, i)};

        if (opt->type == SANE_TYPE_BOOL) {
            std::unique_ptr<hyx::sane_device::bool_option> bop{new hyx::sane_device::bool_option(opt, i)};

            if (!bop->is_inactve()) {
                this->status = sane_control_option(this->handle, i, SANE_ACTION_GET_VALUE, &bop->value, nullptr);
            }

            this->options.push_back(std::move(bop));
        }
        else if (opt->type == SANE_TYPE_FIXED) {
            std::unique_ptr<hyx::sane_device::fixed_option> fop{new hyx::sane_device::fixed_option(opt, i)};

            if (!fop->is_inactve()) {
                this->status = sane_control_option(this->handle, i, SANE_ACTION_GET_VALUE, &fop->value, nullptr);
            }

            this->options.push_back(std::move(fop));
        }
        else if (opt->type == SANE_TYPE_INT) {
            std::unique_ptr<hyx::sane_device::int_option> iop{new hyx::sane_device::int_option(opt, i)};

            if (!iop->is_inactve()) {
                this->status = sane_control_option(this->handle, i, SANE_ACTION_GET_VALUE, &iop->value, nullptr);
            }

            this->options.push_back(std::move(iop));
        }
        else if (opt->type == SANE_TYPE_STRING) {
            std::unique_ptr<hyx::sane_device::string_option> sop{new hyx::sane_device::string_option(opt, i)};

            if (!sop->is_inactve()) {
                this->status = sane_control_option(this->handle, i, SANE_ACTION_GET_VALUE, sop->value, nullptr);
            }

            this->options.push_back(std::move(sop));
        }
    }

    // construct and return an ownerless vector of options
    std::vector<option*> ret;
    ret.reserve(this->options.size());
    std::transform(this->options.begin(), this->options.end(), std::back_inserter(ret), [](std::unique_ptr<option>& el) {
        return el.get();
    });
    return ret;
}

SANE_Int hyx::sane_device::set_option(const bool_option* bop, SANE_Bool new_value)
{
    return this->set_option_helper(bop, &new_value);
}

SANE_Int hyx::sane_device::set_option(const fixed_option* fop, SANE_Fixed new_value)
{
    return this->set_option_helper(fop, &new_value);
}

SANE_Int hyx::sane_device::set_option(const int_option* iop, SANE_Int new_value)
{
    return this->set_option_helper(iop, &new_value);
}

SANE_Int hyx::sane_device::set_option(const string_option* sop, SANE_String new_value)
{
    return this->set_option_helper(sop, new_value);
}

SANE_Parameters hyx::sane_device::get_parameters()
{
    SANE_Parameters parameters;
    this->status = sane_get_parameters(this->handle, &parameters);
    return parameters;
}

bool hyx::sane_device::start()
{
    this->status = sane_start(this->handle);
    return this->is_good();
}

bool hyx::sane_device::read(SANE_Byte* data, SANE_Int data_size)
{
    SANE_Int bytes_read;
    this->status = sane_read(this->handle, data, data_size, &bytes_read);

    return this->is_good();
}

void hyx::sane_device::cancel() noexcept
{
    sane_cancel(this->handle);
}

bool hyx::sane_device::is_good() noexcept
{
    return (this->status == SANE_STATUS_GOOD);
}
