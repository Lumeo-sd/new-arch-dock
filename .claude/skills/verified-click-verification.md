---
name: verified-click-verification
description: Use when a GUI action must be verified by clicking it - proves the click landed and what it did, instead of inferring from screenshots.
---

Verifying a UI action by clicking it with synthetic input is easy to get wrong.
Three distinct failure modes look identical from screenshots alone:

1. The click never landed (wrong coordinates, the surface was hidden or moving).
2. The click landed but the handler did nothing.
3. The handler ran and the effect happened, but it was already in that state.

Before clicking, establish and record the baseline: which state is on screen, and
is the control actually rendered where you think it is. A `toggle`-style trigger
cannot be used to normalise state - pressing it moves the state, it does not set
it. Getting this wrong invalidates every test that follows.

Measure the target's real position from the pixels instead of assuming it. Group
bright or icon-like pixel runs in the relevant scanline to find each control's
true centre, and check whether the cell has any content at all: an empty region
where an icon was expected means the icon failed to load, which presents exactly
like a dead click.

Instrument the code under test to log each stage, and make the instrumentation
distinguish "the event arrived" from "the effect happened". `QProcess::startDetached`
only reports that a child spawned; a child that silently fails looks identical to
success. Keep the process and log its exit code and stderr.

Correlate by counting new log lines across a single action, and count both sides
of the boundary - lines before and after - so manual shell equivalents of the
same action cannot be mistaken for the action under test.

Report only what a log or measurement shows. If a test is confounded, say so and
fix the test before drawing a conclusion; do not iterate on the code under test
while the test itself is unreliable.