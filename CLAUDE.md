# CLAUDE.md

## Core Priorities

1. Correctness
2. Simplicity
3. Minimal impact
4. Verifiability
5. Maintainability
6. Speed

Favor caution over speed for non-trivial tasks.  
For trivial tasks, use judgment and avoid unnecessary process.

---

## 1. Think Before Acting

Before implementation, separate:

- confirmed facts
- assumptions
- uncertainties
- success criteria

Do not silently guess when ambiguity materially changes the result.

Ask the user only when:

- requirements are genuinely ambiguous
- multiple valid interpretations would lead to different implementations
- destructive or irreversible changes are required
- credentials, secrets, or external access are needed

Otherwise, document the assumption and proceed.

---

## 2. Simplicity First

Implement the minimum code that solves the requested problem.

Do not add:

- speculative abstractions
- unused configurability
- features not requested
- generalized systems for single-use cases
- defensive handling for impossible scenarios

If the solution feels over-engineered, simplify it before presenting it.

---

## 3. Surgical Changes

Touch only what is necessary.

When editing existing code:

- Do not refactor unrelated code.
- Do not reformat unrelated code.
- Do not rename unrelated symbols.
- Match the existing style.
- Remove only unused code caused by your own changes.
- Mention unrelated issues instead of fixing them silently.

Every changed line should trace directly to the task.

---

## 4. Plan Mode

Use plan mode for non-trivial tasks.

A task is non-trivial when it involves:

- 3 or more steps
- multiple files
- architectural decisions
- bug investigation
- behavior changes
- tests or verification
- uncertain root cause
- potential regression risk

For non-trivial tasks:

1. Write a plan to `tasks/todo.md`.
2. Include checkable items.
3. Include success criteria.
4. Include verification steps.
5. Proceed autonomously unless user input is required.
6. Update progress as work is completed.
7. Add a review section before finishing.

For trivial tasks, skip `tasks/todo.md` unless useful.

---

## 5. Goal-Driven Execution

Convert requests into verifiable goals.

Examples:

- "Fix the bug"  
  → reproduce the bug, identify root cause, implement fix, verify behavior.

- "Add validation"  
  → add failing validation tests, implement validation, make tests pass.

- "Refactor X"  
  → confirm existing tests pass, refactor, confirm behavior is unchanged.

Do not mark a task complete without evidence.

---

## 6. Autonomous Bug Fixing

When given a bug report:

- Do not ask for hand-holding.
- Investigate the root cause.
- Use logs, tests, errors, and diffs.
- Reproduce the issue when possible.
- Implement the smallest correct fix.
- Verify the fix.
- Explain the cause and the change.

If something goes sideways, stop and re-plan.

No temporary fixes unless explicitly requested.

---

## 7. Verification Before Done

Before final response:

- Run relevant tests if available.
- Check build or type errors when applicable.
- Inspect diffs.
- Compare old behavior and new behavior when relevant.
- Confirm success criteria are met.

If verification could not be performed, state exactly what was not verified and why.

---

## 8. Elegance Check

For non-trivial changes, check before finalizing:

- Is there a simpler solution?
- Is this fix local to the real cause?
- Did I introduce unnecessary abstraction?
- Did I touch unrelated code?
- Is there a cleaner way knowing what I know now?

If the fix feels hacky, replace it with the cleaner solution.

Skip this for simple, obvious fixes.

---

## 9. Subagent Strategy

Use subagents when they reduce complexity or improve confidence.

Good uses:

- large codebase exploration
- independent root-cause investigation
- comparing multiple implementation strategies
- research
- final diff review
- test failure analysis

Avoid subagents for:

- trivial edits
- obvious one-file changes
- simple formatting
- tasks where subagent overhead exceeds value

Each subagent should have one focused objective.

---

## 10. Task Files

For non-trivial tasks, use:

- `tasks/todo.md`
- `tasks/lessons.md`

### `tasks/todo.md`

Template:

~~~md
# TODO

## Goal

## Assumptions

## Success Criteria

## Plan

- [ ] Step 1
- [ ] Step 2
- [ ] Step 3

## Verification

- [ ] Test/build/log check
- [ ] Behavior check
- [ ] Diff review

## Review

### Changed

### Verified

### Not Verified

### Risks
~~~

### `tasks/lessons.md`

Update after meaningful user corrections.

A meaningful correction includes:

- repeated mistake pattern
- project convention
- misunderstood requirement
- quality standard violation
- process failure likely to recur

Format:

~~~md
## Lesson: [short title]

Pattern:
- What went wrong

Rule:
- What to do next time

Example:
- Concrete example if useful
~~~

Do not fill lessons with one-off noise.

---

## 11. Communication

Keep summaries high-level.

When reporting progress:

- explain what changed
- explain why
- explain how it was verified
- mention remaining risks

Do not over-explain obvious edits.  
Do not claim completion without proof.

---

## 12. Final Response Format

For non-trivial code changes:

~~~txt
Summary
- what changed

Verification
- what was tested / checked

Notes
- risks, limitations, or things not verified
~~~

For trivial tasks, answer directly.

<!-- 대화 중 발견된 프로젝트 규칙이 여기에 추가됩니다. -->

## Project Rules
- 윈도우, 웹의 멀티 플랫폼을 지원해야 한다는 것을 잊지 말 것.
- 작업 시 멀티 플랫폼 호환성을 생각하고, 윈도우 파츠를 구현했다면, 웹도 동일 선상의 기능을 병렬적으로 구현할 것.
- 줄 끝 일관성에 유의하며 작업
- 사용자가 설계/구조/필요성을 되물었을 때는 즉시 추가/삭제로 행동하지 말고, 현재 런타임 필수값인지, 디버그/진단 편의값인지, 차후 확장에 필요한 계약인지 구분해서 답한 뒤 지시가 있을 때만 수정할 것.
- 사용자가 "필요한가", "왜 있는가", "확장에 어떤가"처럼 검토 맥락으로 묻는 경우에는 코드 변경 요청으로 해석하지 말고, 판단 기준과 추천 방향을 먼저 제시할 것.
- 수정 전에는 해당 구현이 왜 존재했는지 먼저 읽고, 유지했을 때 깨지는 반례와 바꿨을 때 영향 범위를 확인한 뒤 고칠 것.
- 수정 보고는 중요한 항목마다 코드를 읽고 "어떻게 생각했고 / 어떤 반례를 찾았고 / 어떻게 고쳤다" 흐름으로 남길 것.
- 직렬화는 JSON보다 YAML/바이너리 방향을 우선한다.
- 임시 bool 플래그 추가로 회피 금지
