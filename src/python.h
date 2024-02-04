// <python.h> -*- C++ -*-
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

#ifndef HYX_PYTHON_H
#define HYX_PYTHON_H

#include <Python.h>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyx {
    using py_unique_object = std::unique_ptr<PyObject, decltype([](auto* py_obj) { Py_DECREF(py_obj); })>;

    class py_init;
    class py_module;

    class py_init {
    public:
        ~py_init();

        // singletons should not copy
        py_init(const py_init& si) = delete;
        py_init(py_init& si) = delete;
        py_init& operator=(const py_init& si) = delete;
        py_init& operator=(py_init&& si) = delete;

        [[nodiscard]] static py_init& get_instance();

        [[nodiscard]] py_module* import(const std::filesystem::path& module_name);

    private:
        py_init();

        std::unordered_map<std::string, std::unique_ptr<py_module>> imported_modules;
    };

    class py_module {
    public:
        ~py_module();

        // singletons should not copy
        py_module(const py_module& si) = delete;
        py_module(py_module& si) = delete;
        py_module& operator=(const py_module& si) = delete;
        py_module& operator=(py_module&& si) = delete;

        std::string call(const std::string& func, std::convertible_to<std::string> auto&&... args);

    private:
        py_module(const std::string& module_name, const std::string& path);

        PyObject* module;

        // let py_init to call private constructor
        friend py_init;
    };

    void enumerate_pack(auto f, auto... args)
    {
        [&]<std::size_t... Idx>(std::index_sequence<Idx...>) { (f(Idx, args), ...); }(std::make_index_sequence<sizeof...(args)>{});
    }

    std::string hyx::py_module::call(const std::string& func, std::convertible_to<std::string> auto&&... args)
    {
        py_unique_object func_uobj{PyObject_GetAttrString(this->module, func.c_str())};
        if (!func_uobj && PyCallable_Check(func_uobj.get())) {
            // TODO: add a way to view error from PyErr_* after checking for PyErr_Occurred
            throw std::runtime_error("Cannot find function \"" + func + "\"");
        }

        py_unique_object args_uobj{PyTuple_New(sizeof...(args))};

        enumerate_pack([&args_uobj](std::size_t i, const auto& arg) {
            auto* list_obj{PyList_New(0)};
            auto* arg_obj{PyUnicode_DecodeFSDefault(arg.c_str())};
            if (!arg_obj) {
                throw std::runtime_error("Could not convert an argument");
            }
            if (PyList_Append(list_obj, arg_obj) != 0) {
                throw std::runtime_error("Failed to append an argument to a list");
            }
            if (PyTuple_SetItem(args_uobj.get(), i, list_obj) != 0) {
                throw std::runtime_error("Failed to append list to arguments");
            }
        }, args...);

        // // TODO: allow for converting any type
        // for (const auto& [i, arg] : std::views::enumerate({args...})) {
        //     if (std::is_same_v<std::vector<std::string>, decltype(arg)>) {
        //         auto* list_obj{PyList_New(0)};
        //         for (const auto& element : arg) {
        //             if (std::is_same_v<std::string, element>) {
        //                 auto* arg_obj{PyUnicode_DecodeFSDefault(element.c_str())};
        //                 if (!arg_obj) {
        //                     throw std::runtime_error("Could not convert an argument");
        //                 }
        //                 if (PyList_Append(list_obj, arg_obj) != 0) {
        //                     throw std::runtime_error("Failed to append an argument to a list");
        //                 }
        //             }
        //         }
        //         if (PyTuple_SetItem(args_obj, i, list_obj) != 0) {
        //             throw std::runtime_error("Failed to append list to arguments");
        //         }
        //     }
        //     else if (std::is_same_v<std::string, decltype(arg)>) {
        //         auto* str_obj{PyUnicode_DecodeFSDefault(arg.c_str())};
        //         if (!str_obj) {
        //             throw std::runtime_error("Could not convert an argument");
        //         }
        //         if (PyTuple_SetItem(args_obj, i, str_obj) != 0) {
        //             throw std::runtime_error("Failed to append string to arguments");
        //         }
        //     }
        // }

        py_unique_object ret_uobj{PyObject_CallObject(func_uobj.get(), args_uobj.get())};
        if (!ret_uobj) {
            // TODO: add a way to view error from PyErr_*
            throw std::runtime_error("Failed to call function \"" + func + "\"");
        }

        // TODO: allow for converting any type
        auto size = PyUnicode_GetLength(ret_uobj.get());
        return std::string{PyUnicode_AsUTF8AndSize(ret_uobj.get(), &size)};
    }
} // namespace hyx

#endif // HYX_PYTHON_H
