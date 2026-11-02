param([string]$SigningDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) '../NesDQ_signing'))
$ErrorActionPreference='Stop'
$signingPath=[IO.Path]::GetFullPath($SigningDirectory)
New-Item -ItemType Directory -Force -Path $signingPath | Out-Null
$keystore=Join-Path $signingPath 'nesdq-release.p12'
$properties=Join-Path $signingPath 'release.properties'
if ((Test-Path -LiteralPath $keystore) -or (Test-Path -LiteralPath $properties)) {
    if ((Test-Path -LiteralPath $keystore) -and (Test-Path -LiteralPath $properties)) { Write-Output 'Existing release signing preserved.'; exit 0 }
    throw 'Incomplete signing files exist. They were not overwritten.'
}
$keytool=Join-Path $env:JAVA_HOME 'bin/keytool.exe'
if (!(Test-Path -LiteralPath $keytool)) { throw 'Set JAVA_HOME to the JDK directory.' }
$passwordBytes=New-Object byte[] 32
[Security.Cryptography.RandomNumberGenerator]::Fill($passwordBytes)
$signingPassword=[Convert]::ToBase64String($passwordBytes).Replace('+','-').Replace('/','_').TrimEnd('=')
try {
    $env:NESDQ_SIGNING_PASSWORD=$signingPassword
    & $keytool -genkeypair -noprompt -storetype PKCS12 -keystore $keystore -alias nesdq-release -keyalg RSA -keysize 3072 -sigalg SHA256withRSA -validity 10000 -dname 'CN=NesDQ' -storepass:env NESDQ_SIGNING_PASSWORD -keypass:env NESDQ_SIGNING_PASSWORD
    if ($LASTEXITCODE -ne 0) { throw 'Release key creation failed.' }
    $content="storePassword=$signingPassword`nkeyAlias=nesdq-release`nkeyPassword=$signingPassword`n"
    [IO.File]::WriteAllText($properties,$content,[Text.UTF8Encoding]::new($false))
    & $keytool -exportcert -rfc -keystore $keystore -alias nesdq-release -storepass:env NESDQ_SIGNING_PASSWORD -file (Join-Path $signingPath 'release-certificate.pem')
    if ($LASTEXITCODE -ne 0) { throw 'Public certificate export failed.' }
}
finally {
    Remove-Item Env:NESDQ_SIGNING_PASSWORD -ErrorAction SilentlyContinue
    $signingPassword=$null
    [Array]::Clear($passwordBytes,0,$passwordBytes.Length)
}
Write-Output 'Release signing created. Passwords were saved locally and not printed.'
