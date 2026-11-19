#include "kana_decoder/language_pack.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::string> surfaces(const kana_decoder::DecodedPath& path) {
  std::vector<std::string> result;
  for (const auto& token : path.tokens) result.push_back(token.candidate.surface);
  return result;
}

}  // namespace

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

  // Immutable snapshot identity and deterministic decoding.
  const auto second_reference = pack;
  assert(second_reference.get() == pack.get());
  const auto paths = pack->decode("あめ", kana_decoder::LanguageDecodeOptions{});
  const auto repeated_paths = pack->decode("あめ", kana_decoder::LanguageDecodeOptions{});
  assert(!paths.empty());
  assert(!paths.front().tokens.empty());
  assert(paths.size() == repeated_paths.size());
  assert(surfaces(paths.front()) == surfaces(repeated_paths.front()));

  // Provider-neutral path and resource policies are enforced by the pack.
  kana_decoder::LanguageDecodeOptions bounded;
  bounded.max_paths = 1;
  bounded.beam_width = 1;
  bounded.max_input_codepoints = 1;
  assert(pack->decode("あめ", bounded).empty());
  bounded.max_input_codepoints = 256;
  const auto single_path = pack->decode("あめ", bounded);
  assert(single_path.size() <= 1);
  assert(pack->decode(std::string("\xff", 1),
                      kana_decoder::LanguageDecodeOptions{}).empty());

  // The legacy C++ options bridge remains source-compatible during migration.
  kana_decoder::DecoderOptions legacy_bounded;
  legacy_bounded.max_input_codepoints = 1;
  assert(pack->decode("あめ", legacy_bounded).empty());

  // Incremental chunks must retain the full reading and converge to the
  // same best path as a one-shot decode.
  kana_decoder::StreamingSession streaming;
  kana_decoder::StreamingSnapshot snapshot;
  assert(pack->append(streaming, "あ", snapshot));
  assert(snapshot.reading == "あ");
  assert(!snapshot.paths.empty());
  assert(pack->append(streaming, "め", snapshot));
  assert(snapshot.reading == "あめ");
  assert(!snapshot.paths.empty());
  assert(surfaces(snapshot.paths.front()) == surfaces(paths.front()));

  // Sessions are isolated even when they share one immutable Pack.
  kana_decoder::StreamingSession other_streaming;
  kana_decoder::StreamingSnapshot other_snapshot;
  assert(pack->append(other_streaming, "い", other_snapshot));
  assert(other_snapshot.reading == "い");
  assert(streaming.reading() == "あめ");

  // Session limits are enforced transactionally: a rejected chunk does not
  // mutate the accepted reading or snapshot revision.
  kana_decoder::StreamingSession capped_streaming(1);
  kana_decoder::StreamingSnapshot capped_snapshot;
  assert(pack->append(capped_streaming, "あ", capped_snapshot));
  const auto capped_revision = capped_snapshot.revision;
  assert(!pack->append(capped_streaming, "め", capped_snapshot));
  assert(capped_streaming.reading() == "あ");
  assert(capped_snapshot.reading == "あ");
  assert(capped_snapshot.revision == capped_revision);

  kana_decoder::surge::Entity numeric;
  assert(pack->adapter().make_numeric_entity(
      "さんじゅうに", "test-number",
      kana_decoder::surge::NumericEntityTarget::Number, {}, numeric));
  std::string rendered;
  assert(pack->adapter().render_entity(
      numeric, kana_decoder::surge::NumericStyle::GroupedArabic, rendered));
  assert(rendered == "32");
  return 0;
}
