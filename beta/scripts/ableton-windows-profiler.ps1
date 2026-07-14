param(
    [string]$OutputDir = [Environment]::GetFolderPath("Desktop")
)

$ErrorActionPreference = "SilentlyContinue"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$out = Join-Path $OutputDir "ableton-beta-windows-$stamp.txt"
$idDir = Join-Path $(if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $env:USERPROFILE }) "Ableton-Beta"
$testerIdFile = Join-Path $idDir "tester-id-v2"
$machineIdFile = Join-Path $idDir "machine-id"
New-Item -ItemType Directory -Force -Path $OutputDir,$idDir | Out-Null

if (Test-Path -LiteralPath $testerIdFile) {
    $testerId = (Get-Content -LiteralPath $testerIdFile -TotalCount 1).Trim()
} else {
    $testerId = "BETA-$([guid]::NewGuid().ToString('N').Substring(0, 20).ToUpperInvariant())"
    Set-Content -LiteralPath $testerIdFile -Value $testerId -Encoding ASCII
}

if (Test-Path -LiteralPath $machineIdFile) {
    $machineId = (Get-Content -LiteralPath $machineIdFile -TotalCount 1).Trim()
} else {
    $machineId = [guid]::NewGuid().ToString()
    Set-Content -LiteralPath $machineIdFile -Value $machineId -Encoding ASCII
}

function Header([string]$Name) {
    Add-Content -LiteralPath $out ""
    Add-Content -LiteralPath $out "[$Name]"
}

function Redact([AllowEmptyString()][string]$Text) {
    if ($Text -match '(?i)^\s*[^:=]*(serial|uuid|udid|wwn|guid|unique[ _-]?id|asset[ _-]?tag|processorid|identifyingnumber|instanceid|pnpdeviceid|address|location[ _-]?id|mount[ _-]?point|device[ _-]?identifier)[^:=]*[:=]') {
        return ""
    }
    foreach ($value in @($env:USERPROFILE, $env:HOME)) {
        if ($value -and [System.IO.Path]::IsPathRooted($value)) {
            $Text = $Text -replace [regex]::Escape($value), "<HOME>"
        }
    }
    $Text = $Text -replace '(?i)C:\\Users\\[^\\\s]+', 'C:\Users\<USER>'
    $Text = $Text -replace '(?i)\b[0-9a-f]{2}(?:[:-][0-9a-f]{2}){5}\b', '<MAC>'
    $Text = $Text -replace '(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b', '<EMAIL>'
    $Text = $Text -replace '(?i)\b(password|passwd|token|secret|api[ _-]?key|machineguid|unlock\.json|ableton[ _-]?(?:serial|licen[cs]e)|licen[cs]e[ _-]?key)\b[^\r\n]*', '$1=<REDACTED>'
    return $Text
}

function Write-Text {
    param(
        [Parameter(ValueFromPipeline = $true)]
        $Value
    )
    process {
        ($Value | Out-String -Width 200) -split "`r?`n" |
            ForEach-Object { (Redact $_).TrimEnd() } |
            Where-Object { $_ -ne "" } |
            Add-Content -LiteralPath $out
    }
}

"collected_utc=$([DateTime]::UtcNow.ToString('o'))" |
    Set-Content -LiteralPath $out -Encoding UTF8

Header "IDENTIFIERS"
@(
    "tester_id=$testerId",
    "machine_id=$machineId"
) | Write-Text

Header "SYSTEM"
Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption,Version,BuildNumber,OSArchitecture |
    Write-Text
Get-CimInstance Win32_Processor |
    Select-Object Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors |
    Write-Text
Get-CimInstance Win32_ComputerSystem |
    Select-Object Manufacturer,Model,SystemType,@{n="RAM_GB";e={[math]::Round($_.TotalPhysicalMemory / 1GB, 1)}} |
    Write-Text

Header "PLATFORM_HARDWARE"
Get-CimInstance Win32_ComputerSystemProduct |
    Select-Object Vendor,Name,Version,SKUNumber |
    Write-Text
Get-CimInstance Win32_BaseBoard |
    Select-Object Manufacturer,Product,Version |
    Write-Text
Get-CimInstance Win32_BIOS |
    Select-Object Manufacturer,SMBIOSBIOSVersion,ReleaseDate |
    Write-Text

Header "STORAGE"
Get-CimInstance Win32_DiskDrive |
    Select-Object Model,Manufacturer,InterfaceType,MediaType,FirmwareRevision,
        @{n="Size_GB";e={[math]::Round($_.Size / 1GB, 1)}} |
    Write-Text

Header "DISPLAY"
Get-CimInstance Win32_VideoController |
    Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution |
    Write-Text
Get-CimInstance Win32_DesktopMonitor |
    Select-Object Name,MonitorManufacturer,MonitorType,ScreenWidth,ScreenHeight |
    Write-Text
Get-ItemProperty "HKCU:\Control Panel\Desktop" LogPixels,Win8DpiScaling |
    Select-Object LogPixels,Win8DpiScaling |
    Write-Text

Header "AUDIO"
Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.PNPClass -in @("MEDIA", "AudioEndpoint") } |
    Select-Object Status,PNPClass,Manufacturer,Description,Service |
    Sort-Object PNPClass,Manufacturer,Description -Unique |
    Write-Text

Header "MIDI"
Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.Name -match "(?i)midi|controller|push|keyboard" } |
    Select-Object Status,PNPClass,Manufacturer,Description,Service |
    Sort-Object Manufacturer,Description -Unique |
    Write-Text

Header "PNP_DEVICES"
Get-CimInstance Win32_PnPEntity |
    Select-Object Status,PNPClass,Manufacturer,Description,Service |
    Sort-Object PNPClass,Manufacturer,Description,Service -Unique |
    Write-Text

Header "ABLETON"
$abletonInstalls = Join-Path $env:ProgramData "Ableton"
if (Test-Path $abletonInstalls) {
    Get-ChildItem $abletonInstalls -Recurse -File -Include "*.exe" |
        Select-Object Name,@{n="Version";e={$_.VersionInfo.FileVersion}} |
        Sort-Object Name,Version -Unique |
        Write-Text
}

$abletonPreferences = Join-Path $env:APPDATA "Ableton"
if (Test-Path $abletonPreferences) {
    Get-ChildItem $abletonPreferences -Recurse -File `
        -Include "Options.txt","PluginScanDb.txt","PluginScanner.txt","Log.txt" |
        Select-Object @{n="File";e={$_.FullName.Substring($abletonPreferences.Length).TrimStart("\")}},Length |
        Sort-Object File -Unique |
        Write-Text

}

Header "PLUGINS"
$pluginRoots = @(
    "$env:ProgramFiles\Common Files\VST3",
    "${env:ProgramFiles(x86)}\Common Files\VST3",
    "$env:LOCALAPPDATA\Programs\Common\VST3",
    "$env:ProgramFiles\Common Files\VST2",
    "$env:ProgramFiles\VSTPlugins",
    "$env:ProgramFiles\Steinberg\VSTPlugins",
    "$env:LOCALAPPDATA\VSTPlugins"
) | Where-Object { Test-Path $_ }

foreach ($root in $pluginRoots) {
    Get-ChildItem $root -Recurse -File |
        Where-Object { $_.Extension -in @(".dll", ".vst3") } |
        Select-Object Name,
            @{n="Product";e={$_.VersionInfo.ProductName}},
            @{n="Version";e={$_.VersionInfo.FileVersion}} |
        Sort-Object Name,Version -Unique |
        Write-Text
}
