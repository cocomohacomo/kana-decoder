cmake_minimum_required(VERSION 3.16)
project(kana_decoder LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SURGE is the sole context cognition and decision runtime. The vendored
# upstream Core keeps the complete application offline and reproducible.
add_subdirectory(third_party/surge)
add_library(kana_decoder_ja_language_set OBJECT
  language_sets/ja-JP/decoder.cpp
  language_sets/ja-JP/morphology_decoder.cpp
  language_sets/ja-JP/lexicon_trie.cpp
  language_sets/ja-JP/scoring.cpp
  language_sets/ja-JP/correction.cpp
  language_sets/ja-JP/pack_loader.cpp
  language_sets/ja-JP/japanese_language_pack.cpp
  language_sets/ja-JP/surge_language.cpp
  language_sets/ja-JP/surge_cluster.cpp)
target_include_directories(kana_decoder_ja_language_set PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(kana_decoder_ja_language_set PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kana_decoder_ja_language_set PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(kana_decoder_core src/engine.cpp $<TARGET_OBJECTS:kana_decoder_ja_language_set> src/streaming_session.cpp src/c_api.cpp src/pack_manifest.cpp src/language_pack.cpp src/surge_runtime.cpp)
target_include_directories(kana_decoder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kana_decoder_core PUBLIC surge_core)
target_compile_options(kana_decoder_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(kana_decoder_shared SHARED src/engine.cpp $<TARGET_OBJECTS:kana_decoder_ja_language_set>    src/streaming_session.cpp  src/c_api.cpp src/pack_manifest.cpp src/language_pack.cpp src/surge_runtime.cpp)
target_include_directories(kana_decoder_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kana_decoder_shared PRIVATE surge_core)
target_compile_options(kana_decoder_shared PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(kana_decoder_shared PROPERTIES OUTPUT_NAME kana_decoder)
# Release builds define NDEBUG. Keep C/C++ conformance assertions active so
# CTest does not silently compile away the test conditions.
function(kana_decoder_enable_test_assertions target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

add_executable(kana_decoder_tests tests/test_engine.cpp)
target_link_libraries(kana_decoder_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_tests)
target_compile_definitions(kana_decoder_tests PRIVATE KANA_DECODER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()

add_executable(kana_decoder_c_api_tests tests/test_c_api.c)
target_link_libraries(kana_decoder_c_api_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_c_api_tests)
target_include_directories(kana_decoder_c_api_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(kana_decoder_c_api_tests PRIVATE KANA_DECODER_PACK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite")
add_test(NAME c_api_integration_tests COMMAND kana_decoder_c_api_tests)

add_test(NAME kana_decoder_tests COMMAND kana_decoder_tests)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
 add_test(NAME pack_validation_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pack.py)
 add_test(NAME public_header_boundary_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_public_header_boundary.py ${CMAKE_CURRENT_SOURCE_DIR})
endif()
add_executable(kana_decoder_bench tests/bench_core.cpp)
target_link_libraries(kana_decoder_bench PRIVATE kana_decoder_core)

add_executable(kana_decoder_eval tools/evaluate_corpus.cpp)
target_link_libraries(kana_decoder_eval PRIVATE kana_decoder_core)

add_executable(kana_decoder_sequential_eval tools/evaluate_sequential_corpus.cpp)
target_link_libraries(kana_decoder_sequential_eval PRIVATE kana_decoder_core)

add_executable(kana_decoder_surge_eval tools/evaluate_surge_runtime.cpp)
target_link_libraries(kana_decoder_surge_eval PRIVATE kana_decoder_core)
add_test(NAME japanese_smoke_quality COMMAND kana_decoder_eval
  ${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ja-smoke-gold.tsv)
if(Python3_Interpreter_FOUND)
 add_test(NAME pack_builder_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pack_builder.py)
 add_test(NAME japanese_dictionary_import_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_japanese_dictionary_import.py)
 add_test(NAME python_ctypes_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ctypes.py $<TARGET_FILE:kana_decoder_shared> ${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite)
 add_test(NAME python_api_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python_api.py ${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite $<TARGET_FILE:kana_decoder_shared>)
 add_test(NAME python_package_build_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python_package_build.py)
 add_test(NAME sudachi_pipeline_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sudachi_pipeline.py)
 add_test(NAME sudachi_v1_import_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sudachi_v1.py)
 add_test(NAME sudachi_pack_build_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sudachi_pack_build.py)
 add_test(NAME evaluator_cli_contract_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_evaluator_cli.py $<TARGET_FILE:kana_decoder_eval> ${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ja-smoke-gold.tsv)
 add_test(NAME surge_probe_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_surge_probe.py)
 add_test(NAME memory_budget_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_memory_budget.py)
 add_test(NAME context_corpus_preparation_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prepare_context_corpus.py)
 add_test(NAME ablation_evaluation_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_evaluate_ablation.py)
 add_test(NAME ud_gsd_corpus_adapter_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prepare_ud_gsd_corpus.py)
 add_test(NAME context_corpus_ablation_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_evaluate_context_ablation.py)
 add_test(NAME sequential_decoder_evaluator_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_evaluate_sequential_decoder.py $<TARGET_FILE:kana_decoder_sequential_eval> ${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite)
endif()

add_executable(kana_decoder_trace_tests tests/test_decoder_trace.cpp)
target_link_libraries(kana_decoder_trace_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_trace_tests)
add_test(NAME decoder_trace_tests COMMAND kana_decoder_trace_tests)

add_executable(kana_decoder_surge_cluster_tests tests/test_surge_cluster.cpp)
target_link_libraries(kana_decoder_surge_cluster_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_surge_cluster_tests)
add_test(NAME surge_cluster_model_tests COMMAND kana_decoder_surge_cluster_tests)

add_executable(kana_decoder_surge_ngram_tests tests/test_surge_ngram.cpp)
target_link_libraries(kana_decoder_surge_ngram_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_surge_ngram_tests)
add_test(NAME surge_ngram_memory_tests COMMAND kana_decoder_surge_ngram_tests)

add_executable(kana_decoder_surge_language_tests tests/test_surge_language.cpp)
target_link_libraries(kana_decoder_surge_language_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_surge_language_tests)
add_test(NAME surge_language_foundation_tests COMMAND kana_decoder_surge_language_tests)

add_executable(kana_decoder_japanese_language_set_tests tests/test_japanese_language_set.cpp)
target_link_libraries(kana_decoder_japanese_language_set_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_japanese_language_set_tests)
add_test(NAME japanese_language_set_conformance_tests COMMAND kana_decoder_japanese_language_set_tests)

add_executable(kana_decoder_language_pack_tests tests/test_language_pack.cpp)
target_link_libraries(kana_decoder_language_pack_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_language_pack_tests)
target_compile_definitions(kana_decoder_language_pack_tests PRIVATE KANA_DECODER_PACK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite")
add_test(NAME immutable_language_pack_runtime_tests COMMAND kana_decoder_language_pack_tests)

add_executable(kana_decoder_surge_runtime_tests tests/test_surge_runtime.cpp)
target_link_libraries(kana_decoder_surge_runtime_tests PRIVATE kana_decoder_core)
kana_decoder_enable_test_assertions(kana_decoder_surge_runtime_tests)
target_compile_definitions(kana_decoder_surge_runtime_tests PRIVATE KANA_DECODER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME surge_core_normalization_integration COMMAND kana_decoder_surge_runtime_tests)
add_test(NAME surge_core_real_data_eval_smoke COMMAND kana_decoder_surge_eval
  ${CMAKE_CURRENT_SOURCE_DIR}/packs/ja-lite
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ja-smoke-gold.tsv general
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/empty-train-sequences.tsv
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/surge-decision-policy.tsv)

add_executable(kana_decoder_surge_bench tools/bench_surge_runtime.cpp)
target_link_libraries(kana_decoder_surge_bench PRIVATE kana_decoder_core)
