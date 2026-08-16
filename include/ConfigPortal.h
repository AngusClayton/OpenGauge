#pragma once

bool startConfigPortal();
void stopConfigPortal();
bool isConfigPortalActive();
void noteConfigPortalActivity();
const char* getConfigPortalSsid();
const char* getConfigPortalPassword();
const char* getConfigPortalIp();
