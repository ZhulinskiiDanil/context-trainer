# v0.7.1

- Replace the overflowing Tools information alert with a fixed-size help page inside Tools.
- Keep the close button and Back action visible. Back, Escape or the close button returns to the previous Tools tab without losing draft edits.
- Shorten explanations and include a concrete lead-in example.

# v0.7.0

- Rebuild the training home around the current interval, six recent outcomes, and plan progress.
- Group pace settings into compact run-time and reminder controls.
- Give Statistics compact Runs / Map navigation, an interval track and aligned entry histories.
- Group custom-run inputs and focus tags; add Pause to edit directly in the editor.
- Use a consistent GD bitmap type hierarchy and neutral native button backgrounds tinted mint/lavender.
- Simplify first-pass, original-level and new-plan options screens. Keep training rules and saved histories unchanged.

# v0.6.2

- Open Statistics directly on combined Normal + Practice run progress.
- Use the same combined history for reliability, transfer evidence, next-run selection and plan completion.
- Keep game mode as an optional filter; Reset filters restores the combined view.
- Preserve every saved attempt and its original game mode; checkpoint respawns inside a run remain ineligible.

# v0.6.1

- Allow requested training starts and restarts even after an excluded attempt.
- Show Training active / Resume training after training starts.
- Keep Normal as the default statistics filter and add a direct Show trainer results (Practice) button.

# v0.6.0

- Detect suppressed deaths with Blitzkrieg's destroyPlayer hook priority and anticheat-object exclusion; check both ignore-damage flags.
- Detect scheduler speed multipliers and non-default scheduler time scale. Compare deltas within the same scheduler call, independent of frame rate and wall-clock lag.
- Latch assistance until restart and show Noclip / Speedhack: Attempt excluded without pausing gameplay.
- Keep per-window results provisional until the gameplay attempt ends, discarding the whole assisted attempt even if a target was passed earlier. Continue collecting later windows during clean attempts.
- Exclude assisted first-pass survey data, stage completion and pacing streaks. A clean restart resumes recording automatically.

# v0.5.3

- Default statistics to Normal on both StartPos copies and linked originals.
- Place Normal on the left and Practice on the right in game-mode filters, with matching explanation order.

# v0.5.2

- Use original GD button textures throughout Training, StartPos selection, Options, Pace, Original level, Statistics and Tools. Secondary actions now have visible backgrounds.
- Use the GD bitmap button font, green primary actions, blue selected controls with check marks, grey secondary controls, slate regular buttons and red destructive actions.
- Use native arrow and info sprites for navigation and help. Dim unavailable navigation buttons.
- Restyle clickable StartPos/run rows with native button frames; retain standard GD checkboxes and passive progress panels.
- Keep button geometry and callbacks; limit press enlargement to avoid overlap in compact rows.

# v0.5.1

- Replace the wide Trainer pause button with a Small DarkPurple CircleButtonSprite in the native right-button-menu, matching AskDash's button construction and automatic layout.
- Add a mint cube and lavender Context loop logo, used both on the pause button and in the Geode mod list.
- Declare the node IDs dependency required by the native menu integration.

# v0.5.0

- Release the pacing minimum after five consecutive clean target passes. Switch to another selected task at an attempt boundary when available; never interrupt the fifth live attempt.
- Reset the streak on failure before the target or on a target/StartPos change. Later deaths preserve an already passed target.
- Add optional Original level linking by online ID. Clean Normal attempts from zero in the linked original record reached runs into the StartPos copy's plan, with separate Normal and Practice statistics.
- Keep the original passive: no automatic starts, pauses or replay. Preserve the copy's start list and training plan when loading or saving original-level results.
- Add original-specific home/status and default statistics to Normal there. Allow unlinking without deleting results. Playing from zero in the SP copy remains available.

# v0.4.1

- Keep training attempts running after the target is passed, until death or native completion. Reaching a run boundary no longer restarts the level during Stage 2.
- Record every reached run in the current plan, including runs unchecked for automatic selection. Later results keep the original entry context and contribute to future scheduling.
- Preserve earlier passes after a later death, exclude unreached runs from failure counts, and continue time/input tracking through the full attempt.
- Show "Passed | Keep playing - still recording" in the gameplay HUD and explain continuous attempts on the training screen.
- Keep the one-minute minimum between automatic start changes, soft break reminders, first-pass calibration and saved progress.

# v0.4.0

- Keep the same training target and StartPos for at least one active minute by default. Automatic changes happen between completed attempts, including changes to an earlier entry.
- Add **Pace** with a 1–10 minute minimum per run and **Next run** to override the wait.
- Replace automatic attempt-count blocks with gentle break notifications: 20 active minutes by default, with Off / 20 min / 30 min controls. Reminders never pause play, and Stage 1 is excluded from both timers.
- Notify when the selected plan meets its readiness criteria while allowing training to continue. Manual attempt-count blocks remain available under Tools > Session.
- Rename Run results to **Run progress**, explain its purpose, and show clear passed/attempt counts for each entry type.
- Replace coloured outcome squares with the last six labelled **Pass / Fail** chips, newest on the right. Add **How to read this** for outcomes and reliability.
- Move mode and attempt-type filters into **Other records**, with explanations and a shortcut back to trainer records. Browse the current plan by default; older windows remain available through All saved runs.
- Preserve saved plans, first-pass progress, separate histories and the existing StartPos completion fixes.

# v0.3.0

- Rebuilt the interface around one training home: Choose starts, Learn the level, Train.
- Start and continue buttons close the popup and resume the game directly.
- Reused Blitzkrieg's dark popup and run-state panels, bundled with attribution.
- Added scrollable whole-row StartPos selection, All / None, clear options and an explicit restart confirmation.
- Added a first-pass death histogram and separate read-only run statistics.
- Moved custom run editing and manual session controls into Tools; browsing results no longer changes the active run.
- Replaced the gameplay HUD with two concise lines and clear paused states.
- Kept existing saved plans, histories, 2.1 StartPos search and completion fixes.

# v0.2.3

- Keep an automatic attempt that began in Practice valid when the same managed StartPos is in test mode or a native finish has occurred, even if the current practice flag is off.
- Allow completion-screen replay for an active managed training plan in that state.
- Preserve the original practice classification, revision and enabled checks, and existing diagnostic progress.
- Add regression checks for the observed finish state, invalid normal runs, and recording the completion exactly once.

# v0.2.2

- Count a native finish during the end animation and on the completion screen as well as through the delayed levelComplete callback, without recording duplicate attempts.
- Show completed Stage 1 intervals as Passed and the active interval separately as Current; the final objective says Finish the level.
- Continue builds Stage 2 directly when every diagnostic interval is already passed.
- Preserve v0.2.1 diagnostic progress and the position-2.1 measurement system.

# v0.2.1

- Fixed StartPos discovery running before asynchronous level setup finished and caching an empty result.
- Refresh StartPos after loading and when opening Context, preserving the original StartPos during training.
- Use Blitzkrieg's 2.1 X / level-length percentages for StartPos search, ordering and automatic training. No time-based 2.2 search.
- Old automatic plans require Create again to avoid mixing percentage systems; previous windows and history are preserved.

# v0.2.0

- New default Plan page with Create, StartPos checkboxes and six analysis options.
- Mandatory Stage 1: pass every selected StartPos interval once, recording unsuccessful attempts, clicks, active time and form changes.
- Stage 2 automatically selects training tasks using Stage 1 diagnostic priority and subsequent results, then tests longer entries after repeatability without changing window boundaries.
- Native StartPos switching in practice, automatic resets at intermediate goals and original StartPos restoration on Stop or quit. Level objects are unchanged.
- Whole-attempt rest budgets count early deaths once, independently of how many windows a run reaches.
- Saved diagnostic progress and plans reopen paused; Continue / Train resumes them.
- Training pauses when selected tasks meet repetition and available transfer criteria.
- Actual native StartPos percentages calibrate initial estimates; an underestimated previous interval is rechecked before Stage 1 completes.
- Rebuild preserves previous window histories. StartPos changes require a new plan.
- Kept manual Windows, Progress and Session tools with separate context and mode histories.
- Added standalone diagnostic-analysis and plan-selection checks.

After a run reaches 100%, training continues through the native completion screen's replay. Automatic replay stays off when a break is due or the plan has finished; the completion animation is not reset directly.

# v0.1.0

- Initial Context Trainer release for Geode 5.10.1 / GD 2.2081 on Windows.
- Named practice windows, cause tags and cue notes.
- Automatic attempt tracking with separate context and mode histories.
- Configurable recent consistency, return checks and transfer evidence.
- Bounded blocks, self-reported mood and input workload metrics.
- Offline persistence and standalone training-model tests.
