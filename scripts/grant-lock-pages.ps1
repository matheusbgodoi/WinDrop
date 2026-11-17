#Requires -RunAsAdministrator
# Gives one account the "Lock pages in memory" right, which the radio daemon
# needs for the memory the card reads and writes directly. It takes effect at
# the account's next logon. -Remove takes the right away again.
#   grant-lock-pages.ps1 [-Account DOMAIN\name] [-Remove]
param([string]$Account = [Security.Principal.WindowsIdentity]::GetCurrent().Name, [switch]$Remove)
$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LsaRights {
  [StructLayout(LayoutKind.Sequential)] struct Attributes { public int Length; public IntPtr a, b; public int c; public IntPtr d, e; }
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  struct Text { public ushort Length, Maximum; [MarshalAs(UnmanagedType.LPWStr)] public string Buffer; }
  [DllImport("advapi32.dll")] static extern uint LsaOpenPolicy(IntPtr system, ref Attributes attributes, int access, out IntPtr policy);
  [DllImport("advapi32.dll")] static extern uint LsaAddAccountRights(IntPtr policy, byte[] sid, Text[] rights, int count);
  [DllImport("advapi32.dll")] static extern uint LsaRemoveAccountRights(IntPtr policy, byte[] sid, bool all, Text[] rights, int count);
  [DllImport("advapi32.dll")] static extern uint LsaClose(IntPtr policy);
  [DllImport("advapi32.dll")] static extern int LsaNtStatusToWinError(uint status);
  public static int Change(byte[] sid, string right, bool remove) {
    var attributes = new Attributes();
    IntPtr policy;
    uint status = LsaOpenPolicy(IntPtr.Zero, ref attributes, 0x811 /* lookup names, create account, view */, out policy);
    if (status != 0) return LsaNtStatusToWinError(status);
    var rights = new[] { new Text { Buffer = right, Length = (ushort)(right.Length * 2), Maximum = (ushort)(right.Length * 2 + 2) } };
    status = remove ? LsaRemoveAccountRights(policy, sid, false, rights, 1) : LsaAddAccountRights(policy, sid, rights, 1);
    LsaClose(policy);
    return status == 0 ? 0 : LsaNtStatusToWinError(status);
  }
}
'@

$sid = (New-Object Security.Principal.NTAccount $Account).Translate([Security.Principal.SecurityIdentifier])
$bytes = New-Object byte[] $sid.BinaryLength
$sid.GetBinaryForm($bytes, 0)
$error = [LsaRights]::Change($bytes, 'SeLockMemoryPrivilege', $Remove)
if ($error) { throw "The right could not be changed for $Account (Windows error $error)." }
if ($Remove) { "$Account no longer has 'Lock pages in memory'." }
else { "$Account has 'Lock pages in memory' from its next logon on." }
