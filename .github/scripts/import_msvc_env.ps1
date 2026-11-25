$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if (-not $installation) {
  throw 'Could not find a Visual Studio installation with the x64 C++ toolchain'
}

$vcvarsall = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path $vcvarsall)) {
  throw "vcvarsall.bat was not found: $vcvarsall"
}

$env:VCVARSALL = $vcvarsall
$allEnv = cmd /c "`"$env:VCVARSALL`" amd64 >NUL & powershell -NoProfile -Command `"Get-ChildItem env: | Select-Object -Property Key,Value | ConvertTo-Json`"" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) {
  throw "vcvarsall.bat failed with exit code $LASTEXITCODE"
}

$oldPath = $env:PATH -split ';'
foreach ($envvar in $allEnv) {
  if ($envvar.Key -ieq 'PATH') {
    # PATH has its own workflow command; add only vcvarsall's additions so the
    # runner keeps its existing paths without accumulating duplicates.
    foreach ($entry in ($envvar.Value -split ';')) {
      if ($entry -and $entry -notin $oldPath) {
        Add-Content -Path $env:GITHUB_PATH -Value $entry
      }
    }
  } elseif (
    # GitHub owns these variables, and unchanged values need no propagation.
    $envvar.Key -notmatch '^(GITHUB_|RUNNER_|ACTIONS_)' -and
    $envvar.Value -cne [Environment]::GetEnvironmentVariable($envvar.Key, 'Process')
  ) {
    "$($envvar.Key)=$($envvar.Value)" | Add-Content -Path $env:GITHUB_ENV
  }
}
