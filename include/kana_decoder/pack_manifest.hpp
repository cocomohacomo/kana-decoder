#pragma once
#include <string>
namespace kana_decoder {
bool verify_pack_manifest(const std::string& directory, std::string* error = nullptr);
}
