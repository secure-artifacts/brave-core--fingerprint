[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProxyHost,

  [Parameter(Mandatory = $true)]
  [ValidateRange(1, 65535)]
  [int]$ProxyPort,

  [Parameter(Mandatory = $true)]
  [string]$Username,

  [Security.SecureString]$Password,

  [ValidateRange(1, 120)]
  [int]$TimeoutSeconds = 10
)

$ErrorActionPreference = 'Stop'

function Read-ExactBytes {
  param(
    [Parameter(Mandatory = $true)]
    [System.IO.Stream]$Stream,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 65535)]
    [int]$Count
  )

  $buffer = [byte[]]::new($Count)
  $offset = 0
  while ($offset -lt $Count) {
    $read = $Stream.Read($buffer, $offset, $Count - $offset)
    if ($read -eq 0) {
      throw [System.IO.EndOfStreamException]::new(
        "SOCKS5 server closed the connection while reading $Count bytes."
      )
    }
    $offset += $read
  }

  return ,$buffer
}

function Get-Socks5ReplyDescription {
  param([byte]$ReplyCode)

  switch ($ReplyCode) {
    0x00 { return 'succeeded' }
    0x01 { return 'general SOCKS server failure' }
    0x02 { return 'connection not allowed by ruleset' }
    0x03 { return 'network unreachable' }
    0x04 { return 'host unreachable' }
    0x05 { return 'connection refused' }
    0x06 { return 'TTL expired' }
    0x07 { return 'command not supported' }
    0x08 { return 'address type not supported' }
    default { return ('unknown reply 0x{0:X2}' -f $ReplyCode) }
  }
}

if ($null -eq $Password) {
  $Password = Read-Host -Prompt 'SOCKS5 password' -AsSecureString
}

$targetHost = 'api.ipify.org'
$targetPort = 443
$timeoutMilliseconds = $TimeoutSeconds * 1000
$client = $null
$networkStream = $null
$sslStream = $null
$usernameBytes = $null
$passwordBytes = $null
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$result = [ordered]@{
  Proxy = "$ProxyHost`:$ProxyPort"
  TcpConnected = $false
  Authenticated = $false
  ConnectSucceeded = $false
  HttpsStatus = $null
  ExitIp = $null
  ElapsedMilliseconds = $null
  Error = $null
}

try {
  $client = [System.Net.Sockets.TcpClient]::new()
  $connectTask = $client.ConnectAsync($ProxyHost, $ProxyPort)
  if (-not $connectTask.Wait($timeoutMilliseconds)) {
    throw [System.TimeoutException]::new(
      "TCP connection to $ProxyHost`:$ProxyPort timed out after $TimeoutSeconds seconds."
    )
  }
  if (-not $client.Connected) {
    throw [System.Net.Sockets.SocketException]::new()
  }
  $result.TcpConnected = $true

  $networkStream = $client.GetStream()
  $networkStream.ReadTimeout = $timeoutMilliseconds
  $networkStream.WriteTimeout = $timeoutMilliseconds

  # Offer only RFC 1929 username/password authentication so this test cannot
  # accidentally pass through an unauthenticated method.
  [byte[]]$methodRequest = @(0x05, 0x01, 0x02)
  $networkStream.Write($methodRequest, 0, $methodRequest.Length)
  $methodReply = Read-ExactBytes -Stream $networkStream -Count 2
  if ($methodReply[0] -ne 0x05) {
    throw "Unexpected SOCKS version in method reply: $($methodReply[0])."
  }
  if ($methodReply[1] -eq 0xFF) {
    throw 'The proxy rejected username/password authentication.'
  }
  if ($methodReply[1] -ne 0x02) {
    throw ('The proxy selected unexpected authentication method 0x{0:X2}.' -f $methodReply[1])
  }

  $plainPassword = [System.Net.NetworkCredential]::new('', $Password).Password
  $usernameBytes = [System.Text.Encoding]::UTF8.GetBytes($Username)
  $passwordBytes = [System.Text.Encoding]::UTF8.GetBytes($plainPassword)
  $plainPassword = $null
  if ($usernameBytes.Length -notin 1..255) {
    throw 'The UTF-8 username length must be between 1 and 255 bytes.'
  }
  if ($passwordBytes.Length -notin 1..255) {
    throw 'The UTF-8 password length must be between 1 and 255 bytes.'
  }

  $authRequest = [byte[]]::new(3 + $usernameBytes.Length + $passwordBytes.Length)
  $authRequest[0] = 0x01
  $authRequest[1] = [byte]$usernameBytes.Length
  [Array]::Copy($usernameBytes, 0, $authRequest, 2, $usernameBytes.Length)
  $passwordLengthOffset = 2 + $usernameBytes.Length
  $authRequest[$passwordLengthOffset] = [byte]$passwordBytes.Length
  [Array]::Copy(
    $passwordBytes,
    0,
    $authRequest,
    $passwordLengthOffset + 1,
    $passwordBytes.Length
  )
  $networkStream.Write($authRequest, 0, $authRequest.Length)
  [Array]::Clear($authRequest, 0, $authRequest.Length)

  $authReply = Read-ExactBytes -Stream $networkStream -Count 2
  if ($authReply[0] -ne 0x01) {
    throw "Unexpected RFC 1929 authentication version: $($authReply[0])."
  }
  if ($authReply[1] -ne 0x00) {
    throw ('SOCKS5 username/password authentication failed with status 0x{0:X2}.' -f $authReply[1])
  }
  $result.Authenticated = $true

  $targetHostBytes = [System.Text.Encoding]::ASCII.GetBytes($targetHost)
  [byte[]]$connectRequest = @(
    0x05,
    0x01,
    0x00,
    0x03,
    [byte]$targetHostBytes.Length
  ) + $targetHostBytes + @(
    [byte](($targetPort -shr 8) -band 0xFF),
    [byte]($targetPort -band 0xFF)
  )
  $networkStream.Write($connectRequest, 0, $connectRequest.Length)

  $connectReply = Read-ExactBytes -Stream $networkStream -Count 4
  if ($connectReply[0] -ne 0x05) {
    throw "Unexpected SOCKS version in CONNECT reply: $($connectReply[0])."
  }
  if ($connectReply[1] -ne 0x00) {
    $description = Get-Socks5ReplyDescription -ReplyCode $connectReply[1]
    throw "SOCKS5 CONNECT failed: $description."
  }

  switch ($connectReply[3]) {
    0x01 { $null = Read-ExactBytes -Stream $networkStream -Count 4 }
    0x03 {
      $domainLength = (Read-ExactBytes -Stream $networkStream -Count 1)[0]
      if ($domainLength -gt 0) {
        $null = Read-ExactBytes -Stream $networkStream -Count $domainLength
      }
    }
    0x04 { $null = Read-ExactBytes -Stream $networkStream -Count 16 }
    default { throw ('Unsupported bound address type 0x{0:X2}.' -f $connectReply[3]) }
  }
  $null = Read-ExactBytes -Stream $networkStream -Count 2
  $result.ConnectSucceeded = $true

  $sslStream = [System.Net.Security.SslStream]::new($networkStream, $false)
  $sslStream.ReadTimeout = $timeoutMilliseconds
  $sslStream.WriteTimeout = $timeoutMilliseconds
  $sslStream.AuthenticateAsClient($targetHost)

  $httpRequest = "GET /?format=json HTTP/1.1`r`nHost: $targetHost`r`nConnection: close`r`nUser-Agent: FingerprintBrowser-SOCKS5-QA/1.0`r`n`r`n"
  $httpRequestBytes = [System.Text.Encoding]::ASCII.GetBytes($httpRequest)
  $sslStream.Write($httpRequestBytes, 0, $httpRequestBytes.Length)

  $responseBuffer = [byte[]]::new(8192)
  $responseMemory = [System.IO.MemoryStream]::new()
  while (($read = $sslStream.Read($responseBuffer, 0, $responseBuffer.Length)) -gt 0) {
    $responseMemory.Write($responseBuffer, 0, $read)
    if ($responseMemory.Length -gt 1MB) {
      throw 'HTTPS response exceeded the 1 MiB safety limit.'
    }
  }
  $httpResponse = [System.Text.Encoding]::UTF8.GetString($responseMemory.ToArray())
  $responseMemory.Dispose()
  $headerSeparator = $httpResponse.IndexOf("`r`n`r`n")
  if ($headerSeparator -lt 0) {
    throw 'The HTTPS response did not contain a complete header block.'
  }
  $headerLines = $httpResponse.Substring(0, $headerSeparator).Split("`r`n")
  if ($headerLines[0] -notmatch '^HTTP/\d(?:\.\d)?\s+(\d{3})') {
    throw "Unexpected HTTP status line: $($headerLines[0])"
  }
  $result.HttpsStatus = [int]$Matches[1]
  if ($result.HttpsStatus -ne 200) {
    throw "HTTPS exit check returned status $($result.HttpsStatus)."
  }
  $body = $httpResponse.Substring($headerSeparator + 4)
  $ipResponse = $body | ConvertFrom-Json
  $result.ExitIp = $ipResponse.ip
}
catch {
  $result.Error = $_.Exception.Message
}
finally {
  $stopwatch.Stop()
  $result.ElapsedMilliseconds = $stopwatch.ElapsedMilliseconds
  if ($null -ne $usernameBytes) {
    [Array]::Clear($usernameBytes, 0, $usernameBytes.Length)
  }
  if ($null -ne $passwordBytes) {
    [Array]::Clear($passwordBytes, 0, $passwordBytes.Length)
  }
  if ($null -ne $sslStream) {
    $sslStream.Dispose()
  } elseif ($null -ne $networkStream) {
    $networkStream.Dispose()
  }
  if ($null -ne $client) {
    $client.Dispose()
  }
}

[pscustomobject]$result | ConvertTo-Json
if ($null -ne $result.Error) {
  exit 1
}
