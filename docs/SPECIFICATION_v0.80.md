# kana-decoder Specification v0.80

## POS index capacity compaction

After in-place duplicate folding, the POS index now releases excess capacity when the folded size is smaller than the vector's capacity:
- allocate a compact vector reserved to the folded entry count;
- copy the folded index entries;
- swap vectors, allowing the oversized original allocation to be released at scope exit.

This reduces steady-state retained memory for duplicate IDs. The compaction copy temporarily holds two folded-size index buffers, rather than the pre-fold candidate-sized buffer plus a final index. It is conditional and skipped when there is no excess capacity.

## Constraints
- No `shrink_to_fit()` reliance.
- Index ordering and `lower_bound` lookup preserved.
- Conflicting POS values for duplicate IDs remain ambiguous (empty POS).
- This is a structural optimization; actual full Japanese-pack RSS improvement must be measured.

## Verification
CI is pending for this commit. No test or memory result is asserted until CI completes.
