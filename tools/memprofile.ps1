# Profileur memoire : parcourt l espace d adressage du processus avec
# VirtualQueryEx et classe les regions engagees par type, comme le fait VMMap.
# Attribue en plus les regions MEM_IMAGE aux modules charges.
param([int]$ProcessId, [string]$Label = "")

Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class MemProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORY_BASIC_INFORMATION {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public uint __align1;
        public IntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
        public uint __align2;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_MEMORY_COUNTERS_EX {
        public uint cb;
        public uint PageFaultCount;
        public IntPtr PeakWorkingSetSize;
        public IntPtr WorkingSetSize;
        public IntPtr QuotaPeakPagedPoolUsage;
        public IntPtr QuotaPagedPoolUsage;
        public IntPtr QuotaPeakNonPagedPoolUsage;
        public IntPtr QuotaNonPagedPoolUsage;
        public IntPtr PagefileUsage;
        public IntPtr PeakPagefileUsage;
        public IntPtr PrivateUsage;
    }

    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool inh, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr,
        out MEMORY_BASIC_INFORMATION mbi, IntPtr len);
    [DllImport("psapi.dll")] public static extern bool GetProcessMemoryInfo(IntPtr h,
        out PROCESS_MEMORY_COUNTERS_EX c, uint cb);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr h, uint flags);

    public const uint PROCESS_QUERY_INFORMATION = 0x0400;
    public const uint PROCESS_VM_READ = 0x0010;

    public const uint MEM_COMMIT  = 0x1000;
    public const uint MEM_RESERVE = 0x2000;
    public const uint MEM_IMAGE   = 0x1000000;
    public const uint MEM_MAPPED  = 0x40000;
    public const uint MEM_PRIVATE = 0x20000;

    public class Region {
        public ulong Base; public ulong AllocBase; public ulong Size;
        public uint State; public uint Type; public uint Protect;
    }

    public static List<Region> Walk(IntPtr h) {
        var list = new List<Region>();
        ulong addr = 0;
        int sz = Marshal.SizeOf(typeof(MEMORY_BASIC_INFORMATION));
        while (addr < 0x00007FFFFFFF0000UL) {
            MEMORY_BASIC_INFORMATION mbi;
            IntPtr got = VirtualQueryEx(h, (IntPtr)(long)addr, out mbi, (IntPtr)sz);
            if (got == IntPtr.Zero) break;
            ulong rsz = (ulong)mbi.RegionSize.ToInt64();
            if (rsz == 0) break;
            list.Add(new Region {
                Base = (ulong)mbi.BaseAddress.ToInt64(),
                AllocBase = (ulong)mbi.AllocationBase.ToInt64(),
                Size = rsz, State = mbi.State, Type = mbi.Type, Protect = mbi.Protect
            });
            addr += rsz;
        }
        return list;
    }
}
'@ -ErrorAction SilentlyContinue

function MB($bytes) { return [math]::Round($bytes / 1MB, 2) }

$proc = Get-Process -Id $ProcessId -ErrorAction Stop
$h = [MemProbe]::OpenProcess(([MemProbe]::PROCESS_QUERY_INFORMATION -bor [MemProbe]::PROCESS_VM_READ), $false, $ProcessId)
if ($h -eq [IntPtr]::Zero) { "echec OpenProcess"; exit 1 }

$c = New-Object MemProbe+PROCESS_MEMORY_COUNTERS_EX
$c.cb = [uint32][System.Runtime.InteropServices.Marshal]::SizeOf($c)
[void][MemProbe]::GetProcessMemoryInfo($h, [ref]$c, $c.cb)

$regions = [MemProbe]::Walk($h)

# Bases des modules charges, pour attribuer les regions image.
$modBase = @{}
foreach ($m in $proc.Modules) { $modBase[[uint64]$m.BaseAddress.ToInt64()] = $m }

$imageBytes = [uint64]0; $mappedBytes = [uint64]0; $privateBytes = [uint64]0; $reservedBytes = [uint64]0
$perModule = @{}
$privateRegions = New-Object System.Collections.ArrayList

foreach ($r in $regions) {
    if ($r.State -eq [MemProbe]::MEM_RESERVE) { $reservedBytes += $r.Size; continue }
    if ($r.State -ne [MemProbe]::MEM_COMMIT) { continue }

    switch ($r.Type) {
        ([MemProbe]::MEM_IMAGE) {
            $imageBytes += $r.Size
            $name = "(image inconnue)"
            if ($modBase.ContainsKey($r.AllocBase)) { $name = $modBase[$r.AllocBase].ModuleName }
            if (-not $perModule.ContainsKey($name)) { $perModule[$name] = [uint64]0 }
            $perModule[$name] += $r.Size
        }
        ([MemProbe]::MEM_MAPPED) { $mappedBytes += $r.Size }
        ([MemProbe]::MEM_PRIVATE) {
            $privateBytes += $r.Size
            [void]$privateRegions.Add($r)
        }
    }
}

""
"================ $Label (pid $ProcessId) ================"
"Working set            : $(MB $c.WorkingSetSize.ToInt64()) Mo"
"Private (commit charge): $(MB $c.PrivateUsage.ToInt64()) Mo"
"Pagefile usage         : $(MB $c.PagefileUsage.ToInt64()) Mo"
"Objets GDI             : $([MemProbe]::GetGuiResources($h, 0))"
"Objets USER            : $([MemProbe]::GetGuiResources($h, 1))"
""
"--- Repartition du commit (VirtualQuery) ---"
"  Image  (code+donnees des DLL, partageable) : $(MB $imageBytes) Mo"
"  Mapped (fichiers projetes, partageable)    : $(MB $mappedBytes) Mo"
"  Private (heap, piles, allocations pilote)  : $(MB $privateBytes) Mo"
"  Reserve (non engage, ne coute rien)        : $(MB $reservedBytes) Mo"
"  TOTAL engage                               : $(MB ($imageBytes + $mappedBytes + $privateBytes)) Mo"
""
"--- Top 15 modules par octets image engages ---"
$perModule.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 15 | ForEach-Object {
    "  {0,-34} {1,8} Mo" -f $_.Key, (MB $_.Value)
}
"  ({0} modules charges au total)" -f $proc.Modules.Count
""
"--- Top 12 regions privees ---"
$names = @{ 0x02 = "READONLY"; 0x04 = "READWRITE"; 0x08 = "WRITECOPY"; 0x20 = "EXECUTE_READ";
            0x40 = "EXECUTE_READWRITE"; 0x404 = "READWRITE|WRITECOMBINE (memoire GPU)";
            0x104 = "READWRITE|GUARD"; 0x01 = "NOACCESS" }
$privateRegions | Sort-Object Size -Descending | Select-Object -First 12 | ForEach-Object {
    $pn = $names[[int]$_.Protect]; if (-not $pn) { $pn = "0x{0:X}" -f $_.Protect }
    "  0x{0:X12}  {1,8} Mo  {2}" -f $_.Base, (MB $_.Size), $pn
}
""
"--- Regions privees agregees par protection ---"
$privateRegions | Group-Object Protect | Sort-Object { ($_.Group | Measure-Object Size -Sum).Sum } -Descending | ForEach-Object {
    $sum = ($_.Group | Measure-Object Size -Sum).Sum
    $pn = $names[[int]$_.Name]; if (-not $pn) { $pn = "0x{0:X}" -f [int]$_.Name }
    "  {0,-40} {1,8} Mo  ({2} regions)" -f $pn, (MB $sum), $_.Count
}
[void][MemProbe]::CloseHandle($h)
