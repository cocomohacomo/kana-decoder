cmake_minimum_required(VERSION 3.16)
project(surge_core_vendor LANGUAGES CXX)
add_library(surge_core STATIC
  src/c_api.cpp
  src/core.cpp
  src/event_journal.cpp
  src/context_graph.cpp
  src/dependency_engine.cpp
  src/question_router.cpp
  src/scope_validator.cpp
  src/context_hint.cpp
  src/packet_admission.cpp
  src/sha256.cpp
  src/temporal_boundary_ledger.cpp
  src/temporal_question_ledger.cpp
  src/evidence_provenance.cpp
  src/chaos_cluster.cpp
  src/snapshot_domains.cpp
  src/snapshot.cpp
  src/snapshot_coordinator.cpp
  src/snapshot_store.cpp
  src/journal_backup_retention.cpp
)
target_include_directories(surge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(surge_core PUBLIC cxx_std_17)
set_target_properties(surge_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(surge_core PRIVATE /W4 /WX)
else()
  target_compile_options(surge_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
