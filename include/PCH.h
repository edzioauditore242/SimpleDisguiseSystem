#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace logger = SKSE::log;
using namespace std::literals;