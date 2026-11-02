param([string]$RomPath = '')
$executable = Join-Path $PSScriptRoot 'build/Release/nesdq.exe'
if (!(Test-Path -LiteralPath $executable)) { throw '先にWindows版をビルドしてください。' }
if ($RomPath) { & $executable $RomPath } else { & $executable }
exit $LASTEXITCODE
