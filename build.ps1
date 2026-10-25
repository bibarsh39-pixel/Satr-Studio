<#
.SYNOPSIS
Builds the native Windows x64 Satr application and copies its runtime assets.
.DESCRIPTION
Run from any directory. Defaults use the verified development dependencies in
this workspace's work directory and the installed Code::Blocks GCC 14.2 toolchain.
The script does not download dependencies or change installed application files.

Requires matching wxWidgets 3.2.7 Unicode MinGW headers, setup.h, import library,
and DLL. The supplied wx-config/wx/setup.h sets WXWIN_COMPATIBILITY_3_0=0 to match
the runtime. The import library must mark data exports correctly: the shipped
wx DLL is UPX-packed, so gendef on the packed DLL alone is insufficient.

The exact prepared library is work/build-native/libwxmsw32u.a. It was generated
by unpacking a workspace copy of the DLL with official UPX 5.0.2, running gendef
on that unpacked copy, restoring the original DLL name in the DEF, then running
llvm-dlltool -m i386:x86-64 -d <def> -D wxmsw32u_gcc_custom.dll -l <archive>.
The delivered runtime DLL itself remains unchanged. Source dependencies:
https://github.com/wxWidgets/wxWidgets/releases/tag/v3.2.7

All build intermediates stay in a unique subdirectory of BuildDirectory. Its
path must contain ASCII characters because the installed GNU linker cannot
write an executable to an Arabic path. The finished files can be copied into
an Arabic OutputDirectory using PowerShell's Unicode file APIs.

Static GCC/stdC++/pthread runtime linking avoids extra MinGW DLL dependencies;
wxWidgets remains dynamically linked. The resource embeds the icon, version,
Windows common controls and DPI manifest. Fonts, locale, examples and notices
are copied alongside the executable when present. No installer is required.

.EXAMPLE
.\build.ps1 -OutputDirectory .\bin
.EXAMPLE
.\build.ps1 -WxInclude C:\deps\wx\include -WxConfig C:\deps\wx\lib\gcc_dll\mswu `
  -WxLib C:\deps\wx\lib\gcc_dll\libwxmsw32u.a `
  -WxRuntime C:\deps\wx\lib\gcc_dll\wxmsw32u_gcc_custom.dll `
  -Compiler 'C:\Program Files\CodeBlocks\MinGW\bin\g++.exe' `
  -BuildDirectory C:\build\satr -OutputDirectory .\bin
#>
[CmdletBinding()]
param(
    [string]$WxInclude = (Join-Path $PSScriptRoot '..\..\work\wxWidgets-3.2.7\include'),
    [string]$WxConfig = (Join-Path $PSScriptRoot '..\..\work\build-native\wx-config'),
    [string]$WxLib = (Join-Path $PSScriptRoot '..\..\work\build-native\libwxmsw32u.a'),
    [string]$WxRuntime = (Join-Path $PSScriptRoot '..\..\work\build-native\wxmsw32u_gcc_custom.dll'),
    [string]$Compiler = 'C:\Program Files\CodeBlocks\MinGW\bin\g++.exe',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\..\work\satr-build'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'bin')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function ExistingPath([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path. Supply its explicit build parameter; see Get-Help .\build.ps1 -Full."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

$Compiler = ExistingPath $Compiler 'C++ compiler'
$WxInclude = ExistingPath $WxInclude 'wxWidgets include directory'
$WxConfig = ExistingPath $WxConfig 'wxWidgets configuration directory'
if (Test-Path -LiteralPath $WxLib -PathType Container) {
    $WxLib = Join-Path $WxLib 'libwxmsw32u.a'
}
$WxLib = ExistingPath $WxLib 'wxWidgets import archive'
$WxRuntime = ExistingPath $WxRuntime 'wxWidgets runtime DLL'
$null = ExistingPath (Join-Path $WxInclude 'wx\wx.h') 'wxWidgets primary header'
$null = ExistingPath (Join-Path $WxConfig 'wx\setup.h') 'wxWidgets ABI setup header'

$sourceDirectory = Join-Path $PSScriptRoot 'src'
$thirdParty = Join-Path $PSScriptRoot 'third_party'
$mainSource = ExistingPath (Join-Path $sourceDirectory 'main.cpp') 'Satr main source'
$diagnosticsSource = ExistingPath (Join-Path $sourceDirectory 'diagnostics.cpp') 'Satr diagnostics source'
$null = ExistingPath (Join-Path $thirdParty 'nlohmann\json.hpp') 'JSON header'

$BuildDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if ($BuildDirectory -match '[^\x00-\x7F]') {
    throw 'BuildDirectory must use ASCII characters for this GCC linker. OutputDirectory may contain Arabic.'
}
$null = New-Item -ItemType Directory -Path $BuildDirectory -Force
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$stageDirectory = Join-Path $BuildDirectory ('build-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $stageDirectory
$stageExecutable = Join-Path $stageDirectory 'Satr.exe'
$compilerDirectory = Split-Path -Parent $Compiler
$oldPath = $env:PATH

try {
    # Tool subprocesses (windres preprocessor, linker) use the same toolchain.
    $env:PATH = $compilerDirectory + [IO.Path]::PathSeparator + $oldPath
    $inputs = @($mainSource, $diagnosticsSource)
    $resourceSource = Join-Path $sourceDirectory 'Satr.rc'
    if (Test-Path -LiteralPath $resourceSource) {
        $windres = ExistingPath (Join-Path $compilerDirectory 'windres.exe') 'Resource compiler'
        $resourceObject = Join-Path $stageDirectory 'Satr.res'
        Push-Location -LiteralPath $sourceDirectory
        try {
            & $windres '--input-format=rc' '--output-format=coff' '--target=pe-x86-64' `
                '--codepage=65001' '-i' 'Satr.rc' '-o' $resourceObject
            if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed with exit code $LASTEXITCODE." }
        }
        finally { Pop-Location }
        $inputs += $resourceObject
    }
    elseif (Test-Path -LiteralPath (Join-Path $sourceDirectory 'Satr.res')) {
        $inputs += (Join-Path $sourceDirectory 'Satr.res')
    }

    # Absolute include paths and argument arrays preserve spaces and prevent
    # PowerShell or shell interpolation from changing compiler arguments.
    $arguments = @(
        '-std=c++17', '-O2', '-s', '-Wall', '-Wextra', '-m64', '-mthreads',
        '-D__WXMSW__', '-DWXUSINGDLL', '-DUNICODE', '-D_UNICODE',
        ('-I' + $WxInclude.Replace('\', '/')),
        ('-I' + $WxConfig.Replace('\', '/')),
        ('-I' + $thirdParty.Replace('\', '/'))
    ) + $inputs + @(
        $WxLib, '-mwindows', '-static', '-static-libgcc', '-static-libstdc++',
        '-Wl,--no-insert-timestamp', '-o', $stageExecutable
    )
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "Satr compilation failed with exit code $LASTEXITCODE. Intermediates: $stageDirectory" }
    if (-not (Test-Path -LiteralPath $stageExecutable)) { throw 'Compiler returned without producing Satr.exe.' }

    $targetExecutable = Join-Path $OutputDirectory 'Satr.exe'
    Copy-Item -LiteralPath $stageExecutable -Destination $targetExecutable -Force
    $runnerSource = Join-Path $sourceDirectory 'console_runner.cpp'
    if (Test-Path -LiteralPath $runnerSource) {
        $runnerExecutable = Join-Path $stageDirectory 'SatrRunner.exe'
        & $Compiler '-std=c++17' '-O2' '-s' '-Wall' '-Wextra' '-municode' '-static' '-static-libgcc' '-static-libstdc++' ('-I'+$thirdParty) $runnerSource '-o' $runnerExecutable
        if ($LASTEXITCODE -ne 0) { throw 'Console runner compilation failed.' }
        Copy-Item -LiteralPath $runnerExecutable -Destination (Join-Path $OutputDirectory 'SatrRunner.exe') -Force
    }
    $targetRuntime = Join-Path $OutputDirectory (Split-Path -Leaf $WxRuntime)
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals($WxRuntime, $targetRuntime)) {
        Copy-Item -LiteralPath $WxRuntime -Destination $targetRuntime -Force
    }
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals(
            [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd([char[]]'/\'),
            $OutputDirectory.TrimEnd([char[]]'/\'))) {
        foreach ($folder in @('fonts', 'locale', 'examples', 'licenses', 'assistant', 'setup', 'toolchain')) {
            $source = Join-Path $PSScriptRoot $folder
            if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $OutputDirectory -Recurse -Force }
        }
        $notices = Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md'
        if (Test-Path -LiteralPath $notices) { Copy-Item -LiteralPath $notices -Destination $OutputDirectory -Force }
    }
    Write-Host "Built: $targetExecutable"
    Write-Host "Build files: $stageDirectory"
    Get-Item -LiteralPath $targetExecutable
}
finally { $env:PATH = $oldPath }
