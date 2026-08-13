#pragma once
#include <string>

#include <windows.h>

namespace Http {
	bool get(const std::string& url, std::string& responseBody, const std::string& headerName = std::string(), const std::string& headerValue = std::string(), const std::string& userAgent = std::string(), DWORD* outStatusCode = nullptr);
    bool postJson(const std::string& url, const std::string& jsonBody, std::string& responseBody);
}


