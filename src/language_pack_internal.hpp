#pragma once

#include "kana_decoder/language_pack.hpp"

#include <memory>
#include <string>

namespace kana_decoder::language_sets {

std::shared_ptr<const LanguagePack> load_japanese_language_pack(
    const std::string& directory, LanguagePackIdentity identity,
    std::string* error);

}  // namespace kana_decoder::language_sets
