# kana-decoder Specification v0.82

## Frozen runtime Trie nodes

Mutable construction uses separate `BuildNode` instances that own edge and candidate vectors. The loaded runtime representation uses compact `Node` records containing only four 32-bit arena range fields. Child references and candidates reside in shared contiguous arenas.

After both arenas are frozen, the builder-node vector is released. Runtime lattice traversal and POS-index construction use frozen nodes and arenas. For a Trie built incrementally through `add()`, traversal continues to use the mutable builder representation until freezing.

## Mutation compatibility

Calling `add()` after loading expands frozen arenas into builder nodes before mutation. This preserves the mutable API, with a deliberate temporary memory cost during thawing. A subsequent freeze/load restores compact runtime storage.

## Safety and verification

32-bit node and arena bounds remain enforced. CI covers loading, traversal, duplicate handling, POS lookup, mutation after load, and repeated freeze/thaw cycles. The 100 MiB synthetic guard is not a full Japanese-pack memory claim.

## Progressive arena transfer

Freezing transfers each builder node's edge and candidate buffers into the contiguous runtime arenas using move iterators, then releases that node's source buffer immediately. This avoids retaining already-transferred per-node vector capacities until the entire arena has been built. Arena capacity is precomputed and reserved before transfer; 32-bit range checks run before mutation.
