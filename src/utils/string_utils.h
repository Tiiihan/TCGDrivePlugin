#pragma once

#include <string>
#include <vector>

namespace StringUtils {

std::string  toUtf8 (const std::wstring& wide);   // UTF-16 -> UTF-8
std::wstring fromUtf8(const std::string&  utf8);   // UTF-8  -> UTF-16

std::string urlEncode(const std::string& value);   

std::vector<std::wstring> splitPath(const std::wstring& path);

std::string trim(const std::string& s);  

} 
