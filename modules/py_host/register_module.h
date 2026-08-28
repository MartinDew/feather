#pragma once

#include <main/init_level.h>

namespace feather {
void register_py_host(InitLevel level);

void unregister_py_host(InitLevel level);
} //namespace feather
