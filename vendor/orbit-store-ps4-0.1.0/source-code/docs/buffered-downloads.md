# Buffered downloads

Developed on `feature/buffered-downloads`, based on local main `be558f2`, and
approved for the 0.7.0 release. Console installation requires the owner's approval
separately from local development checks.

## Transfer design

- An 8 MiB queue is divided equally among the active ranges. A separate writer
  has one additional 256 KiB scratch buffer and a 1 MiB thread stack.
- The network worker owns all curl handles and validates every response before
  enqueueing bytes. It never writes the file or waits on the queue-state mutex.
- The sole disk writer combines chunks into writes of up to 256 KiB. It performs
  drive checks, periodic flushes and the final flush. Networking continues until
  its bounded buffers fill, then waits for space rather than allocating more.
- Resume cursors advance only after the file flush succeeds. Other API state
  saves also serialize these durable cursors, never live or buffered progress.
- Pause, cancel, shutdown and HTTP failures stop input and join the writer before
  changing final status or removing a partial. Failed disk writes/flushes preserve
  the last durable cursor and cannot finalize a download.
- Eligible new files use four ranges. Existing two-range partials keep their
  original layout. Unsupported range responses, weak identity and small files
  retain a single connection. Eight/sixteen connections are available only in
  the isolated diagnostic, not normal app builds.
- PS5 transfer sockets request a 1 MiB receive buffer before connecting, without
  reducing a larger system buffer. The measured console grants 512 KiB instead
  of its 64 KiB default. A rejected tuning request leaves the connection usable;
  desktop builds retain their system defaults. Diagnostic builds can override
  this request for controlled comparisons.
- The writer prioritizes full blocks in round-robin order before coalescing a
  short buffer, so a slow range cannot make ready sibling buffers wait.

## Queue compatibility

Schema 4 stores one, two or four durable cursors. Schema 1 contiguous partials and
schema 2 two-range partials can migrate. The separate TorBox branch uses schema 3
and is not included here. Before loading a schema 1 or 2 queue, this build writes
an exact, private backup named `state-before-buffered-<random>.json` in Orbit's
state directory and refuses migration if the backup fails. Subsequent schema 4
starts do not create another backup. Diagnostics report `queueBackupCreated` for
the startup that made it. Older payloads reject schema 4; the original backup is
for rollback, not for resuming downloads started afterward. A file's length alone is
never sufficient to resume or complete a buffered download.

## Validation

Host writer and scheduler tests, queue integration tests and the optional transfer
benchmark cover the implementation. The benchmark in `experiments/transfer-benchmark`
is retained for future PS4 measurements. Old PS5 throughput and startup results
are archived separately and do not establish PS4 performance or power behavior.
