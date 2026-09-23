# Context Trainer

Choose your starts, learn each run once, then practise until your execution becomes reliable. Open **Pause > Trainer** to begin.

## Start a plan

1. Select **Choose starts** and tick the StartPos runs you want. **All / None** and the scrollable list help with longer levels.
2. Review **Options** if needed, then press **Start first pass**. The popup closes and play resumes directly.
3. During **Learn the level**, pass each selected start to the next selected start once. The last run ends at the actual level finish. Deaths retry the current run.
4. Once every run is passed, the mod builds your training plan. **Train now / Resume training** resumes automatic practice.

The first pass collects deaths, clicks, active time and form changes to prioritize training. Passing a run once does not establish reliability. **All runs** shows the first-pass checklist and lets you select training tasks after that pass is complete.

## Set your pace

Automatic training keeps the same target and StartPos until **1 minute of active play or 5 consecutive target passes** by default. Open **Pace** to choose **1–10 minutes per run**. Five clean passes release that wait and move to another selected task if one is available. Dying before passing the target breaks the streak; dying later preserves the target's success. Changing the target or entry resets its streak. Automatic changes happen between attempts, so the fifth pass never cuts off live play. This pacing rule is separate from the Reliable criterion.

After the minimum time, the plan can choose another task or test an earlier entry. **Next run** overrides the wait when you want to move on sooner. **Pause plan** stops automatic training and keeps your results.

During training, passing the target never restarts your attempt. Keep playing until death or the finish: every reached run in the current plan is still recorded, including runs unchecked for automatic selection. Earlier passes remain passed if you die later; unreached runs do not receive failures. Later results keep the actual entry context and inform future task selection among your checked runs. Time and input tracking continue for the whole attempt. The first pass still checks each selected StartPos separately.

Break reminders are notifications, not forced pauses. The default is **20 active minutes**, with **Off / 20 min / 30 min** available in **Pace**. A reminder appears at the end of an attempt and does not interrupt play. The plan's **Break reminders** option must also be enabled. The first pass, also called Stage 1, is excluded from both pacing and reminder timers.

Meeting the plan's reliability and entry criteria produces a readiness notification. Practice continues until you choose to pause it. Automatic training has no attempt-count limit; the block settings belong only to manual sessions under **Tools > Session**.

## Read your progress

**Stats > Map** shows the first-pass death histogram in 5% sections, with attempts and active play time. The observed-section strip identifies where data was collected. This map describes the first pass, not readiness for a full clear.

**Stats > Runs** answers: which runs are becoming reliable? It opens on the current plan with Normal and Practice combined. Each run has a total such as **2 passed / 7 attempts** and separate histories for starting near the run, entering earlier and starting from the beginning of the level. The last six outcomes are explicitly labelled **Pass / Fail**, with the newest on the right. Unfinished attempts are excluded.

**Reliable** means enough passes in a full recent sample: **4 of the last 6 checks** by default. These thresholds are adjustable. A longer entry tests whether a good short run also works with more of the level before it. The **i** button explains the counts and statuses.

**Filters** holds the advanced viewing filters:

- **Current plan / All saved runs:** choose which runs to browse, including older saved windows.
- **All modes / Normal / Practice:** the game mode in which the attempt began.
- **Controlled checks / Learning attempts:** verification records versus study records; learning attempts do not establish reliability.

These are optional viewing filters. The trainer uses **All modes + Controlled checks**; **Reset** restores that view. Reliability, transfer and next-run selection use the combined history. Entry contexts and Learning attempts stay separate. Respawning inside a window cannot pass its missing beginning; respawning before it can still supply a full run. Browsing filters never changes the active task.

## Optional original-level tracking

You can keep playing from zero in the StartPos copy. To share new original-level results instead, open **Original level** in the copy after the first pass, enter the original's online ID and choose **Save link** or **Save & exit**. A copy-source ID is only a suggestion; link the original with the same gameplay layout. Open that original through Saved or Search and play **Normal from 0%** without creating another plan.

Reached runs are recorded into the copy's plan with **Normal / From level beginning** context. Normal and Practice contribute to shared progress while retaining their mode metadata. The original uses passive recording: no automatic StartPos changes, rest pauses or completion-screen replay. Practice, native StartPos, test mode and ignore-damage cannot supply normal-from-zero evidence. Stats opens on combined progress in both the original and the copy; **Filters > Normal** can isolate original-mode attempts. New results are available to task selection when you return to training.

Return to the copy and choose **Continue training** when needed. **Unlink** removes the association for future visits without deleting recorded results. A given original can belong to one training plan.

## Saved plans and advanced tools

Saved plans reopen paused and keep their first-pass progress. **Change starts** prepares a new selection; confirming **Start over** repeats the first pass while preserving older run histories. Plans and histories from v0.2.1 onward remain compatible. Plans from v0.2.0 need a new first pass because the percentage system changed; their old window histories remain saved.

**Tools > Custom runs** edits manual ranges, notes and tags. Changing boundaries or the earlier-entry distance creates a new window and preserves the old history. **Tools > Session** contains manual controls, attempt-count blocks and self-reported mood.

Without StartPos, the available run is the full level from zero. For shorter runs, add StartPos objects to a local copy. Context Trainer switches native starts and clears practice checkpoints without editing level objects. Stopping or quitting restores the original StartPos; the mod does not turn practice mode off for you.

StartPos discovery refreshes after loading and when opening Trainer. Automatic plans use Blitzkrieg's 2.1 percentages: X divided by level length. Windows x64, Geometry Dash 2.2081 and Geode 5.10.1 are supported; platformer levels are not. All results stay on your device. Death and input signals are approximate, memory is an optional user-selected tag, and the consistency thresholds are training guides.
