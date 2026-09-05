# Contributing

Thanks for improving `vibe-usage-esp32`. Changes are welcome when they keep the
two boards' safety, privacy, and evidence boundaries explicit.

## Before opening a change

1. Read [`AGENTS.md`](AGENTS.md), even when you are not using a coding agent.
2. Read the task-specific architecture, API, installation, upstream, and latest
   acceptance documents linked there.
3. Open an issue before changing a wire contract, metric, cache schema,
   protected partition, upstream snapshot, or supported hardware list.
4. Never use a real credential, Wi-Fi password, personal Usage response, device
   identity dump, or private hostname as a fixture.

## Development setup

- ESP-IDF 5.5.3 for firmware builds.
- A C11 compiler for host tests.
- Python 3.9 or newer for repository and image verification.
- Both physical boards for claims that affect display, provisioning, Recovery,
  RTC, latch, power, or real network behavior.

Run the fast checks while iterating:

```bash
./tools/validate.sh --static
```

Before requesting firmware review, run the clean dual-target gate:

```bash
./tools/validate.sh
```

Do not hand-edit generated `sdkconfig`, managed components, build directories,
or release metadata. Change `sdkconfig.defaults`, lock files, source, or tools
instead. Do not add a command that erases the entire flash or NVS partition.

## Tests and evidence

Protocol and data changes need deterministic host tests, including malformed
and boundary cases. Device tests must name the exact board, source revision,
image checksum, action, expectation, observation, and sanitized evidence.
Record Build, Host tests, CI, real API, Device, Installer, and Power separately;
one category cannot stand in for another.

Fixtures should be minimal synthetic examples. Preserve integers above `2^53`
where relevant so accidental floating-point conversions remain detectable.

## Pull requests

Keep changes focused and explain:

- the user-visible result and contracts affected;
- why the implementation is safe on both the C3 resource floor and the S3;
- checks run and exact results;
- hardware cases run, with board-specific PASS/FAIL/NOT RUN;
- remaining risks or follow-up work;
- licenses and pinned revisions for any imported source.

Reviewers may ask for a smaller diff when a vendored update and product change
are mixed. Generated firmware, local settings, secrets, and raw device dumps
must not be committed.

By participating, you agree to follow [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
