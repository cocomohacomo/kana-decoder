#include "kana_decoder/language_pack.hpp"

#include <cassert>
#include <memory>
#include <string>

int main() {
  std::string error;
  const auto pack = kana_decoder::load_language_pack_snapshot(
      KANA_DECODER_PACK_DIR, &error);
  assert(pack && error.empty());
  assert(pack->identity().pack_id == "ja-lite-reference");
  assert(pack->identity().language_tag == "ja-JP");
  assert(pack->identity().version == "0.1.0");
  assert(pack->identity().digest.size() == 64);
  assert(pack->adapter().language_tag() == "ja-JP");
  const auto second_reference = pack;
  assert(second_reference.get() == pack.get());

  const auto paths = pack->decode("あめ", {});
  assert(!paths.empty());
  assert(!paths.front().tokens.empty());

  kana_decoder::StreamingSession streaming;
  kana_decoder::StreamingSnapshot snapshot;
  assert(pack->append(streaming, "あ", snapshot));
  assert(snapshot.reading == "あ");
  assert(!snapshot.paths.empty());

  kana_decoder::surge::Entity numeric;
  assert(pack->adapter().make_numeric_entity(
      "さんじゅうに", "test-number", kana_decoder::surge::NumericEntityTarget::Number,
      {}, numeric));
  std::string rendered;
  assert(pack->adapter().render_entity(
      numeric, kana_decoder::surge::NumericStyle::GroupedArabic, rendered));
  assert(rendered == "32");
  return 0;
}
