# SMP-Bestandsaufnahme GNU Mach x86_64 (Stand upstream c5c121ae, 2026-11-22)

Legende: **[V]** im Code verifiziert, **[S]** vermutet / noch zu bestätigen.
Zeilennummern beziehen sich auf `upstream`.

## Kernaussage

Ein `--enable-ncpus=N`-Kernel bootet auf x86_64 alle APs und besteht die
Testsuite unter `qemu -smp 2`. Die APs laufen aber standardmäßig leer:
`MACH_HOST=1` (`configfrag.ac:63-67`) und `cpu_up()` (`kern/machine.c:98-102`)
stecken jede CPU ≠ 0 in `slave_pset`. Alle Tasks erben `default_pset`
(`kern/task.c:170`), d. h. das komplette Hurd läuft nur auf CPU0.
Nur über `/sbin/smp` (hurd `sutils/smp.c`) gestartete Programme laufen auf
den APs – und dann *nur* dort. IPC-Handoff über Pset-Grenzen hinweg wird
abgelehnt (`kern/ipc_sched.c:154-166`), Client/Server-Verkehr zwischen den
Psets nimmt also immer den langsamen Pfad.

„Echtes“ SMP erfordert deshalb zweierlei: Kernel-Fehler beseitigen, die erst
unter Last auf mehreren CPUs auftreten, und anschließend die Hurd-Server
selbst parallel laufen lassen (dort lauern eigene Races, s. u.).

## P0 – Sicherheit / Korrektheit

1. **[V] GS-Base aus dem Userland unvalidiert** – `i386/i386/pcb.c:713-719`:
   `i386_FSGS_BASE_STATE` maskiert `gs_base` nur im PCB, schreibt aber für
   `current_thread()` den *unmaskierten* Wert nach `MSR_KGSBASE`; weder FS
   noch GS werden auf Kanonizität geprüft. Die Entry-Pfade entscheiden über
   `swapgs` anhand des Vorzeichens von GSBASE (`x86_64/locore.S:188-213`).
   Negativer Wert → Kernel läuft mit User-`%gs` (Per-CPU-Daten!);
   nicht-kanonisch → #GP im Kernel beim `wrmsr`. → **Erster Patch.**
2. **[V] Gemeinsamer Exception-/Double-Fault-IST-Stack für alle CPUs** –
   `i386/i386/ktss.c:46-52` („FIXME: make it per-processor“).
3. **[V] Kein NMI-Handler, kein IST für NMI/#MC/#DB** – `x86_64/idt_inittab.S:106`,
   `locore.S:903`. `syscall64` läuft kurz auf dem User-RSP (`locore.S:1389ff`).
4. **[V] I/O-Permission-Änderungen nicht an andere CPUs propagiert** –
   `i386/i386/io_perm.c:314`, `pcb.c:295,370` (`#warning SMP support missing`).

## P1 – Scheduler / Processor Sets

5. **[V] APs nicht im `default_pset`**, BSP nicht umhängbar
   (`kern/machine.c:240-245`). Es fehlt eine Boot-Option/Policy
   „alle CPUs in einem Pset“. `processor_assign()`/`action_thread` ist der
   vorhandene, aber ungetestete Weg (`kern/machine.c:160-316`).
6. **[V] Keine Remote-Preemption** beim Einreihen in die Pset-Runqueue
   (`kern/sched_prim.c:1314-1327`).
7. **[V] Lost-Wakeup im Idle-Loop**: Prüfung, dann nacktes `hlt` ohne
   `cli; check; sti; hlt` (`sched_prim.c:1665-1686`, `i386/i386at/model_dep.c:251-258`).
8. **[S] Lock-Order `cpu_up` vs. `task_assign`** (`machine.c:86-88` vs.
   `task.c:995-1001`); `cpu_up` vergleicht mit `0` statt `master_cpu`.

## P1 – IPI / TLB-Shootdown / APIC

9.  **[V] CPU-Limits**: `cpu_set` „must be <= 32“, Masken via `int 1 << cpu`
    (`i386/intel/pmap.h:204`, `pmap.c:302,3143,3195`) – UB ab 32 CPUs, kein
    configure-Check. Flat-Logical-APIC-IDs `1 << (cpu % 8)` aliasen ab 9 CPUs
    (`i386/i386/apic.h:337-338`).
10. **[S] `i_bit_set`/`i_bit_clear` ohne `"memory"`-Clobber**
    (`i386/i386/lock.h:101-117`) – steuern `cpus_active`/`cpus_idle` im
    Shootdown-Protokoll.
11. **[V] Globale Kernel-PTEs** werden durch CR3-Reload nicht geflusht
    (`pmap.c:587,809-876`, `proc_reg.h:170`); kein PCID.
12. **[S] Shootdown-Deadlock-Potenzial**: Sender spinnt mit IF=0
    (alle spl ≥ 1 = `cli`, `x86_64/spl.S:98-127`).
13. **[V] Alle Geräte-IRQs physisch an den BSP** (`i386/i386at/ioapic.c:393-400`);
    nur ein IOAPIC funktioniert wirklich (`ioapic.c:291-333,374,462-488`);
    `apic_add_ioapic`/`apic_add_irq_override` ohne Bounds-Check (`apic.c:118-134`).
14. **[V] Kein x2APIC** (`acpi_parse_apic.c:551-554`, `apic.c:45`); [S] direktes
    x2APIC→xAPIC-Umschalten in `boothdr.S:67-72`/`cpuboot.S:278-286` ist laut SDM #GP.

## P1 – Uhr / Timer

15. **[V] hpclock nicht SMP-sicher** (`kern/mach_clock.c:93-95` `#warning`,
    `last_hpc_read` ohne Ordnung geschrieben, `:341`) – Zeit kann auf APs
    rückwärts laufen.
16. **[V] `softclkpending` global, nicht-atomar** (`x86_64/spl.S:257-264`).
17. AP-LAPIC-Timer nur einmal gegen den PIT kalibriert; LAPIC-als-Uhrquelle
    wurde revertiert (ddb0766a, „LAPIC unzuverlässig auf VMs“).

## P2 – Diagnose / Robustheit

18. **[V] Lock-Asserts sind auf SMP No-Ops** (`kern/lock.h:115-125`:
    `simple_lock_taken()` ≡ 1) – Hauptgrund, warum SMP-Bugs nur als Hänger
    auffallen. Eigentümer-Tracking (`holder = cpu/thread`) wäre billig.
19. **[V] Kein Stop-IPI** für panic/ddb/halt (`kern/debug.c:141-150`,
    `ddb/db_mp.c:150-160`, `model_dep.c:283-302`).
20. **[V] AP-Start**: kein 10-ms-Delay INIT→SIPI, kein Timeout beim Warten
    (`smp.c:143-211`, `mp_desc.c:331-337`); [S] `ONLINE_CAPABLE`-MADT-Einträge
    können Boot hängen lassen (`acpi_parse_apic.c:426`).
21. **[V] Interrupt-Stack misaligned** (`mp_desc.c:93`: `top - 4`).
22. **[S] FPU**: `init_fpu()` auf jedem AP überschreibt globale `fp_*`
    (`fpu.c:153-221`); kein per-CPU `fp_owner` (eager save bei jedem Switch).
23. **[V] Treiber ohne MP-Schutz**: `dev_master.h` ungenutzt; com/kd/lpr nur
    per lokalem spl (`com.c:488-561`, `kd_event.c:215-302`) – sicher nur,
    solange Aufrufer auf dem BSP bleiben. Linux-Treiber auf SMP deaktiviert.
24. **[S] `gsync_wait`: `copyin` unter Map-Read-Lock + kmutex**
    (`kern/gsync.c:255-276`) – Deadlock bei wartendem Writer.

## Hurd-Userland (relevant, sobald Server parallel laufen)

- **[S] libports: Doppel-Free** zwischen `ports_port_deref` und
  `_ports_complete_deallocate` bei gleichzeitigem Lookup
  (`libports/port-deref.c:46-49`, `complete-deallocate.c:32-54`).
- **[S] `ports_lookup_payload` ohne Lock** vs. sofortiges Free im
  No-Senders-Pfad (`libports/ports.h:270-288`).
- **[V] ext2fs: Node-Bitfelder ohne `np->lock`** im Pager-Pfad
  (`ext2fs/pager.c:740-800` → `getblk.c:193-199`).
- **[V] ext2fs: Spinlocks über blockierende Arbeit** (`balloc.c:326` u. a.).
- **[V] pfinet: Linux-Glue-Atomics/Spinlocks sind No-Ops** bei zwei
  Lock-Domänen (`pfinet/glue-include/asm/*`, `sched.c:27-28`).
- `sutils/smp.c:47` nimmt an, dass `psets[1]` der `slave_pset` ist.

## Vorgeschlagene Reihenfolge

1. P0-1 (GS-Base) – klein, klar, sicherheitsrelevant.
2. Diagnose zuerst: SMP-Lock-Ownership (18) und Stop-IPI (19) – macht alle
   weiteren Bugs findbar statt nur hängend.
3. Boot-Option „APs in `default_pset`“ (5) + Stresstests (`tests/`) mit
   Threads über alle CPUs → reale Races in vm/ipc provozieren.
4. Shootdown-Härtung (9–12), Idle-Wakeup (7), hpclock (15/16).
5. Hurd-Server: libports-Refcounting, dann ext2fs/pfinet.

---

## Status (2026-11-24)

Topic branches, each forked from `upstream`, patches in `../build/patches/`:

| Branch | Content | State |
|---|---|---|
| `fsgs-base-check` | P0-1: reject kernel/non-canonical fs/gs bases + test | tested, not mailed |
| `smp-lock-debug` | P2-18: `simple_lock_taken` on SMP, `can_sleep` guard, `--enable-ldebug` holder tracking | tested, not mailed |
| `smp-stop-ipi` | P2-19: `CALL_HALT` IPI from `Panic`/`halt_all_cpus` (fixed vector; IF=0 spinners need NMI) | tested in QEMU, not mailed |
| `percpu-volatile` | **new bug**: `percpu_get`/`percpu_ptr` not volatile → `cpu_number()` cached across `thread_sleep` | found by test-smp + ldebug, fixed |
| `smp-default-pset` | P1-5: `tests/test-smp` (processor_assign + VM/IPC/shootdown/task stress), testlib race fix, `smp_pset=default` kernel option | tested |
| `panic-lock` | `Panic` re-initialised `panic_lock` on every call | tested |
| `smp-integration` | merge of all of the above, for stress testing | — |

Results:
- `make check` with `smp_pset=default` on 4 CPUs, with `--enable-ldebug`: all pass.
- `test-smp` 3000 iterations on 8 CPUs with ldebug: pass.
- Debian GNU/Hurd amd64 (2025-08-07 image) boots to login with the
  integration kernel and `smp_pset=default` on 2 CPUs.

Deferred:
- Idle lost-wakeup (P1-7): needs an MD "check and halt atomically"
  interface; `splsched` is not `cli` on PIC i386 and `halt_cpu` reuses
  `machine_idle`.
- i386 (32-bit) builds of the IPI changes are untested here.
