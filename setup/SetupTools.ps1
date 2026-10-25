[CmdletBinding()]
param(
    [string]$AppRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$InstallRoot = (Join-Path $env:LOCALAPPDATA 'Satr\tools'),
    [string]$CacheRoot = '',
    [switch]$Offline,
    [switch]$FunctionsOnly
)
$ErrorActionPreference='Stop'
# An app launched by PowerShell 7 can inherit its incompatible PSModulePath.
# Use the Windows PowerShell modules belonging to this process only.
$env:PSModulePath=Join-Path $PSHOME 'Modules'
Set-StrictMode -Version 2
[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
$OutputEncoding=[Console]::OutputEncoding
Add-Type -AssemblyName System.Net.Http
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'packages.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$AppRoot=[IO.Path]::GetFullPath($AppRoot)
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot)
if(!$CacheRoot){$CacheRoot=Join-Path $InstallRoot 'downloads'}
$CacheRoot=[IO.Path]::GetFullPath($CacheRoot)

function Emit([string]$Kind,[string]$Message,[int]$Percent=-1,[bool]$Cancellable=$true) {
    $record=@{kind=$Kind;message=$Message;percent=$Percent;cancellable=$Cancellable}
    $line=$record | ConvertTo-Json -Compress
    if(Test-Path -LiteralPath $InstallRoot){[IO.File]::AppendAllText((Join-Path $InstallRoot 'setup.log'),$line+[Environment]::NewLine,(New-Object Text.UTF8Encoding($false)))}
    [Console]::WriteLine($line)
}
function FullChild([string]$Parent,[string]$Child) {
    if([IO.Path]::IsPathRooted($Child) -or $Child.Contains(':')){throw 'Invalid archive path'}
    $parentPath=[IO.Path]::GetFullPath($Parent).TrimEnd([char[]]'\/')+'\'
    $path=[IO.Path]::GetFullPath((Join-Path $Parent $Child))
    if(!$path.StartsWith($parentPath,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive entry leaves installation directory'}
    return $path
}
function HashFile([string]$Path) {return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function AssertDownloadUri([Uri]$Uri,[bool]$Redirect=$false) {
    $hosts=@('github.com','huggingface.co','download.visualstudio.microsoft.com','www.python.org','nodejs.org')
    if($Redirect){$hosts+=@('release-assets.githubusercontent.com','objects.githubusercontent.com','cdn-lfs.huggingface.co','cdn-lfs.hf.co','cdn-lfs-us-1.hf.co','cas-bridge.xethub.hf.co')}
    if(!$Uri.IsAbsoluteUri -or $Uri.Scheme -ne 'https' -or $Uri.Port -ne 443 -or $Uri.Host -notin $hosts -or $Uri.UserInfo -or $Uri.Fragment){throw 'Untrusted package URL or redirect'}
}
function OpenDownloadResponse($Client,[Uri]$Uri) {
    for($hop=0;$hop -le 5;$hop++){
        AssertDownloadUri $Uri ($hop -gt 0)
        $response=$Client.GetAsync($Uri,[Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        if([int]$response.StatusCode -in @(301,302,303,307,308)){
            try{
                if(!$response.Headers.Location){throw 'Download redirect has no location'}
                $next=New-Object Uri($Uri,$response.Headers.Location)
                AssertDownloadUri $next $true
                $Uri=$next
            }finally{$response.Dispose()}
            continue
        }
        try{$null=$response.EnsureSuccessStatusCode()}catch{$response.Dispose();throw}
        return $response
    }
    throw 'Too many download redirects'
}
function Fetch($Package) {
    $path=Join-Path $CacheRoot ($Package.id+$Package.extension)
    $null=FullChild $CacheRoot ($Package.id+$Package.extension)
    if((Test-Path -LiteralPath $path) -and (HashFile $path) -eq $Package.sha256){return $path}
    if($Offline){throw ('Package unavailable offline: '+$Package.id)}
    $uri=[Uri]$Package.url
    AssertDownloadUri $uri
    Emit progress ('جارٍ تحميل '+$Package.label) 0
    $part=$path+'.part';$handler=New-Object Net.Http.HttpClientHandler;$handler.AllowAutoRedirect=$false;$handler.UseDefaultCredentials=$false
    $client=New-Object Net.Http.HttpClient($handler);$client.Timeout=[TimeSpan]::FromSeconds(45)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('SatrStudio/0.9')
    $response=$null;$stream=$null;$output=$null
    try {
        $response=OpenDownloadResponse $client $uri
        if($response.Content.Headers.ContentLength -and $response.Content.Headers.ContentLength -ne [long]$Package.bytes){throw 'Unexpected download size'}
        $stream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $output=[IO.File]::Open($part,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $buffer=New-Object byte[] 131072;$received=[long]0;$last=[DateTime]::UtcNow
        while($true){
            $cancel=New-Object Threading.CancellationTokenSource
            try{$cancel.CancelAfter(45000);$count=$stream.ReadAsync($buffer,0,$buffer.Length,$cancel.Token).GetAwaiter().GetResult()}finally{$cancel.Dispose()}
            if($count -eq 0){break}
            $received+=$count;if($received -gt [long]$Package.bytes){throw 'Download exceeded expected size'}
            $output.Write($buffer,0,$count)
            if(([DateTime]::UtcNow-$last).TotalMilliseconds -ge 350){$percent=[int]($received*100/[long]$Package.bytes);Emit progress ('تحميل '+$Package.label+' — '+[int]($received/1MB)+' / '+[int]([long]$Package.bytes/1MB)+' MB') $percent;$last=[DateTime]::UtcNow}
        }
        $output.Dispose();$output=$null
        if($received -ne [long]$Package.bytes -or (HashFile $part) -ne $Package.sha256){throw ('Package integrity check failed: '+$Package.id)}
        Move-Item -LiteralPath $part -Destination $path -Force
    } finally {if($output){$output.Dispose()};if($stream){$stream.Dispose()};if($response){$response.Dispose()};$client.Dispose();if(Test-Path -LiteralPath $part){Remove-Item -LiteralPath $part -Force}}
    return $path
}
function ExtractChecked([string]$Archive,[string]$Destination) {
    $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        [long]$total=0
        if($zip.Entries.Count -gt 100000){throw 'Too many archive entries'}
        foreach($entry in $zip.Entries){
            $null=FullChild $Destination $entry.FullName
            if((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000){throw 'Archive symbolic links are not supported'}
            $total+=$entry.Length;if($total -gt 4GB){throw 'Archive expanded size limit exceeded'}
        }
    } finally {$zip.Dispose()}
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive,$Destination)
}
function ToolArguments([string[]]$Items) {
    $quoted=foreach($item in $Items){'"'+[regex]::Replace([regex]::Replace($item,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}
    return ($quoted -join ' ')
}
function RunTool([string]$File,[string[]]$Items,[string]$Directory='') {
    $info=New-Object Diagnostics.ProcessStartInfo;$info.FileName=$File;$info.Arguments=ToolArguments $Items
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    if($Directory){$info.WorkingDirectory=$Directory}
    $process=New-Object Diagnostics.Process;$process.StartInfo=$info
    try {
        $null=$process.Start();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(!$process.WaitForExit(30000)){$process.Kill();throw 'Tool verification timed out'}
        $result=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        if($process.ExitCode -ne 0){throw ('Tool failed ('+$process.ExitCode+'): '+$result)}
        return $result
    } finally {$process.Dispose()}
}
function MoveManagedDirectory([string]$From,[string]$To) {
    $fromPath=FullChild $InstallRoot ([IO.Path]::GetFileName($From))
    $toPath=FullChild $InstallRoot ([IO.Path]::GetFileName($To))
    if($fromPath -ne [IO.Path]::GetFullPath($From) -or $toPath -ne [IO.Path]::GetFullPath($To)){throw 'Invalid managed directory move'}
    for($attempt=0;$attempt -lt 40;$attempt++){
        try{[IO.Directory]::Move($fromPath,$toPath);return}
        catch{if($attempt -eq 39){throw};Start-Sleep -Milliseconds 200}
    }
}
function VerifyPackage([string]$Directory,$Package) {
    $null=RunTool (FullChild $Directory $Package.tool) @('--version')
    if($Package.PSObject.Properties.Name -contains 'companions'){
        foreach($name in $Package.companions){$null=RunTool (FullChild $Directory $name) @('--version')}
    }
}
function InstallArchive($Package) {
    $destination=Join-Path $InstallRoot $Package.id
    $null=FullChild $InstallRoot $Package.id
    $tool=FullChild $destination $Package.tool
    if(Test-Path -LiteralPath $tool){try{VerifyPackage $destination $Package;Emit progress ($Package.label+' جاهز');return $destination}catch{}}
    $archive=Fetch $Package
    Emit progress ('جارٍ تجهيز '+$Package.label)
    $stage=Join-Path $InstallRoot ('.stage-'+$Package.id+'-'+[Guid]::NewGuid().ToString('N'))
    $null=FullChild $InstallRoot ([IO.Path]::GetFileName($stage))
    ExtractChecked $archive $stage
    VerifyPackage $stage $Package
    $backup=''
    if(Test-Path -LiteralPath $destination){$backup=$destination+'.previous-'+[Guid]::NewGuid().ToString('N');MoveManagedDirectory $destination $backup}
    try {MoveManagedDirectory $stage $destination}
    catch {if($backup -and !(Test-Path -LiteralPath $destination)){MoveManagedDirectory $backup $destination};throw}
    return $destination
}
function VisualRuntimePresent {
    $system=[Environment]::GetFolderPath('System')
    return ((Test-Path -LiteralPath (Join-Path $system 'vcruntime140.dll')) -and (Test-Path -LiteralPath (Join-Path $system 'vcruntime140_1.dll')) -and (Test-Path -LiteralPath (Join-Path $system 'msvcp140.dll')))
}
function EnsureVisualRuntime {
    if(VisualRuntimePresent){return}
    $installer=Fetch $manifest.visualCpp
    $signature=Get-AuthenticodeSignature -LiteralPath $installer
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation'){throw 'Microsoft installer signature verification failed'}
    Emit progress 'جارٍ تثبيت Microsoft Visual C++. وافق على طلب ويندوز لإكمال التجهيز.' -1 $false
    $process=Start-Process -FilePath $installer -ArgumentList @('/install','/passive','/norestart') -Verb RunAs -PassThru -Wait
    if($process.ExitCode -notin @(0,3010,1638)){throw ('Microsoft Visual C++ setup failed or was cancelled: '+$process.ExitCode)}
    if(!(VisualRuntimePresent)){throw 'Restart Windows to finish the Visual C++ installation, then retry.'}
    Emit progress 'تم تثبيت Microsoft Visual C++' 100
}
if($FunctionsOnly){return}
$mutex=New-Object Threading.Mutex($false,'Local\SatrStudioToolsSetup');$locked=$false
try {
    try{$locked=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$locked=$true}
    if(!$locked){throw 'Another tools setup is already running'}
    if(![Environment]::Is64BitOperatingSystem){throw 'Satr Studio requires 64-bit Windows 10 or 11'}
    $null=New-Item -ItemType Directory -Path $InstallRoot,$CacheRoot -Force
    Emit progress 'جارٍ فحص الأدوات المطلوبة…'
    $bundled=Join-Path $AppRoot 'toolchain'
    $gcc=Join-Path $bundled ($manifest.gcc.id+'\'+$manifest.gcc.tool)
    if(Test-Path -LiteralPath $gcc){try{$null=RunTool $gcc @('--version')}catch{$gcc=''}}
    if(!$gcc -or !(Test-Path -LiteralPath $gcc)){$gcc=Join-Path (InstallArchive $manifest.gcc) $manifest.gcc.tool}
    $clangd=Join-Path $bundled ($manifest.clangd.id+'\'+$manifest.clangd.tool)
    if(Test-Path -LiteralPath $clangd){try{$null=RunTool $clangd @('--version')}catch{$clangd=''}}
    if(!$clangd -or !(Test-Path -LiteralPath $clangd)){$clangd=Join-Path (InstallArchive $manifest.clangd) $manifest.clangd.tool}
    EnsureVisualRuntime
    $python=Join-Path $bundled 'python-3.14.9\python.exe'
    if(Test-Path -LiteralPath $python){try{$null=RunTool $python @('--version')}catch{$python=''}}
    if(!$python -or !(Test-Path -LiteralPath $python)){$python=Join-Path (InstallArchive $manifest.python) $manifest.python.tool}
    $node=Join-Path $bundled 'node-v24.22.1-win-x64\node.exe'
    if(Test-Path -LiteralPath $node){try{$null=RunTool $node @('--version')}catch{$node=''}}
    if(!$node -or !(Test-Path -LiteralPath $node)){$node=Join-Path (InstallArchive $manifest.node) $manifest.node.tool}
    $javaRoot=Join-Path $bundled $manifest.java.id
    try{VerifyPackage $javaRoot $manifest.java}catch{$javaRoot=InstallArchive $manifest.java}
    $javac=Join-Path $javaRoot $manifest.java.tool
    $java=Join-Path (Split-Path -Parent $javac) 'java.exe'
    if((RunTool $python @('-X','utf8','-c','import sqlite3,ssl,json; print("SATR_PYTHON_OK")')).Trim() -ne 'SATR_PYTHON_OK'){throw 'Python verification failed'}
    if((RunTool $node @('-e','const fs=require("node:fs"); console.log("SATR_JS_OK")')).Trim() -ne 'SATR_JS_OK'){throw 'JavaScript verification failed'}
    $runtime=Join-Path $AppRoot 'assistant\llama-completion.exe'
    if(Test-Path -LiteralPath $runtime){try{$null=RunTool $runtime @('--version')}catch{$runtime=''}}
    if(!$runtime -or !(Test-Path -LiteralPath $runtime)){$runtime=Join-Path (InstallArchive $manifest.assistant) $manifest.assistant.tool}
    $model=Join-Path $AppRoot ('assistant\'+$manifest.model.file)
    Emit progress 'جارٍ التحقق من نموذج المساعد…'
    if(!(Test-Path -LiteralPath $model) -or (HashFile $model) -ne $manifest.model.sha256){$model=Fetch $manifest.model}
    Emit progress 'جارٍ تجربة ترجمة وتشغيل برنامج C وبرنامج C++…'
    $probe=Join-Path $InstallRoot ('.verify-'+[Guid]::NewGuid().ToString('N'));$null=New-Item -ItemType Directory -Path $probe
    $c=Join-Path (Split-Path -Parent $gcc) 'gcc.exe'
    [IO.File]::WriteAllText((Join-Path $probe 'test.c'),'#include <stdio.h>'+[Environment]::NewLine+'int main(void){puts("SATR_C_OK");return 0;}')
    [IO.File]::WriteAllText((Join-Path $probe 'test.cpp'),'#include <iostream>'+[Environment]::NewLine+'#include <vector>'+[Environment]::NewLine+'int main(){std::vector<int> v{1,2};std::cout << "SATR_CPP_OK";return v.size()!=2;}')
    $null=RunTool $c @('-std=c17','-static','test.c','-o','test-c.exe') $probe
    $null=RunTool $gcc @('-std=c++17','-static','test.cpp','-o','test-cpp.exe') $probe
    if((RunTool (Join-Path $probe 'test-c.exe') @() $probe).Trim() -ne 'SATR_C_OK'){throw 'C verification failed'}
    if((RunTool (Join-Path $probe 'test-cpp.exe') @() $probe).Trim() -ne 'SATR_CPP_OK'){throw 'C++ verification failed'}
    [IO.File]::WriteAllText((Join-Path $probe 'Main.java'),'public class Main { public static void main(String[] args) { System.out.print("SATR_JAVA_OK"); } }')
    $null=RunTool $javac @('-J-XX:TieredStopAtLevel=1','-encoding','UTF-8','-proc:none','Main.java') $probe
    if((RunTool $java @('-cp',$probe,'Main') $probe).Trim() -ne 'SATR_JAVA_OK'){throw 'Java verification failed'}
    $null=FullChild $InstallRoot ([IO.Path]::GetFileName($probe))
    Remove-Item -LiteralPath $probe -Recurse -Force
    $state=@{schema=1;compiler=$gcc;clangd=$clangd;runtime=$runtime;model=$model;python=$python;node=$node;javac=$javac;java=$java;ready=$true}
    $stateText=$state | ConvertTo-Json -Compress
    [IO.File]::WriteAllText((Join-Path $InstallRoot 'ready.json'),$stateText,(New-Object Text.UTF8Encoding($false)))
    [Console]::WriteLine((@{kind='complete';message='كل الأدوات جاهزة. يمكنك الآن كتابة البرامج وتشغيلها.';percent=100;paths=$state} | ConvertTo-Json -Depth 4 -Compress))
} catch {
    [Console]::WriteLine((@{kind='detail';message=$_.ScriptStackTrace;percent=-1;cancellable=$true} | ConvertTo-Json -Compress))
    Emit error ('تعذر إكمال التجهيز. تحقق من الإنترنت ثم حاول مجددًا. '+$_.Exception.Message)
    exit 1
} finally {if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
