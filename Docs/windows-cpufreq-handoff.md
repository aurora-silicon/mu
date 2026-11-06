# J813 manual CPU-frequency device

The `J813CPUF` SSDT publishes `AURA8142` only when `/chosen` contains the exact
little-endian u32 `aura,windows-cpufreq-v1 = 1`, chip `0x8142` and board `0x24`.
Absent, malformed, disabled and other-target markers withhold the whole table.
This is separate from MTP and N1 admission and is disabled by default.

The device owns two noncacheable read/write 4 KiB resources, `0x210e20000`
(E cluster) and `0x211e20000` (P cluster). Their command registers are at offset
`0x20`. The resource layout and OPP mapping come from public Linux T8142/J813
support. No `_CPC`, `_PSS`, measured frequency or Windows automatic PEP policy
is advertised by this table.

The companion Aura profile `cpufreq: j813-manual-v1` validates the complete
firmware OPP ladders before publishing the marker. Native capability bit 11
and configuration bit 5 enforce CPU ownership: host INIT/SET requests are
blocked from successful configuration until a fresh resident reset, including
pre-initialization reconfiguration and guest debug pauses. The guest driver
must serialize transitions and retain its conservative boot-state ceiling.

Validation uses the actual table, production admission function and native
Aura proxy/configuration branches in `tools/windows_cpufreq_e2e.py` in the
companion Aura checkout. It retains source hashes, compiled AML and command
logs. Wrong target/marker, malformed OPP table and ownership revocation cases
are rejected. The normal MTP admission replay also passes. The full native
internal-storage Windows firmware builds with this table. Hardware transition
qualification and Windows PEP integration remain pending; no driver is installed
by these build or replay tools.
