# Downloads the toolchains pinned in engine/toolchains.txt into <repo>/toolchains/ with SHA-256 checks.
# Idempotent: a tool whose folder holds a matching .pin stamp is skipped. No admin rights needed.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File engine\scripts\fetch-toolchains.ps1 [-ToolchainDir <folder>]
# -ToolchainDir: where the toolchains live when this script runs from a snapshot of the sources (check.ps1).
param([string]$ToolchainDir = '')
$ErrorActionPreference = 'Stop'
$engine = Split-Path -Parent $PSScriptRoot
$tc = $(if ($ToolchainDir) { $ToolchainDir } else { Join-Path (Split-Path -Parent $engine) 'toolchains' })
$dl = Join-Path $tc 'dl'
New-Item -ItemType Directory -Force $dl | Out-Null
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'

$pins = Get-Content (Join-Path $engine 'toolchains.txt') | Where-Object { $_ -and -not $_.StartsWith('#') } |
    ForEach-Object {
        $c = $_ -split "`t"
        [pscustomobject]@{ Name = $c[0]; Version = $c[1]; File = $c[2]; Sha = $c[3]; Url = $c[4] }
    }

function Get-Pinned($p) {
    $f = Join-Path $dl $p.File
    if (-not (Test-Path $f)) {
        Write-Host "download $($p.Url)"
        & curl.exe -sSfL -o "$f.part" $p.Url
        if ($LASTEXITCODE -ne 0) { throw "download failed: $($p.Url)" }
        Move-Item -Force "$f.part" $f
    }
    $h = (Get-FileHash -Algorithm SHA256 $f).Hash.ToLower()
    if ($h -ne $p.Sha) {
        Remove-Item -Force $f
        throw "SHA-256 mismatch for $($p.File): got $h, pinned $($p.Sha) (file deleted)"
    }
    return $f
}

function Test-Stamp($dest, $stamp) {
    $s = Join-Path $dest '.pin'
    return (Test-Path $s) -and ((Get-Content -Raw $s).Trim() -eq $stamp)
}

function Expand-Pinned($file, $dest) {
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Force $dest | Out-Null
    & $tar -xf $file -C $dest
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $file" }
}

foreach ($p in $pins | Where-Object { $_.Name -ne 'emsdk-dl' -and $_.Name -ne 'emsdk' }) {
    $dest = Join-Path $tc "$($p.Name)-$($p.Version)"
    if (Test-Stamp $dest $p.Sha) { Write-Host "ok   $($p.Name) $($p.Version)"; continue }
    Expand-Pinned (Get-Pinned $p) $dest
    Set-Content -Encoding ascii (Join-Path $dest '.pin') $p.Sha
    Write-Host "new  $($p.Name) $($p.Version)"
}

# emsdk: the tag archive, plus the three archives it would download, pre-placed so that emsdk uses them
# (EMSDK_KEEP_DOWNLOADS=1 skips a download when the file exists). emsdk itself verifies no hashes.
$em = $pins | Where-Object { $_.Name -eq 'emsdk' }
if (-not $em) { exit 0 }  # a toolchains.txt without the web build's lines (the public copy)
$emDl = @($pins | Where-Object { $_.Name -eq 'emsdk-dl' })
$dest = Join-Path $tc "emsdk-$($em.Version)"
$stamp = (@($em.Sha) + ($emDl | ForEach-Object { $_.Sha })) -join ' '
if (Test-Stamp $dest $stamp) {
    Write-Host "ok   emsdk $($em.Version)"
} else {
    Expand-Pinned (Get-Pinned $em) $dest
    $root = Join-Path $dest "emsdk-$($em.Version)"
    New-Item -ItemType Directory -Force (Join-Path $root 'downloads') | Out-Null
    foreach ($p in $emDl) { Copy-Item (Get-Pinned $p) (Join-Path $root 'downloads') }
    $env:EMSDK_KEEP_DOWNLOADS = '1'
    & cmd.exe /c "`"$root\emsdk.bat`" install $($em.Version) && `"$root\emsdk.bat`" activate $($em.Version)"
    if ($LASTEXITCODE -ne 0) { throw 'emsdk install failed' }
    $ver = (Get-Content -Raw (Join-Path $root 'upstream\emscripten\emscripten-version.txt')).Trim().Trim('"')
    if ($ver -ne $em.Version) { throw "emsdk installed Emscripten $ver, pinned $($em.Version)" }
    Remove-Item -Recurse -Force (Join-Path $root 'downloads')
    Set-Content -Encoding ascii (Join-Path $dest '.pin') $stamp
    Write-Host "new  emsdk $($em.Version)"
}
Write-Host 'toolchains ready'
