#pragma once

#include <stddef.h>

bool initConfigStorage();
bool loadPersistedConfig(char* error, size_t errorSize);
bool saveAndApplyConfig(const char* json, size_t length, char* error, size_t errorSize);

