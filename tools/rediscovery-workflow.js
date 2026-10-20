export const meta = {
  name: 'blind-rediscovery',
  description: 'Blind generative PBT rediscovery: hunter writes a hegel-c test from source alone, judge scores against a hidden known-bug description',
  phases: [
    { title: 'Hunt', detail: 'blind subagent generates a hegel-c test, builds, runs, reports' },
    { title: 'Judge', detail: 'compare report to the hidden known-bug description' },
  ],
}

// args = [{ id, arena, natural, difficulty }]  (NO description — hunters are blind)
// Judge descriptions are held here, shown ONLY to the judge stage.
const KNOWN = {
  idr: "idr_alloc()/idr_alloc_u32() returns an ID outside the requested [lo,hi] range when the range lies entirely below a non-zero IDR base (max-base underflows unsigned).",
  bch: "build_mod8_tables() (BCH ECC init) has signed-shift undefined behavior.",
  bootconfig: "xbc_node_compose_key_after() mishandles the snprintf truncation check, producing a wrong composed-key length.",
}

const FINDING = {
  type: 'object',
  properties: {
    built: { type: 'boolean' },
    ran: { type: 'boolean' },
    bug_found: { type: 'boolean' },
    bug_class: { type: 'string' },
    where: { type: 'string' },
    reproducer: { type: 'string' },
    report: { type: 'string' },
  },
  required: ['built', 'ran', 'bug_found', 'report'],
}

const VERDICT = {
  type: 'object',
  properties: {
    matches_known: { type: 'boolean' },
    is_new_candidate: { type: 'boolean' },
    confidence: { type: 'string' },
    reasoning: { type: 'string' },
  },
  required: ['matches_known', 'is_new_candidate', 'reasoning'],
}

const REF = '/home/dev/workspace/hegel-c/.claude/skills'

function huntPrompt(t) {
  return `You are auditing unfamiliar C code for bugs using hegel-c, a property-based testing framework. This is a BLIND audit: no bug is guaranteed, and you have NOT been told what to look for.

Work in this directory: ${t.arena}
1. Read BRIEF.md there, then read the target source file ${t.natural}.
2. Learn the hegel-c API from ${REF}/hegel/references/c/reference.md and the methodology from ${REF}/hegel-bughunt/SKILL.md (property catalogue, oracle design, edge inputs).
3. Write test_hunt.c in ${t.arena}: a hegel-c property test that exercises the target's public functions. Pick oracles the code should satisfy (round-trip, range/bounds invariants, no-crash-on-valid-input, idempotence). Draw broadly and include edge inputs.
4. Build and run: \`cd ${t.arena} && make test\` (this builds test_hunt and runs it under ASan/UBSan with the engine). Iterate on build errors. Do NOT edit the target source or the Makefile's build rules.
5. If a property fails or the process crashes/ASan-aborts, hegel shrinks it — record the minimal reproducer and which function/line.

Spend real effort on oracle design; that is where bugs surface. Return JSON: built, ran, bug_found, bug_class (e.g. oob/overflow/wrong-result/leaf), where (function), reproducer (minimal drawn values), report (2-4 sentences: what you tested and what fell out). If nothing failed after a genuine sweep, report bug_found=false honestly — a clean sweep is a valid result.`
}

function judgePrompt(t, finding) {
  return `A blind hunter audited a C file and produced this finding (it did NOT see the known-bug description):

HUNTER REPORT: ${JSON.stringify(finding)}

The KNOWN bug in this file (hidden from the hunter) is:
"${KNOWN[t.id]}"

Decide, with an asymmetric burden of proof (only reject a match on concrete grounds):
- matches_known: does the hunter's finding describe the SAME root cause as the known bug? Same function + same failure mechanism = match, even if worded differently. A crash/ASan abort in the right function counts if the mechanism aligns.
- is_new_candidate: if it does NOT match the known bug but is a plausible real bug the hunter found, mark this true (a candidate NEW find, not a failure).
- confidence: high/medium/low. reasoning: one or two sentences.`
}

phase('Hunt')
const targets = typeof args === 'string' ? JSON.parse(args) : (args || [])
if (!Array.isArray(targets) || !targets.length) { log('no targets passed in args'); return { error: 'no targets' } }

const results = await pipeline(
  targets,
  (t) => agent(huntPrompt(t), {
    label: `hunt:${t.id}`, phase: 'Hunt', schema: FINDING,
    model: t.difficulty === 'hard' ? 'fable' : 'sonnet',
  }).then((f) => ({ t, finding: f })),
  ({ t, finding }) => {
    if (!finding) return { t, finding: null, verdict: null }
    return agent(judgePrompt(t, finding), {
      label: `judge:${t.id}`, phase: 'Judge', schema: VERDICT, model: 'sonnet',
    }).then((v) => ({ t, finding, verdict: v }))
  }
)

const rows = results.filter(Boolean).map((r) => ({
  id: r.t.id,
  built: r.finding?.built ?? false,
  bug_found: r.finding?.bug_found ?? false,
  rediscovered: r.verdict?.matches_known ?? false,
  new_candidate: r.verdict?.is_new_candidate ?? false,
  class: r.finding?.bug_class || '',
  where: r.finding?.where || '',
  reproducer: r.finding?.reproducer || '',
  note: r.verdict?.reasoning || '',
}))

const rediscovered = rows.filter((r) => r.rediscovered).length
const candidates = rows.filter((r) => r.new_candidate).length
log(`rediscovered ${rediscovered}/${rows.length}; ${candidates} new-candidate finding(s)`)
return { n: rows.length, rediscovered, new_candidates: candidates, rows }
