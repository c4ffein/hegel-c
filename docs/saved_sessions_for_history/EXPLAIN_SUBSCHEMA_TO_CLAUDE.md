❯ What should be the next item? How about we try to have sub-schemas representing parts of a generated data? For example, the
  length of an array? There is a hh=HEGEL_ARRAY(), and we can have a HEGEL_STRUCT(h.length, something_else, h.array) => on draw,
  when you get a hegel_subschema_t, you check if the schema has been drawn this turn, if it has not been, you draw, if it has, you
   get the value? WDYT?

● Yes — this is exactly the V2 handle sketch in TODO.md. And the timing is right: every macro you add before V2 lands is migration
  cost later. Before cutting code though, three things that sketch leaves open that I'd want nailed down:

  1. "Shared draw" vs "shared template" are two different problems. Your proposal solves the first: one array is generated, its
  length and ptr project to different slots. The HEGEL_INLINE_REF footgun is the second: one schema definition is re-instantiated
  independently at two sites (fresh draw each time). V2 handles as you described them solve (1) — they do NOT automatically solve
  (2). Worth being explicit about scope so you don't set an expectation the first cut can't meet.

  2. Round-stamp ownership. "Has it been drawn this turn?" needs a counter somewhere. Two options:

  - Per-TestCase counter bumped by the runner each case. Handles query state->last_round_id == current_round. Simple, one source of
   truth, but means every handle touches a shared location.
  - Per-enclosing-struct reset. The parent struct draw calls handle_reset() on each handle before drawing its fields. More local,
  more bookkeeping.

  Per-TestCase counter is simpler and I think correct — round identity is a TestCase-level concept, not a struct-level one. But it
  needs a hook in the runner to bump on each case.

  3. Draw-order semantics for shrinking. Whichever facet appears first in the positional list triggers the real generation.
  Rearranging the struct fields shifts where the byte-stream consumption happens. That's consistent with today's behavior
  (positional = draw-order), so probably fine — but it means HEGEL_STRUCT(h.length, other, h.array) and HEGEL_STRUCT(h.array,
  other, h.length) produce different shrinking sequences for the same logical schema. Worth documenting, not a bug.

  Scope suggestion. V1 of V2 = just HEGEL_ARRAY facets (.ptr, .length). Proof-of-concept. Ship it, write 2-3 pattern tests,
  validate the shape. Then generalize to HEGEL_OPTIONAL (.is_some, .value), HEGEL_UNION (.tag, .body), HEGEL_VARIANT (.tag, .ptr)
  once the mechanics are proven. Trying to do all four at once means the ownership story gets designed in the abstract instead of
  shaken out by one concrete case.

  Parallel track: HEGEL_ONE_OF hardening is ~20 lines, totally independent of V2, and catches silent memory corruption today. I'd
  do that first (same session, before starting V2 sketching) so it's not blocked behind a multi-week change.

  Want me to sketch the hegel_array_t handle struct and the draw-path modification concretely so we can pressure-test it before
  committing? Or hold on that until you've decided on scope?

✻ Sautéed for 1m 16s

❯ How about we'll provide a HEGEL_COPY to copy a tree and use somewhere else? WDYT?  ;  I'm wondering about schema vs shape now =>
   should we just have had mechanisms to fill and clean a shape, instead of having a separate schema? i'd say it's way worse but
  maybe i didn't ponder this possibility enough. should we check the drawn state at shape level? should the root of a shape have
  to arrays, one of pointers to schemas, one of either null or pointer to shapes? i think this is the simplest solution...  ;
  `HEGEL_STRUCT(h.length, other, h.array) and HEGEL_STRUCT(h.array,
    other, h.length) produce different shrinking sequences for the same logical schema` => we can document, but no impact on us,
  right? the order stays consistent accross all cases?  ;  nah, HEGEL_ONE_OF stays where it is for now. anyway. wanna try goin?

● HEGEL_COPY: good call. Orthogonal to V2 handles — handles solve "shared draw, project facets"; copy solves "shared template,
  independent draws" (the INLINE_REF footgun). Implementation is a structural tree walk; schemas are built once at setup so O(tree
  size) is fine. One thing to sanity-check before coding: how hegel_schema_self() is represented today. If it's a by-name sentinel
  that the draw path resolves to "the enclosing root," copy is trivial. If it's a stored pointer to a specific root, copy needs
  pointer rewrites. Worth a 30-second look first.

  Schema/shape merge: honestly, not obviously worse. The "sparse-mirror with nullable slots" idea is cleaner in some ways — one
  tree, one ownership story, no two-phase allocation. But it's a much bigger refactor than V2 handles. My recommendation is
  decouple: do handles in the current two-tier model first, see if they fit naturally. If handles feel forced in the split model,
  that's the empirical signal to merge. Committing to the merge now compounds risk with a change we don't yet need.

  Draw-order: confirmed — docs-only. Positional = deterministic per-schema; same across all test cases for a given struct layout.
  Two different struct layouts produce two different shrink sequences, but that's a user choice, not a per-run variance.

  Going: yes, let's. One scope confirmation before I start sketching:

  - V1 of V2 handles = HEGEL_ARRAY only (.ptr + .length facets) in the current schema/shape model. Two or three pattern tests to
  validate the mechanics.
  - HEGEL_COPY as a parallel small addition in the same session (independent mechanic, reads well alongside).
  - HEGEL_OPTIONAL / HEGEL_UNION / HEGEL_VARIANT facets deferred to V1.1 once we've seen what the array case actually looks like in
   code.

  Sound right? If yes I'll start with a minimal hegel_array_t handle sketch + the round-stamp hook, show you the shape of the
  change, then we iterate from there.

✻ Churned for 1m 27s

❯ Can you make a note in the TODO about HEGEL_COPY ?  ;  take a note in todo about schema/shape merge, only as exploratory?  ;
  good for draw order then :)  ;  good scope, except hegel_copy, defer this one? validate global mechanism first?

● Let me check the current TODO structure and find the right spot.

  Searched for 1 pattern, read 1 file (ctrl+o to expand)

● Update(TODO.md)
  ⎿  Added 49 lines
      172  current behavior so a future accidental fix doesn't silently change
      173  it.
      174
      175 +## `HEGEL_COPY` for shared-template instantiation
      176 +
      177 +A schema-tree deep-copy: `HEGEL_COPY(s)` returns a fresh independent
      178 +tree with the same shape.  Use case: place the same schema template
      179 +at two sites without the manual `hegel_schema_ref` bookkeeping that
      180 +`HEGEL_INLINE_REF` currently requires — fixes the silent double-free
      181 +footgun from the V2 rethink section above.
      182 +
      183 +Orthogonal to V2 handles: handles solve "shared draw, project
      184 +facets"; copy solves "shared template, independent draws".  Cheap
      185 +O(tree size) at schema-build time, no per-case cost.
      186 +
      187 +**Gotcha to verify before coding:** how `hegel_schema_self()` is
      188 +represented.  If it's a by-name sentinel resolved against the
      189 +enclosing root at draw time, copy is trivial.  If it stores a pointer
      190 +to a specific root, copy needs pointer rewrites during the tree walk.
      191 +
      192 +**Deferred** until V2 handles land.  Validate the broader "composite
      193 +schemas with projected facets" mechanic first — if handles happen
      194 +to subsume this use case, we don't need `HEGEL_COPY` at all.
      195 +
      196 +## Merge schema and shape into a single tree? (exploratory)
      197 +
      198 +Today: schema (immutable description, refcounted, built once) and
      199 +shape (per-run value wrapper, built on draw, owns value memory) are
      200 +separate tree types that mirror each other.
      201 +
      202 +Alternative to ponder: one tree.  Each node carries `(schema_kind +
      203 +schema_args + nullable drawn_state)`.  `drawn_state` is null until
      204 +populated by draw; reset to null at the start of each test case;
      205 +whole tree freed once at end.  The user's phrasing was "two parallel
      206 +arrays at the root, one of pointers to schemas, one of either null
      207 +or pointer to shapes" — same idea expressed as a sparse mirror.
      208 +
      209 +**Plausibly cleaner:** one ownership story, no two-phase allocation,
      210 +no refcount bookkeeping beyond an optional tree-level refcount if we
      211 +want `HEGEL_COPY` to share structure.
      212 +
      213 +**Risks:** much bigger refactor than V2 handles alone; schema becomes
      214 +mutable (fine in fork mode + sequential suite mode, but we lose the
      215 +invariant); every current shape-accessor API (`hegel_shape_tag`,
      216 +`_array_len`, `_is_some`, `_field`) needs re-examination.
      217 +
      218 +**Disposition:** exploratory only, decision deferred.  Do V2 handles
      219 +in the current two-tier model first.  If handles fit naturally there,
      220 +the split is earning its keep.  If they feel forced — uglier draw
      221 +path, extra indirection, awkward ownership — that's the empirical
      222 +signal to revisit this.
      223 +
      224  ## High-leverage next work
      225
      226  ### Point the library at real C code (Scotch, etc.)

● Both notes added under the V2 rethink section, before ## High-leverage next work. HEGEL_COPY is marked deferred pending V2
  validation; schema/shape merge is marked exploratory only with the "do V2 handles in two-tier first, use fit as signal"
  disposition.

  Scope locked: V1 of V2 handles = HEGEL_ARRAY only (.ptr + .length facets) in the current two-tier model. No HEGEL_COPY this
  session.

  Ready to start sketching the hegel_array_t handle struct + draw-path modification. Want me to go straight to code, or walk
  through the shape of the change in prose first so you can redirect before I commit to a direction?

✻ Baked for 1m 11s

❯ walk in prose is a good idea, see if our visions match :)

● Here's the shape of what I'd build. Tell me where my mental model diverges from yours.

  User-facing API.

  hegel_array_t h = HEGEL_ARRAY_HANDLE(
      hegel_schema_int_range(0, 100),  // element schema
      0, 10);                           // min_len, max_len

  hegel_schema_t s = HEGEL_STRUCT(Bag,
      HEGEL_INT(0, 100),   // unrelated
      h.length,            // int slot
      HEGEL_TEXT(1, 5),    // char* slot
      h.ptr);              // void* slot

  hegel_array_t is a small struct { state*, ptr (schema), length (schema) }. .ptr and .length are first-class hegel_schema_t values
   — they plug into HEGEL_STRUCT exactly like HEGEL_INT() does. The two facets are not required to be adjacent in the struct.

  Schema-tree representation.

  New schema kind: HEGEL_SCH_FACET, a leaf carrying (state*, facet_tag) where facet_tag ∈ {ARRAY_PTR, ARRAY_LEN}. The state
  (hegel__array_handle_state) holds the underlying array schema, min/max, and a round_id counter.

  Slot size/align for layout comes from the facet tag: ARRAY_LEN → int, ARRAY_PTR → pointer. Straightforward; the layout pass needs
   no new logic beyond a switch on facet_tag.

  State ownership: refcounted, shared between the two facets. hegel_schema_free on a facet decrements state refcount. Zero = state
  freed (and the underlying array schema dropped).

  Draw-path mechanic — the round stamp.

  Thread-local uint64_t g_round_id in the C runtime, bumped once at the start of each top-level hegel_schema_draw. Overflow is
  impractical (2^64 test cases).

  When hegel__draw_struct iterates to a facet binding:

  1. state->round_id == g_round_id → already drawn this case. Read cached ptr or length from state; write to the slot. Consumes
  zero bytes from the draw stream.
  2. state->round_id != g_round_id → first facet encountered this case. Call the underlying array schema's draw (this is the point
  where the byte stream is consumed); cache ptr, length, and the element-shape-tree in state; update round_id. Then write the
  requested facet to the slot.

  Either facet can be "first" — same code path, picked dynamically by positional order. That matches what you confirmed earlier:
  field order determines where bytes are consumed, consistent across all cases.

  Ownership & cleanup — the part I'd flag for discussion.

  Per-case resources (the array's value memory, its element shape tree) need freeing at end of case; the schema-level state
  persists across cases. Two ways:

  - (A) State owns per-case resources directly. Shape-tree free walks a "states touched this case" list that each struct shape
  maintains, calls cleanup on each state. Cost: maintain the list during draw.
  - (B) Primary facet owns, secondary facet is a weak view. At schema-build time (during HEGEL_STRUCT), walk bindings; first facet
  from a given state becomes "primary" — its shape slot will embed the array-element shape tree. Secondary facet's shape slot is a
  trivial leaf that looks up the value via state. Free path is unchanged recursion.

  I lean (B). The shape-free path is load-bearing; not touching it has real value. The cost is ~20 lines of bookkeeping at
  schema-build time, one primary bit on facet bindings. (A) sprinkles list-maintenance everywhere a facet is drawn.

  Shape accessors.

  For V1, hegel_shape_field(struct_sh, i) for a facet returns a primitive int or pointer shape (just the drawn value). Users don't
  currently need access to the array-element shape tree via a facet; if they did later, we'd add
  hegel_array_handle_element_shape(struct_sh, <facet binding>) that looks it up on the primary facet's slot. Defer until asked.

  Naming question.

  HEGEL_ARRAY_HANDLE as a new macro, keeping existing HEGEL_ARRAY (two-adjacent-slot form) as-is for V1. Both coexist during V1.
  V1.1+ can decide whether to migrate and delete the old form. This lets V1 be purely additive — no existing test breaks.

  What I'd flag for your take before I start typing:

  1. (A) vs (B) for ownership. My (B) lean isn't strong; if you see a reason (A) is cleaner, easy redirect.
  2. Facet kind genericity. HEGEL_SCH_FACET as written is generic (state* + tag); when we generalize to optional/union/variant
  later, the tag namespace grows but the mechanism is unchanged. Good, or should facet kinds be distinct (HEGEL_SCH_ARRAY_FACET,
  HEGEL_SCH_OPTIONAL_FACET, etc.) for clearer debugging?
  3. Coexistence vs replacement. HEGEL_ARRAY_HANDLE alongside HEGEL_ARRAY for V1 as above — or bite the bullet and make HEGEL_ARRAY
   return a hegel_array_t, breaking existing tests? I think coexistence for V1, but wanted to surface it explicitly.

  Where does this mismatch with how you were picturing it?

✻ Sautéed for 1m 58s

❯ There is no HEGEL_ARRAY_HANDLE. There is a HEGEL_ARRAY and a HEGEL_STRUCT. h=HEGEL_ARRAY(); h->size is a hegel_subschema_t and
  h->value is a hegel_subschema_t. WDYT?

● Got it — HEGEL_ARRAY grows, doesn't gain a sibling. Two clarification questions and one scope implication before I lock the
  design:

  Restated with your syntax:

  hegel_array_t *h = HEGEL_ARRAY(
      hegel_schema_int_range(0, 100),  // element schema
      0, 10);                           // min, max

  hegel_schema_t s = HEGEL_STRUCT(Bag,
      HEGEL_INT(0, 100),
      h->size,            // subschema — one int slot
      HEGEL_TEXT(1, 5),
      h->value);          // subschema — one pointer slot

  The internal mechanism I walked through last message is unchanged — shared state with a round-stamp, first subschema encountered
  does the real draw, second one reads cached values. Only the naming / user-facing surface changes.

  Q1 — is hegel_subschema_t a distinct C type from hegel_schema_t?

  Two readable options:

  - (a) Distinct wrapper: typedef struct { hegel_schema *_raw; } hegel_subschema_t; — parallel to hegel_schema_t. HEGEL_STRUCT
  accepts either via _Generic (or overloads the layout entry). Benefit: compile-time prevention of "use a subschema as a top-level
  schema." Cost: more surface, doubles the generic dispatch.
  - (b) Same type, different kind: hegel_subschema_t is just a typedef alias for hegel_schema_t, with internal kind =
  HEGEL_SCH_FACET. HEGEL_STRUCT doesn't need changes — facets plug in where schemas plug in. Misuse (passing a facet to
  hegel_schema_draw directly) is a runtime abort, not a compile error.

  I lean (b) for V1 — simpler, less churn, matches how HEGEL_INT() etc. already slot into struct entries. Distinct typing can be
  revisited later if misuse shows up in practice. You?

  Q2 — h->size with ->: heap-allocated handle returned as pointer?

  HEGEL_ARRAY(...) mallocs a hegel_array_t and returns the pointer. Lifetime: ownership transfers to the struct schema on use —
  freeing the struct schema frees the handle state via the subschemas. If you meant a by-value hegel_array_t h with h.size/h.value,
   tell me — I was reading -> literally.

  Scope implication: breaking change.

  Today HEGEL_ARRAY(elem, lo, hi) expands inline to two adjacent layout entries:

  HEGEL_STRUCT(T, HEGEL_ARRAY(int_schema, 0, 10));  // two slots, adjacent

  Under the new model, HEGEL_ARRAY returns a handle — it can't be used inline at the struct-entry position anymore. Every existing
  test using inline HEGEL_ARRAY needs migration to h = HEGEL_ARRAY(...); HEGEL_STRUCT(..., h->value, h->size).

  Mechanical migration, but not zero. Quick grep — let me count the call sites so you can decide whether to bite the bullet.

  Searched for 3 patterns (ctrl+o to expand)

● Migration cost: 7 inline call sites, 4 test files (plus header + docs). Small enough to be mechanical in the same session.

  So summarizing what I need from you to start coding:

  1. hegel_subschema_t — distinct C type (a) or alias for hegel_schema_t (b)? I lean (b).
  2. Handle by pointer (h->size, heap-allocated) or by value (h.size, stack struct)?
  3. Green light on migrating the 7 inline HEGEL_ARRAY sites as part of this change?

  And the one unresolved design call from the earlier walk: (A) state owns per-case resources via a "states touched" list vs (B)
  primary subschema owns the array shape, secondary is a weak view. I lean (B) for not touching the shape-free path.

  Once those four are locked, I'll start with hegel_gen.h type + state struct, then the draw path, then migrate the tests, then
  validate.

✻ Cogitated for 1m 40s

※ recap: We're designing V2 array handles where `h = HEGEL_ARRAY(...)` exposes `h->size` and `h->value` as subschemas pluggable
  into any `HEGEL_STRUCT` slot. Next: you answer four locked-in questions (subschema type, pointer vs value, migrate 7 call sites,
  ownership A vs B) so I can start coding. (disable recaps in /config)

❯ Q1 I'm not exactly sure... what if it is a hegel_schema_t, with a specific hegel_schema_kind? then, it has a hegel_schema_t to
  the actual schema + an offset to the part that it points at in the schema? WDYT?

● Yes — that's cleaner than my separate-state-struct version. Restating to confirm I've got it:

  Facet schema node:
  struct hegel_schema {
      hegel_schema_kind kind;      // = HEGEL_SCH_SUBSCHEMA (new)
      hegel_schema_t    source;    // points to the array schema
      int               offset;    // 0 = size, 1 = value, ...
      // (size/align for the parent struct's layout slot derived from offset)
  };

  Source (array) schema gets per-case cache fields:
  struct hegel_schema {
      hegel_schema_kind kind;      // = HEGEL_SCH_ARRAY
      hegel_schema_t    elem;
      int               min_len, max_len;
      // new:
      uint64_t          drawn_round_id;   // 0 = never drawn
      void *            drawn_ptr;
      int               drawn_len;
      hegel_shape *     drawn_shape;
  };

  Draw path: subschema's draw derefs source, checks source->drawn_round_id == g_round_id. If not, calls source's normal draw (which
   populates source's cache), updates round_id. Either way, reads source->drawn_{ptr,len} by offset and writes to the slot.

  Why this is better than my separate-state version:

  - One fewer allocation per handle (no separate state struct).
  - One fewer indirection in the hot draw path.
  - Conceptually tighter: "the array's state lives on the array" is more natural than "the array's state lives in a cookie the
  array points to."

  The real implication: schema nodes gain per-case mutable fields. That's a new invariant — today schemas are immutable after
  build. The round-stamp guard makes it safe (stale cache gets overwritten on round mismatch), and fork mode + sequential suite
  mode mean no concurrent mutation. But it's worth naming explicitly: we're conceding "schemas are immutable" as the price for this
   design. That concession aligns with your earlier instinct to eventually merge schema and shape — this is a small step in that
  direction.

  One remaining Q before I code:

  Heap-allocated handle (h->size, matches your -> syntax) or by-value struct (h.size)?

  Leaning heap just because your syntax was h->size and it lets the handle be passed around / assigned to variables without
  lifetime worry. But by-value is ~20 fewer lines. If you don't have a preference I'll go heap.

  Green light on (b) for the type (same hegel_schema_t, new kind) + state-on-source + heap handle?

✻ Churned for 1m 24s

❯ make a plan on top of TODO.md ?
