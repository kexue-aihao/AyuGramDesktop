param(
    [ValidateRange(1, 128)]
    [int] $MinimumGB = 8,
    [ValidateRange(2, 256)]
    [int] $MaximumGB = 48,
    [ValidateRange(1, 64)]
    [int] $ReserveGB = 10,
    [switch] $ReportOnly
)

$ErrorActionPreference = 'Stop'

function Get-Capacity {
    $os = Get-CimInstance Win32_OperatingSystem -Property TotalVirtualMemorySize, FreeVirtualMemory
    $pagefiles = @(Get-CimInstance Win32_PageFileUsage -ErrorAction SilentlyContinue |
        Select-Object Name, @{ Name = 'AllocatedMB'; Expression = { $_.AllocatedBaseSize } },
            @{ Name = 'PeakMB'; Expression = { $_.PeakUsage } })
    $disks = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' |
        Select-Object DeviceID, @{ Name = 'FreeGB'; Expression = { [math]::Round($_.FreeSpace / 1GB, 1) } },
            @{ Name = 'SizeGB'; Expression = { [math]::Round($_.Size / 1GB, 1) } })
    return [pscustomobject]@{
        CommitLimitGB = [math]::Round($os.TotalVirtualMemorySize / 1MB, 1)
        CommitFreeGB = [math]::Round($os.FreeVirtualMemory / 1MB, 1)
        PageFiles = $pagefiles
        Disks = $disks
    }
}

function Show-Capacity($capacity, [string] $title) {
    Write-Host "--- $title ---"
    Write-Host ("Commit limit: {0} GB; commit available: {1} GB" -f $capacity.CommitLimitGB, $capacity.CommitFreeGB)
    foreach ($pagefile in $capacity.PageFiles) {
        Write-Host ("Page file: {0}; allocated {1} MB; peak {2} MB" -f $pagefile.Name, $pagefile.AllocatedMB, $pagefile.PeakMB)
    }
    foreach ($disk in $capacity.Disks) {
        Write-Host ("Disk {0} {1} GB free of {2} GB" -f $disk.DeviceID, $disk.FreeGB, $disk.SizeGB)
    }
}

Show-Capacity (Get-Capacity) 'Runner capacity before'

if ($ReportOnly) {
    return
}

# Adapted from al-cheb/configure-pagefile-action (MIT): NtCreatePagingFile adds
# or resizes a paging file on a live system, without the reboot that registry
# changes require.
$source = @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Util
{
    class NativeMethods
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct LUID
        {
            internal uint LowPart;
            internal uint HighPart;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct LUID_AND_ATTRIBUTES
        {
            internal LUID Luid;
            internal uint Attributes;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct TOKEN_PRIVILEGE
        {
            internal uint PrivilegeCount;
            internal LUID_AND_ATTRIBUTES Privilege;
            internal static readonly uint Size = (uint)Marshal.SizeOf(typeof(TOKEN_PRIVILEGE));
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct UNICODE_STRING
        {
            internal UInt16 length;
            internal UInt16 maximumLength;
            internal string buffer;
        }

        [DllImport("advapi32.dll", ExactSpelling = true, CharSet = CharSet.Unicode, SetLastError = true, PreserveSig = false)]
        internal static extern bool LookupPrivilegeValueW(
            [In] string lpSystemName,
            [In] string lpName,
            [Out] out LUID luid
        );

        [DllImport("advapi32.dll", SetLastError = true, PreserveSig = false)]
        internal static extern bool AdjustTokenPrivileges(
            [In] SafeCloseHandle tokenHandle,
            [In] bool disableAllPrivileges,
            [In] ref TOKEN_PRIVILEGE newState,
            [In] uint bufferLength,
            [Out] out TOKEN_PRIVILEGE previousState,
            [Out] out uint returnLength
        );

        [DllImport("advapi32.dll", CharSet = CharSet.Auto, SetLastError = true, PreserveSig = false)]
        internal static extern bool OpenProcessToken(
            [In] IntPtr processToken,
            [In] int desiredAccess,
            [Out] out SafeCloseHandle tokenHandle
        );

        [DllImport("ntdll.dll", CharSet = CharSet.Unicode, SetLastError = true, CallingConvention = CallingConvention.StdCall)]
        internal static extern Int32 NtCreatePagingFile(
            [In] ref UNICODE_STRING pageFileName,
            [In] ref Int64 minimumSize,
            [In] ref Int64 maximumSize,
            [In] UInt32 flags
        );

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern uint QueryDosDeviceW(
            string lpDeviceName,
            StringBuilder lpTargetPath,
            int ucchMax
        );
    }

    public sealed class SafeCloseHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
        private extern static bool CloseHandle(IntPtr handle);

        private SafeCloseHandle() : base(true)
        {
        }

        public SafeCloseHandle(IntPtr preexistingHandle, bool ownsHandle) : base(ownsHandle)
        {
            SetHandle(preexistingHandle);
        }

        override protected bool ReleaseHandle()
        {
            return CloseHandle(handle);
        }
    }

    public class PageFile
    {
        public static void SetPageFileSize(long minimumValue, long maximumValue, string lpDeviceName)
        {
            SetPageFilePrivilege();
            StringBuilder targetPath = new StringBuilder(260);
            if (NativeMethods.QueryDosDeviceW(lpDeviceName, targetPath, targetPath.Capacity) == 0)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            string pageFilePath = targetPath.ToString() + "\\pagefile.sys";
            NativeMethods.UNICODE_STRING pageFileName = new NativeMethods.UNICODE_STRING
            {
                length = (ushort)(pageFilePath.Length * 2),
                maximumLength = (ushort)(2 * (pageFilePath.Length + 1)),
                buffer = pageFilePath
            };
            Int32 result = NativeMethods.NtCreatePagingFile(ref pageFileName, ref minimumValue, ref maximumValue, 0);
            if (result != 0)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }

        static void SetPageFilePrivilege()
        {
            const int SE_PRIVILEGE_ENABLED = 0x00000002;
            const int AdjustPrivileges = 0x00000020;
            const int Query = 0x00000008;
            NativeMethods.LUID luid;
            NativeMethods.LookupPrivilegeValueW(null, "SeCreatePagefilePrivilege", out luid);
            SafeCloseHandle tokenHandle;
            NativeMethods.OpenProcessToken(Process.GetCurrentProcess().Handle, AdjustPrivileges | Query, out tokenHandle);
            NativeMethods.TOKEN_PRIVILEGE previousState;
            NativeMethods.TOKEN_PRIVILEGE newState;
            uint previousSize = 0;
            newState.PrivilegeCount = 1;
            newState.Privilege.Luid = luid;
            newState.Privilege.Attributes = SE_PRIVILEGE_ENABLED;
            NativeMethods.AdjustTokenPrivileges(tokenHandle, false, ref newState, NativeMethods.TOKEN_PRIVILEGE.Size, out previousState, out previousSize);
        }
    }
}
'@

Add-Type -TypeDefinition $source

$capacity = Get-Capacity
$candidates = @($capacity.Disks | Where-Object {
    $_.FreeGB -ge ($MinimumGB + $ReserveGB)
} | Sort-Object FreeGB -Descending)
if (-not $candidates) {
    Write-Host "No fixed disk has $($MinimumGB + $ReserveGB) GB free; leaving the page file unchanged."
    return
}

$extended = $false
foreach ($disk in $candidates) {
    $sizeGB = [math]::Min($MaximumGB, [math]::Floor($disk.FreeGB) - $ReserveGB)
    if ($sizeGB -lt $MinimumGB) {
        continue
    }
    try {
        [Util.PageFile]::SetPageFileSize(
            [long]($MinimumGB * 1GB),
            [long]($sizeGB * 1GB),
            [string]$disk.DeviceID)
        Write-Host ("Extended the page file on {0}: {1} GB initial, {2} GB maximum." -f $disk.DeviceID, $MinimumGB, $sizeGB)
        $extended = $true
        break
    } catch {
        Write-Host ("Could not extend the page file on {0}: {1}" -f $disk.DeviceID, $_.Exception.Message)
    }
}

if (-not $extended) {
    Write-Host 'The page file was left unchanged.'
}

Show-Capacity (Get-Capacity) 'Runner capacity after'
