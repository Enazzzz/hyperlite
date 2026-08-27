# Cloud Agent VM Hardware Specs

Bench-context note capturing raw hardware inventory for this Cursor Cloud Agent VM. Not product documentation. Do not invent values; all figures below are verbatim command output.

Captured: 2026-08-27

## Key facts (from command output)

- **Model name:** Intel(R) Xeon(R) Processor
- **Vendor ID:** GenuineIntel
- **CPU(s):** 4
- **Thread(s) per core:** 1
- **Core(s) per socket:** 4
- **Socket(s):** 1
- **BogoMIPS:** 4800.00
- **Hypervisor vendor:** KVM
- **Virtualization type:** full
- **RAM:** MemTotal 16398384 kB (~15Gi from `free -h`)
- **Swap:** 0B
- **NUMA node(s):** 1 (NUMA node0 CPU(s): 0-3)
- **Caches:** L1d 192 KiB (4 instances); L1i 128 KiB (4 instances); L2 8 MiB (4 instances); L3 320 MiB (1 instance)
- **ISA of interest:** sse4_1 sse4_2 avx avx2 avx512f avx512dq avx512ifma avx512cd avx512bw avx512vl avx512_bf16 avx512vbmi avx512_vbmi2 avx512_vnni avx512_bitalg avx512_vpopcntdq avx512_fp16 amx_bf16 amx_tile amx_int8
- **SMT active:** 0 (`/sys/devices/system/cpu/smt/active`)
- **CPU freq governor / max freq:** not present (paths empty / missing)
- **GPU:** none — no `/dev/nvidia*`, `nvidia-smi` produced no output
- **OS:** Ubuntu 24.04.4 LTS (noble)

## Raw command outputs

### `uname -a`
```
Linux cursor 6.12.94+ #1 SMP PREEMPT_DYNAMIC Wed Aug 26 20:41:45 UTC 2026 x86_64 x86_64 x86_64 GNU/Linux
```

### `nproc`
```
4
```

### `free -h`
```
               total        used        free      shared  buff/cache   available
Mem:            15Gi       787Mi        14Gi        17Mi       1.1Gi        14Gi
Swap:             0B          0B          0B
```

### `lscpu`
```
Architecture:                            x86_64
CPU op-mode(s):                          32-bit, 64-bit
Address sizes:                           46 bits physical, 57 bits virtual
Byte Order:                              Little Endian
CPU(s):                                  4
On-line CPU(s) list:                     0-3
Vendor ID:                               GenuineIntel
Model name:                              Intel(R) Xeon(R) Processor
CPU family:                              6
Model:                                   207
Thread(s) per core:                      1
Core(s) per socket:                      4
Socket(s):                               1
Stepping:                                2
BogoMIPS:                                4800.00
Flags:                                   fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts mmx fxsr sse sse2 ss ht syscall nx pdpe1gb rdtscp lm constant_tsc pebs bts rep_good nopl xtopology nonstop_tsc cpuid tsc_known_freq pni pclmulqdq dtes64 vmx ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves avx_vnni avx512_bf16 wbnoinvd arat vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq rdpid bus_lock_detect cldemote movdiri movdir64b fsrm md_clear serialize tsxldtrk amx_bf16 avx512_fp16 amx_tile amx_int8 flush_l1d arch_capabilities
Virtualization:                          VT-x
Hypervisor vendor:                       KVM
Virtualization type:                     full
L1d cache:                               192 KiB (4 instances)
L1i cache:                               128 KiB (4 instances)
L2 cache:                                8 MiB (4 instances)
L3 cache:                                320 MiB (1 instance)
NUMA node(s):                            1
NUMA node0 CPU(s):                       0-3
Vulnerability Gather data sampling:      Not affected
Vulnerability Indirect target selection: Not affected
Vulnerability Itlb multihit:             Not affected
Vulnerability L1tf:                      Not affected
Vulnerability Mds:                       Not affected
Vulnerability Meltdown:                  Not affected
Vulnerability Mmio stale data:           Not affected
Vulnerability Reg file data sampling:    Not affected
Vulnerability Retbleed:                  Not affected
Vulnerability Spec rstack overflow:      Not affected
Vulnerability Spec store bypass:         Mitigation; Speculative Store Bypass disabled via prctl
Vulnerability Spectre v1:                Mitigation; usercopy/swapgs barriers and __user pointer sanitization
Vulnerability Spectre v2:                Mitigation; Enhanced / Automatic IBRS; IBPB conditional; PBRSB-eIBRS SW sequence; BHI BHI_DIS_S
Vulnerability Srbds:                     Not affected
Vulnerability Tsa:                       Not affected
Vulnerability Tsx async abort:           Mitigation; TSX disabled
Vulnerability Vmscape:                   Not affected
```

### `cat /proc/cpuinfo`
```
processor	: 0
vendor_id	: GenuineIntel
cpu family	: 6
model		: 207
model name	: Intel(R) Xeon(R) Processor
stepping	: 2
microcode	: 0x1
cpu MHz		: 2400.000
cache size	: 327680 KB
physical id	: 0
siblings	: 4
core id		: 0
cpu cores	: 4
apicid		: 0
initial apicid	: 0
fpu		: yes
fpu_exception	: yes
cpuid level	: 32
wp		: yes
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts mmx fxsr sse sse2 ss ht syscall nx pdpe1gb rdtscp lm constant_tsc pebs bts rep_good nopl xtopology nonstop_tsc cpuid tsc_known_freq pni pclmulqdq dtes64 vmx ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves avx_vnni avx512_bf16 wbnoinvd arat vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq rdpid bus_lock_detect cldemote movdiri movdir64b fsrm md_clear serialize tsxldtrk amx_bf16 avx512_fp16 amx_tile amx_int8 flush_l1d arch_capabilities
vmx flags	: vnmi preemption_timer posted_intr invvpid ept_x_only ept_ad ept_1gb ept_5level flexpriority apicv tsc_offset vtpr mtf vapic ept vpid unrestricted_guest vapic_reg vid shadow_vmcs pml tsc_scaling
bugs		: spectre_v1 spectre_v2 spec_store_bypass swapgs taa eibrs_pbrsb bhi ibpb_no_ret
bogomips	: 4800.00
clflush size	: 64
cache_alignment	: 64
address sizes	: 46 bits physical, 57 bits virtual
power management:

processor	: 1
vendor_id	: GenuineIntel
cpu family	: 6
model		: 207
model name	: Intel(R) Xeon(R) Processor
stepping	: 2
microcode	: 0x1
cpu MHz		: 2400.000
cache size	: 327680 KB
physical id	: 0
siblings	: 4
core id		: 1
cpu cores	: 4
apicid		: 1
initial apicid	: 1
fpu		: yes
fpu_exception	: yes
cpuid level	: 32
wp		: yes
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts mmx fxsr sse sse2 ss ht syscall nx pdpe1gb rdtscp lm constant_tsc pebs bts rep_good nopl xtopology nonstop_tsc cpuid tsc_known_freq pni pclmulqdq dtes64 vmx ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves avx_vnni avx512_bf16 wbnoinvd arat vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq rdpid bus_lock_detect cldemote movdiri movdir64b fsrm md_clear serialize tsxldtrk amx_bf16 avx512_fp16 amx_tile amx_int8 flush_l1d arch_capabilities
vmx flags	: vnmi preemption_timer posted_intr invvpid ept_x_only ept_ad ept_1gb ept_5level flexpriority apicv tsc_offset vtpr mtf vapic ept vpid unrestricted_guest vapic_reg vid shadow_vmcs pml tsc_scaling
bugs		: spectre_v1 spectre_v2 spec_store_bypass swapgs taa eibrs_pbrsb bhi ibpb_no_ret
bogomips	: 4800.00
clflush size	: 64
cache_alignment	: 64
address sizes	: 46 bits physical, 57 bits virtual
power management:

processor	: 2
vendor_id	: GenuineIntel
cpu family	: 6
model		: 207
model name	: Intel(R) Xeon(R) Processor
stepping	: 2
microcode	: 0x1
cpu MHz		: 2400.000
cache size	: 327680 KB
physical id	: 0
siblings	: 4
core id		: 2
cpu cores	: 4
apicid		: 2
initial apicid	: 2
fpu		: yes
fpu_exception	: yes
cpuid level	: 32
wp		: yes
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts mmx fxsr sse sse2 ss ht syscall nx pdpe1gb rdtscp lm constant_tsc pebs bts rep_good nopl xtopology nonstop_tsc cpuid tsc_known_freq pni pclmulqdq dtes64 vmx ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves avx_vnni avx512_bf16 wbnoinvd arat vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq rdpid bus_lock_detect cldemote movdiri movdir64b fsrm md_clear serialize tsxldtrk amx_bf16 avx512_fp16 amx_tile amx_int8 flush_l1d arch_capabilities
vmx flags	: vnmi preemption_timer posted_intr invvpid ept_x_only ept_ad ept_1gb ept_5level flexpriority apicv tsc_offset vtpr mtf vapic ept vpid unrestricted_guest vapic_reg vid shadow_vmcs pml tsc_scaling
bugs		: spectre_v1 spectre_v2 spec_store_bypass swapgs taa eibrs_pbrsb bhi ibpb_no_ret
bogomips	: 4800.00
clflush size	: 64
cache_alignment	: 64
address sizes	: 46 bits physical, 57 bits virtual
power management:

processor	: 3
vendor_id	: GenuineIntel
cpu family	: 6
model		: 207
model name	: Intel(R) Xeon(R) Processor
stepping	: 2
microcode	: 0x1
cpu MHz		: 2400.000
cache size	: 327680 KB
physical id	: 0
siblings	: 4
core id		: 3
cpu cores	: 4
apicid		: 3
initial apicid	: 3
fpu		: yes
fpu_exception	: yes
cpuid level	: 32
wp		: yes
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts mmx fxsr sse sse2 ss ht syscall nx pdpe1gb rdtscp lm constant_tsc pebs bts rep_good nopl xtopology nonstop_tsc cpuid tsc_known_freq pni pclmulqdq dtes64 vmx ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm abm 3dnowprefetch cpuid_fault ssbd ibrs ibpb stibp ibrs_enhanced tpr_shadow flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves avx_vnni avx512_bf16 wbnoinvd arat vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq rdpid bus_lock_detect cldemote movdiri movdir64b fsrm md_clear serialize tsxldtrk amx_bf16 avx512_fp16 amx_tile amx_int8 flush_l1d arch_capabilities
vmx flags	: vnmi preemption_timer posted_intr invvpid ept_x_only ept_ad ept_1gb ept_5level flexpriority apicv tsc_offset vtpr mtf vapic ept vpid unrestricted_guest vapic_reg vid shadow_vmcs pml tsc_scaling
bugs		: spectre_v1 spectre_v2 spec_store_bypass swapgs taa eibrs_pbrsb bhi ibpb_no_ret
bogomips	: 4800.00
clflush size	: 64
cache_alignment	: 64
address sizes	: 46 bits physical, 57 bits virtual
power management:

```

### `cat /proc/meminfo`
```
MemTotal:       16398384 kB
MemFree:        14749576 kB
MemAvailable:   15592212 kB
Buffers:           27752 kB
Cached:          1066076 kB
SwapCached:            0 kB
Active:            96072 kB
Inactive:        1405184 kB
Active(anon):        244 kB
Inactive(anon):   425528 kB
Active(file):      95828 kB
Inactive(file):   979656 kB
Unevictable:          16 kB
Mlocked:              16 kB
SwapTotal:             0 kB
SwapFree:              0 kB
Zswap:                 0 kB
Zswapped:              0 kB
Dirty:               280 kB
Writeback:             0 kB
AnonPages:        407668 kB
Mapped:           326032 kB
Shmem:             18356 kB
KReclaimable:      41024 kB
Slab:              68348 kB
SReclaimable:      41024 kB
SUnreclaim:        27324 kB
KernelStack:        3896 kB
PageTables:         8244 kB
SecPageTables:         0 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:     8199192 kB
Committed_AS:    1793136 kB
VmallocTotal:   34359738367 kB
VmallocUsed:        5568 kB
VmallocChunk:          0 kB
Percpu:             1232 kB
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
FileHugePages:         0 kB
FilePmdMapped:         0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:               0 kB
DirectMap4k:       51200 kB
DirectMap2M:     4143104 kB
DirectMap1G:    14680064 kB
```

### CPU sysfs (smt / cpufreq)
```
smt/active: 0
scaling_governor: (missing)
cpuinfo_max_freq: (missing)
```

### NVIDIA / GPU
```
(no /dev/nvidia* devices)
```

### `cat /etc/os-release`
```
PRETTY_NAME="Ubuntu 24.04.4 LTS"
NAME="Ubuntu"
VERSION_ID="24.04"
VERSION="24.04.4 LTS (Noble Numbat)"
VERSION_CODENAME=noble
ID=ubuntu
ID_LIKE=debian
HOME_URL="https://www.ubuntu.com/"
SUPPORT_URL="https://help.ubuntu.com/"
BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
UBUNTU_CODENAME=noble
LOGO=ubuntu-logo
```
