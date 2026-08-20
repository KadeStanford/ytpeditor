# Milestone 4 dogfood report

Scenario: recreate a dense 30-second sentence-mix YTP from one old infomercial, add recurring reverse/pixelation gags, build a 20-copy escalating speed/pitch gag, remove a middle event with all-track ripple, and retrieve fragments from a 500-item Clip Library.

The automated dogfood harness records each interaction class independently and fails if the 60-cut build exceeds 2 seconds, the 20-copy gag or ripple edit exceeds 500 ms, or retrieval exceeds 250 ms on this development machine. The final run measured 17 ms for the 60-cut build, 2 ms for the 20-copy gag, below 1 ms for all-track ripple delete, and below 1 ms for 500-item retrieval.

No interaction exceeded its responsiveness budget. The slowest user-visible operation in the alpha remains processed preview/render generation; those operations are explicitly asynchronous and expose progress/cancellation. Timeline edits, ripple, effect assignment, and library retrieval remain synchronous because their measured latency is below the thresholds.

Known alpha constraints: processed preview is capped at 15 seconds; export currently evaluates effect parameter chains at their stored values rather than sampling every automation curve into FFmpeg expressions; the first public distribution still needs a frozen third-party source/license bundle.
