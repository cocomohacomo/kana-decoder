# kana-decoder Specification v0.88

## Candidate survival through beam pruning

`CandidateScoreTrace` now includes `beam_survived`, which indicates whether the candidate's expanded state remains in the final retained beam at its destination position after all incoming edges for that position have been processed.

- Candidate generation and per-edge morphology shortlist coverage remain measured from lattice traces.
- `scored_or_expanded` counts gold token candidates observed in bounded score traces.
- `beam_survived` counts scored gold candidates whose expanded state survives destination-beam pruning.
- `beam_pruning_drop` counts scored gold candidates that do not survive that pruning.
- `final_selection_drop` remains the later loss between beam survival and the final best path. N-best recoverability is reported separately.
- Trace-capped cases remain separately identified; absence from a capped trace is not treated as proof that the candidate was not expanded.

The destination beam may be pruned incrementally as incoming edges are visited. The diagnostic therefore resets survival flags for all traced candidates ending at that position on each prune pass, then marks only the states retained by the latest pass. This reports final destination-beam survival rather than transient survival in an earlier incremental pass.

The diagnostic is bounded by the existing `max_trace_records` ceiling. It is evaluation instrumentation only and does not change scoring, beam width, candidate ordering, or decoding decisions. It does not claim an accuracy improvement.
