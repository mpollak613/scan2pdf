/**
 * @file hyx_python.cpp
 * @copyright
 * Copyright 2023 Michael Pollak.
 * All rights reserved.
 */

#include <Python.h>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "python.h"

hyx::py_init::py_init()
{
    Py_Initialize();
}

hyx::py_init::~py_init()
{
    // modules must be freed before calling finalize
    this->imported_modules.clear();
    Py_FinalizeEx();
}

hyx::py_init& hyx::py_init::get_instance()
{
    static py_init instance{};
    return instance;
}

hyx::py_module* hyx::py_init::import(const std::filesystem::path& module_name)
{
    if (!this->imported_modules.contains(module_name)) {
        return this->imported_modules.insert({module_name, std::unique_ptr<py_module>(new py_module(module_name.filename(), module_name.parent_path()))}).first->second.get();
    }

    return this->imported_modules.at(module_name).get();
}

hyx::py_module::~py_module()
{
    Py_DECREF(this->module);
}

hyx::py_module::py_module(const std::string& module_name, const std::string& path)
{
    auto* syspath{PySys_GetObject("path")}; // need to free?
    auto* path_obj{PyUnicode_DecodeFSDefault(path.c_str())};

    if (PyList_Insert(syspath, 0, path_obj) != 0) {
        throw std::runtime_error("Failed to find path \"" + path + "\"");
    }

    py_unique_object module_name_obj(PyUnicode_DecodeFSDefault(module_name.c_str()));
    if (!module_name_obj) {
        throw std::runtime_error("Failed to decode module name");
    }

    this->module = PyImport_Import(module_name_obj.get());
    if (!this->module) {
        // TODO: add a way to view error from PyErr_*
        throw std::runtime_error("Failed to import module \"" + path + "/" + module_name + "\"");
    }
}
