❯   Arrays own 2 slots — and this is forced by C, not by us.

    typedef struct { int *items; int n_items; } Bag;   // the value type
    //                ^^^^^^^^^^  ^^^^^^^^^^^^
    //                ptr slot    count slot
    //                8 bytes     4 bytes
    //                separate fields of the value

    An array schema can't be "one slot" because in the value memory there's no one contiguous byte range that holds both the
  pointer
    and the count — C lays them out as two adjacent fields.   =>   hey. I don't want that. when we generate an array, the value is
   the array. the size could be filled anywhere else in the final datastruct consumed by a C function.

● You're right to push back. I was overselling "C forces this" — it's a design choice, not a physical law. Let me be honest about
  what's actually going on.

  What the array schema conceptually produces

  One value: an allocated buffer with some number of elements in it. That's what the generator actually creates. The shape tree
  already records its length via hegel_shape_array_len(sh). The length is metadata about the drawn value, not part of the value
  itself.

  What HEGEL_ARRAY as currently implemented does

  It doesn't just "generate an array." It also writes the length into a second field of the user's struct as a convenience, because
   the {T *items; int n_items;} C idiom is what most tested functions consume. That second write is the source of the "2 slots"
  framing — not because arrays are two slots, but because the current macro bundles "generate array" and "stamp the length into the
   parent struct" together.

  So the honest split

  - Schema concept: array → 1 value. Length lives in the shape.
  - Current macro: array-field-idiom → 2 value slots (ptr + len). The length write is a user-facing convenience.

  Those are different things. If we decoupled them, HEGEL_ARRAY becomes a 1-slot entry (just the pointer), and the length stops
  being written to the parent struct. The user can then either:

  1. Not store the length in the struct at all — read it from the shape tree when they need it. Fine for internal code, awkward for
   tested functions that take (T *items, int n_items) as two arguments.
  2. Store it via a separate entry that somehow references the array — some linking mechanism (positional reference, name, index).
  Gnarly in a positional API.
  3. Store it manually after draw — call hegel_shape_array_len on the relevant shape and assign to the struct field. Breaks the
  "hand me a populated struct" ergonomic.

  None of those are free.

  What that changes in the explanation I gave

  I should've said: "hegel-c's HEGEL_ARRAY macro chose to write both ptr and len into the parent struct because that's the common C
   idiom, and that's what makes it a 2-slot entry. The schema concept itself is 1 value. If you wanted to unify HEGEL_* macros
  under hegel_schema_t, the cleanest path would be to make HEGEL_ARRAY a 1-slot entry and stop writing the length automatically —
  but that pushes work onto the user."

  Worth exploring?

  Maybe. A 1-slot HEGEL_ARRAY + a separate HEGEL_ARRAY_LEN(ref_to_previous_array) or HEGEL_LEN_OF(slot_index) macro would be
  orthogonal and clean in principle, but picks up a new linking concept that has to be explained. Or we accept the two-slot
  convenience and call it "the idiom form" rather than pretending it's fundamental.

  Want me to add this to TODO.md as an exploratory item too? I'd phrase it as "decouple array-value generation from length-field
  writing in HEGEL_ARRAY" — a design question to revisit if the 2-slot shape ever becomes a real pain point.

✻ Crunched for 1m 21s

❯ How about something like : x = HEGEL_ARRAY(...); s = HEGEL_STRUCT(..., x.array_pointer, x.size); ??? Build the tree in a smart
  way? Everything works on draw?

● This is actually a genuinely good idea, and it generalizes beyond arrays. Let me sketch it honestly.

  What you'd write

  hegel_array_t x = HEGEL_ARRAY(hegel_schema_int_range(0, 100), 0, 10);

  hegel_schema_t s = HEGEL_STRUCT(Bag,
      x.array_pointer,   // goes wherever the ptr field is
      x.size);           // goes wherever the len field is

  /* Or with other fields in between: */
  hegel_schema_t t = HEGEL_STRUCT(Thing,
      HEGEL_INT(0, 100),  // some other field
      x.size,             // len comes first!
      HEGEL_TEXT(1, 5),   // another field
      x.array_pointer);   // ptr comes later

  What each accessor is

  x is a handle containing two layout entries plus a shared pointer to some generation state:

  typedef struct {
    hegel_array_state *  state;      // malloc'd, shared
    hegel_layout_entry   array_pointer;
    hegel_layout_entry   size;
  } hegel_array_t;

  typedef struct {
    hegel_schema *  generator;       // the underlying array schema
    void *          drawn_ptr;       // filled at draw time
    int             drawn_len;       // filled at draw time
    int             drawn_this_run;  // reset per test case
  } hegel_array_state;

  Both x.array_pointer and x.size carry a pointer to the same state. They're independent layout entries from the layout pass's
  point of view (each occupies one slot), but at draw time they collaborate.

  How draw resolves the dependency

  When hegel__draw_struct iterates bindings in order and hits either the x.array_pointer binding or the x.size binding:

  1. Check state->drawn_this_run. If not set, call state->generator's draw — this allocates the heap buffer and picks the length,
  storing both in state.
  2. Write the relevant facet (drawn_ptr or drawn_len) at this binding's offset.
  3. Mark drawn_this_run.

  The second binding encountered skips step 1 because state is already populated. Order doesn't matter — whichever of
  x.array_pointer or x.size comes first in the positional list triggers the actual generation.

  At the end of each test case (or start of the next), reset drawn_this_run = 0 so the next case draws fresh. Either the struct
  schema does this pre-draw, or the state has a "round stamp" compared against the current round.

  This generalizes beautifully

  Once you have "produce one value, project facets," you can do it for more than arrays:

  hegel_union_t u = HEGEL_UNION(
      HEGEL_CASE(HEGEL_DOUBLE(0.1, 100.0)),
      HEGEL_CASE(HEGEL_INT(), HEGEL_INT()));

  HEGEL_STRUCT(Weird,
      u.tag,           // tag at field 0
      HEGEL_FLOAT(),   // an unrelated field in between
      u.body);         // body at field 2 — non-adjacent from the tag!

  Variant, array_inline, even user-defined composites. The "2-slot adjacent" constraint of the current design is lifted entirely.

  The cost

  Not trivial. You need:
  - Shared heap state per grouped generation
  - New layout entry kinds for "read from shared state at this slot"
  - Draw-time resolution: check-then-generate-or-reuse
  - Per-test-case reset of the drawn_this_run flag, probably owned by the enclosing struct schema
  - Ownership story: the shared state must be freed exactly once, even though two layout entries reference it. Refcount on the
  state, or the struct schema takes exclusive ownership when it consumes the handle, or the array schema node itself carries the
  state.

  I'd estimate ~250–400 lines for the array case alone, maybe another 150 to generalize to unions/variants. Plus a handful of
  tests.

  The actual conceptual win

  The part I find most appealing is what it says about the model:

  ▎ A composite schema produces one value. That value has named facets (ptr, length, tag, body). You project facets into struct
  ▎ fields wherever they need to land. The schema's job is the generation; the layout's job is the placement; the two are fully
  ▎ orthogonal.

  That's cleaner than today's "a composite schema occupies 2 adjacent slots in declaration order." It matches how you think about
  the problem, not how C happens to represent it.

  My honest take

  I think this is the right direction for a V2 of the schema API. The positional form we just built is a clear step up from the
  offsetof form, but it still conflates "generate a value" with "place its pieces in a parent struct." Your proposal cleanly
  separates them. If you're building hegel-c with the "many users, upstream-blessed C binding" mindset (which your memory says you
  are), this is probably the shape the API should land in long term.

  Worth filing? Yes, strongly. Not as "nice-to-have exploratory" but as "the next major API iteration, when we're ready to revisit
  the positional form." Want me to write it up as a TODO entry — something clearly distinct from the HEGEL_INLINE exploratory note,
   since this is a bigger rethink rather than a gap-filler?

---

tbh already have enough ideas on how to make this work
