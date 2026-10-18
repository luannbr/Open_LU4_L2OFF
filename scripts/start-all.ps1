<#
.SYNOPSIS
Starts the L2OFF servers in order, each in its own window.

.DESCRIPTION
Order: AuthD, AuthGateD, CacheD, L2Server, L2NPC. Each server must be
listening on its ports before the next one starts. Servers that are already
running are left alone.

L2NPC runs under supervision (run-server.ps1 -Supervise): when L2Server goes
down, L2NPC exits with code 3, and its window starts it again as soon as
L2Server listens on the NPC port again.

Server output is appended to build\windows-msvc\Release\<Name>\console.log.

The game data (2018 or GF) is chosen in scripts\data-set.txt; see run-server.ps1.
#>

$runner = Join-Path $PSScriptRoot 'run-server.ps1'
$root = Split-Path $PSScriptRoot -Parent

# Ports come from AuthD\etc\config.txt (serverPort, serverExPort),
# AuthGateD\etc\config.txt (serverPort), CacheD's listeners and
# L2Server\etc\l2server.ini (UserPort, NPCPort). Update them here if those change.
$npcPort = 2002
$servers = @(
    @{ Name = 'AuthD';     Ports = @(2104, 2110);         TimeoutSeconds = 120 }
    @{ Name = 'AuthGateD'; Ports = @(2106);               TimeoutSeconds = 60 }
    @{ Name = 'CacheD';    Ports = @(12006, 12008, 12012); TimeoutSeconds = 600 }
    @{ Name = 'L2Server';  Ports = @(7777, $npcPort);     TimeoutSeconds = 900 }
)

function Write-Status([string] $Message, [ConsoleColor] $Color = 'Cyan') {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Message" -ForegroundColor $Color
}

function Test-Listening([int] $Port) {
    [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

function Start-ServerWindow([string] $Name, [string[]] $ExtraArgs = @()) {
    $arguments = @('-NoExit', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$runner`"", '-Name', $Name) + $ExtraArgs
    Start-Process powershell.exe -WorkingDirectory $root -ArgumentList $arguments | Out-Null
}

# Waits until every port of the server listens. Fails if the process ends or
# never appears, or if the timeout passes.
function Wait-ServerReady($Server) {
    $startedAt = Get-Date
    $seen = $false
    Write-Status "Aguardando $($Server.Name) (portas $($Server.Ports -join ', '))..."
    while ($true) {
        $missing = @($Server.Ports | Where-Object { -not (Test-Listening $_) })
        if ($missing.Count -eq 0) {
            Write-Status "$($Server.Name) pronto." Green
            return $true
        }
        $elapsed = ((Get-Date) - $startedAt).TotalSeconds
        if (Get-Process -Name $Server.Name -ErrorAction SilentlyContinue) {
            $seen = $true
        } elseif ($seen -or $elapsed -gt 30) {
            Write-Status "$($Server.Name) terminou durante a inicialização. Veja a janela dele ou build\windows-msvc\Release\$($Server.Name)\console.log." Red
            return $false
        }
        if ($elapsed -gt $Server.TimeoutSeconds) {
            Write-Status "$($Server.Name) não abriu as portas $($missing -join ', ') em $($Server.TimeoutSeconds) s." Red
            return $false
        }
        Start-Sleep -Seconds 1
    }
}

# AuthD and CacheD need PostgreSQL; take the port from AuthD's connection string.
$dbPort = 5432
$authdConfig = Get-Content (Join-Path $root 'AuthD\etc\config.txt') -ErrorAction SilentlyContinue
$connLine = $authdConfig | Where-Object { $_ -match '^\s*gameAccountDBconnStr\s*=.*port=(\d+)' } | Select-Object -First 1
if ($connLine -and $connLine -match 'port=(\d+)') { $dbPort = [int] $Matches[1] }
if (-not (Test-Listening $dbPort)) {
    Write-Status "PostgreSQL não está escutando na porta $dbPort. Inicie o serviço do PostgreSQL e rode este script de novo." Red
    exit 1
}

foreach ($server in $servers) {
    if (Get-Process -Name $server.Name -ErrorAction SilentlyContinue) {
        Write-Status "$($server.Name) já está rodando; não será iniciado de novo."
    } else {
        Write-Status "Iniciando $($server.Name)..."
        Start-ServerWindow $server.Name
    }
    if (-not (Wait-ServerReady $server)) {
        Write-Status 'Inicialização interrompida; os servidores seguintes não foram iniciados.' Red
        exit 1
    }
}

# A supervision window whose loop has ended stays open with "PARADO" in its
# title (for example after stop-all ended L2NPC by force); it no longer counts.
$supervisor = Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
    Where-Object { $_.CommandLine -match 'run-server\.ps1' -and $_.CommandLine -match '-Name L2NPC' -and $_.CommandLine -match '-Supervise' } |
    Where-Object { (Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue).MainWindowTitle -notmatch 'PARADO' }
if ($supervisor) {
    Write-Status 'L2NPC já está sob supervisão.'
} else {
    if (Get-Process -Name 'L2NPC' -ErrorAction SilentlyContinue) {
        Write-Status 'L2NPC já está rodando; a janela de supervisão assume quando ele terminar.'
    } else {
        Write-Status 'Iniciando L2NPC com reinício automático...'
    }
    Start-ServerWindow 'L2NPC' @('-WaitForPort', "$npcPort", '-Supervise')
}

Write-Status 'Pronto. Cada servidor roda na própria janela; fechar uma janela "rodando" derruba aquele servidor.' Green
