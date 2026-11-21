# SURGE Cognition, Observation, and Memory Contract v1

Status: target architecture for the kana-decoder integration. This document supersedes earlier statements that SURGE Core should choose and commit the final normalized string. It does not authorize changes to the separate SURGE repository or the vendored third_party/surge implementation.

## 1. Responsibility boundary

SURGE Core is a streaming cognition and relational-memory engine. It receives externally classified and weighted information, assigns that information to clusters, develops typed relationships, re-evaluates context, performs inference, and exposes what it recognized.

SURGE does not make the application's final adoption decision. In particular, it must not decide which kana-kanji candidate is committed as the user's final text.

The external application (for kana-kanji conversion, the decoder host) owns:
- candidate acceptance, rejection, deferral, and final commitment;
- UI policy, correction policy, and application-specific output formatting;
- deciding whether feedback is authoritative enough to be projected into durable memory.

The Core owns:
- cluster membership and cluster lifecycle;
- relational graph formation and maintenance;
- attention/focus and context re-evaluation;
- mask-based inference and hypothesis generation;
- observation of actual internal cognition state.

## 2. Core operation contract

### 2.1 Projection

Projection input is already classified and weighted by an upstream producer (for example, a Language Set, statistical feature extractor, or external LLM).

A projection packet contains:
- stable event/idempotency identifier;
- namespace and normalized key;
- externally assigned classification;
- externally supplied weight/confidence;
- producer identity/version and provenance;
- optional typed relation observations, including direction, distance, order, and source evidence.

SURGE must not silently replace the supplied classification or weight with a second classification decision. SURGE owns the subsequent cluster assignment, membership validation, and relation integration.

A packet must retain its provenance and the original supplied values. Any Core-derived salience or confidence is a separate observation, not an overwrite.

### 2.2 Association

Relations are typed and retain direction, ordering, distance, evidence source, provenance, and confidence. Co-occurrence, semantic similarity, equivalence, dependency, temporal succession, agreement, and conflict are distinct relation families.

Graph closure is relation-specific. Identity may be transitive. Co-occurrence, dependency, conflict, and ordinary similarity must not be transitively closed without a separately justified rule. A path through two edges is not automatically a direct relation.

### 2.3 Cognition and mask inference

The Core may infer missing information by masking one or more spans and evaluating the surrounding graph context.

Mask inference supports at least:
- character/reading fragment masks;
- token masks;
- multi-token and variable-span masks;
- relation/role masks;
- context-conditioned masks using active focus and relevant historical clusters.

A mask query may return multiple hypotheses with supporting paths, contributing clusters, evidence references, uncertainty, and bounded scores. It must preserve ambiguity and allow an empty/unknown result when evidence is insufficient.

A hypothesis is not a fact. Inference output must not be silently promoted to durable memory. Promotion requires an explicit external feedback/projection packet with provenance and epistemic status.

### 2.4 Observation is the Core output

The public output of the cognition Core is an Observation, not a selected application result.

An Observation is a read-only view/trace of the actual operations performed by the Core, including:
- input packet identity and externally supplied classification/weight;
- assigned cluster IDs and membership changes;
- relations read, added, strengthened, weakened, or invalidated;
- active focus/attention and the reason for focus transitions;
- context clusters considered and excluded;
- mask spans and generated hypotheses;
- supporting and contradicting evidence paths;
- uncertainty, missing evidence, and budget/truncation status;
- memory mode/scope and state-version watermark.

Observation must be tied to real Core events/state. It must not be a post-hoc natural-language explanation invented independently of the execution trace.

Observation must not:
- select a final kana-kanji path;
- return a committed output string;
- commit application output;
- mutate memory merely because it was read;
- promote an unconfirmed hypothesis to a fact.

The external decoder consumes the Observation and decides what to accept or render. The external decision can later be returned as a new, provenance-bearing projection event.

## 3. Memory spaces and operating modes

Memory space and operating mode are separate concepts.

### 3.1 Memory spaces

| Space | Purpose | Persistence |
|---|---|---|
| Dictionary | curated, versioned lexical/grammar/reference knowledge | immutable snapshot during use |
| Persistent learning | accumulated observations and validated relations | durable, append/revision history |
| Session | current conversation/document/decoding context | isolated and disposable unless explicitly promoted |

### 3.2 Operating modes

| Mode | Read scope | Write scope | Purpose |
|---|---|---|---|
| Learning | Dictionary + Persistent + explicitly attached Session | Persistent | acquire and revise durable knowledge |
| Session | Dictionary + Persistent + current Session | current Session | contextual inference without cross-session leakage |
| Dictionary | Dictionary only | none | deterministic read-only lookup/inference |

Mode changes must be explicit, observable, and validated. A mode switch must not implicitly merge, clear, or promote memory. Session close discards session state unless an explicit promotion operation is requested.

Dictionary updates occur by publishing a new versioned snapshot, not by mutating the active dictionary graph in place.

## 4. Kana-kanji decoder integration

The Japanese Language Set and other upstream feature producers may provide:
- reading-to-surface lexical relations;
- morphology, POS, inflection, and connection constraints;
- directional distance-conditioned n-gram and skip-gram observations;
- dependency and semantic-role relations;
- orthographic realization rules;
- typed entities (number/date/time/range/unit/etc.);
- provenance and calibrated weights.

The Language Set may generate lexical alternatives and typed feature packets. It does not own cross-session SURGE memory or final user acceptance.

SURGE returns an Observation containing graph-grounded hypotheses and their evidence. The external decoder combines this with its own candidate coverage, safety constraints, UI state, and application policy before accepting a surface.

## 5. Runtime and resource invariants

- Offline and CPU-only operation remains supported.
- All projection, retrieval, and mask-inference operations have deterministic resource ceilings.
- Exceeding a ceiling is explicit in the Observation; incomplete inference must not be represented as complete.
- Session memory is isolated by session identity.
- Dictionary and persistent memory versions are included in observation provenance.
- Read-only Observation cannot advance the memory state version.
- Projection is idempotent and revision-aware.
- Raw corpus content is not emitted in routine CI artifacts; aggregate diagnostics are preferred.

## 6. Required conformance tests

1. Supplied classification and weight are preserved exactly; Core owns only cluster assignment.
2. Identical projection retries are idempotent.
3. Relation direction/order/distance and provenance survive persistence/replay.
4. Co-occurrence and conflict are not transitively closed.
5. Mask inference can return multiple hypotheses and an explicit unknown result.
6. Observation reports the actual cluster/relation/focus/mask events.
7. Observation is read-only and does not change state version.
8. Learning writes only persistent memory; Session writes only its own scope; Dictionary rejects writes.
9. Session A cannot retrieve Session B's private evidence.
10. External acceptance/rejection is represented as a new input event, not as a Core-side finalization.
11. Budget exhaustion is explicit and fail-closed.
12. Reopening the journal reproduces the same graph and observation-relevant state.

## 7. Migration from the current kana-decoder runtime

The current SurgeNormalizationRuntime::normalize path still calls UnifiedDecision, selects a path/surface, renders a final string, and persists an application commit. That behavior conflicts with this contract and is a migration target, not the intended end-state.

Migration must separate:
1. projection and graph cognition;
2. mask inference and read-only Observation;
3. external candidate acceptance/rendering/commit;
4. explicit feedback projection.

The old normalization API must not be silently treated as conformant. During migration it should be isolated as a compatibility path, clearly marked deprecated, and covered by tests that ensure new Observation APIs do not call it.

The vendored SURGE Core snapshot remains untouched in this repository task. Any missing primitive required by this contract must be implemented as a kana-decoder-side adapter or proposed separately for the upstream SURGE project.
