param([string]$AppRoot,[string]$WorkRoot,[switch]$Network)
$ErrorActionPreference='Stop'
$AppRoot=[IO.Path]::GetFullPath($AppRoot);$WorkRoot=[IO.Path]::GetFullPath($WorkRoot)
$null=New-Item -ItemType Directory -Path $WorkRoot -Force
. (Join-Path $AppRoot 'setup\SetupTools.ps1') -AppRoot $AppRoot -InstallRoot (Join-Path $WorkRoot 'installed') -CacheRoot (Join-Path $WorkRoot 'downloads') -FunctionsOnly
$script:checks=0;$script:failed=0
function Check([bool]$Okay,[string]$Name){$script:checks++;if(!$Okay){$script:failed++};Write-Output (($('FAIL','PASS')[[int]$Okay])+' '+$Name)}
function Reject([scriptblock]$Action,[string]$Name){$bad=$false;try{& $Action | Out-Null}catch{$bad=$true};Check $bad $Name}
Check ((FullChild $WorkRoot 'folder\file.cpp').StartsWith($WorkRoot)) 'safe_nested_path'
Reject {FullChild $WorkRoot '..\escape.txt'} 'reject_parent_traversal'
Reject {FullChild $WorkRoot 'C:\escape.txt'} 'reject_absolute_archive_path'
Reject {FullChild $WorkRoot 'folder\x:stream'} 'reject_alternate_data_stream'
Check ((ToolArguments @('a b','a"b','C:\ends\')) -eq '"a b" "a\"b" "C:\ends\\"') 'native_argument_quoting'
foreach($url in @('http://github.com/a','https://github.com.evil.test/a','https://github.com@evil.test/a','https://user@github.com/a','https://127.0.0.1/a','https://github.com:444/a','file:///C:/a','https://github.com/a#fragment')){
    Reject {AssertDownloadUri ([Uri]$url) $true} ('reject_redirect_'+$url)
}
AssertDownloadUri ([Uri]'https://release-assets.githubusercontent.com/file?token=example') $true
Check $true 'allow_exact_release_asset_host'
Reject {AssertDownloadUri ([Uri]'https://release-assets.githubusercontent.com/file') $false} 'cdn_only_allowed_after_trusted_initial_host'
function MockDownloadClient([string[]]$Locations){
    $client=[pscustomobject]@{Calls=(New-Object Collections.Generic.List[string]);Responses=(New-Object Collections.Generic.Queue[Net.Http.HttpResponseMessage])}
    foreach($location in $Locations){$response=New-Object Net.Http.HttpResponseMessage;if($location){$response.StatusCode=[Net.HttpStatusCode]::Found;$response.Headers.Location=[Uri]$location};$client.Responses.Enqueue($response)}
    $client | Add-Member ScriptMethod GetAsync {param($uri,$option)
        $this.Calls.Add($uri.AbsoluteUri);$task=[pscustomobject]@{Response=$this.Responses.Dequeue()}
        $task | Add-Member ScriptMethod GetAwaiter {return $this}
        $task | Add-Member ScriptMethod GetResult {return $this.Response}
        return $task
    }
    return $client
}
$client=MockDownloadClient @('http://github.com/insecure')
Reject {OpenDownloadResponse $client ([Uri]'https://github.com/package')} 'https_downgrade_rejected_before_request'
Check ($client.Calls.Count -eq 1) 'no_request_sent_to_insecure_redirect'
$client=MockDownloadClient @('https://evil.test/payload')
Reject {OpenDownloadResponse $client ([Uri]'https://github.com/package')} 'unknown_redirect_host_rejected_before_request'
Check ($client.Calls.Count -eq 1) 'no_request_sent_to_untrusted_redirect'
$client=MockDownloadClient @('https://release-assets.githubusercontent.com/file','')
$response=OpenDownloadResponse $client ([Uri]'https://github.com/package')
Check ($client.Calls.Count -eq 2 -and [int]$response.StatusCode -eq 200) 'trusted_redirect_flow_completes';$response.Dispose()
$client=MockDownloadClient @('https://github.com/loop','https://github.com/loop','https://github.com/loop','https://github.com/loop','https://github.com/loop','https://github.com/loop')
Reject {OpenDownloadResponse $client ([Uri]'https://github.com/package')} 'redirect_loop_is_bounded'
foreach($name in @('traversal','symlink','valid')){
    $file=Join-Path $WorkRoot ($name+'.zip');if(Test-Path $file){Remove-Item -LiteralPath $file}
    $zip=[IO.Compression.ZipFile]::Open($file,[IO.Compression.ZipArchiveMode]::Create)
    $entry=$zip.CreateEntry($(if($name -eq 'traversal'){'../escape.txt'}else{'safe.txt'}))
    if($name -eq 'symlink'){$entry.ExternalAttributes=-1577058304}
    $writer=New-Object IO.StreamWriter($entry.Open());$writer.Write('safe');$writer.Dispose();$zip.Dispose()
    if($name -eq 'valid'){ExtractChecked $file (Join-Path $WorkRoot 'valid');Check ((Get-Content (Join-Path $WorkRoot 'valid\safe.txt') -Raw) -eq 'safe') 'verified_zip_extracts'}
    else {Reject {ExtractChecked $file (Join-Path $WorkRoot $name)} ('reject_'+$name+'_zip')}
}
$null=New-Item -ItemType Directory -Path $CacheRoot,$InstallRoot -Force
$bad=[pscustomobject]@{id='bad-package';extension='.zip';sha256=('0'*64);url='http://example.com/a.zip';bytes=4;label='test'}
Reject {Fetch $bad} 'reject_non_https_package'
$bad.url='https://example.com/a.zip';Reject {Fetch $bad} 'reject_unapproved_download_host'
$Offline=$true;$bad.url='https://github.com/test/package';[IO.File]::WriteAllText((Join-Path $CacheRoot 'bad-package.zip'),'bad')
Reject {Fetch $bad} 'corrupted_cache_never_accepted_offline'
$Offline=$false
$from=Join-Path $InstallRoot 'move-source';$to=Join-Path $InstallRoot 'move-target';$null=New-Item -ItemType Directory -Path $from;[IO.File]::WriteAllText((Join-Path $from 'x.txt'),'x')
MoveManagedDirectory $from $to;Check ((Test-Path (Join-Path $to 'x.txt')) -and !(Test-Path $from)) 'managed_directory_commits'
Reject {MoveManagedDirectory $to (Join-Path $WorkRoot 'outside')} 'directory_move_cannot_escape_root'
if($Network){$file=Fetch $manifest.visualCpp;Check ((HashFile $file) -eq $manifest.visualCpp.sha256) 'real_https_download_sha256_verified';Check ((Get-AuthenticodeSignature $file).Status -eq 'Valid') 'downloaded_microsoft_installer_signature';$same=Fetch $manifest.visualCpp;Check ($same -eq $file) 'verified_download_reused_from_cache'}
Write-Output ($script:checks.ToString()+' setup checks, '+$script:failed+' failed')
if($script:failed){exit 1}
