<#
.SYNOPSIS
Stops the L2OFF servers in reverse start order.

.DESCRIPTION
Order: L2NPC (and its restart supervision), L2Server, CacheD, AuthGateD,
AuthD. Each server gets Ctrl+C in its window, the same as pressing Ctrl+C
there, and the next one is stopped only after it has exited. A server that
is still running after its timeout is ended by force.

Windows opened by start-all.ps1 are closed once their server has stopped.
Servers started some other way keep their windows.
#>
param(
    # Internal: send Ctrl+C to the console of this process, then exit.
    [int] $SendCtrlCTo = 0
)

if ($SendCtrlCTo -gt 0) {
    # Runs in its own hidden process, because it has to leave its console to
    # join the server's. Every process in that console gets the Ctrl+C.
    Add-Type -Namespace L2Stop -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool FreeConsole();
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool AttachConsole(uint processId);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleCtrlHandler(System.IntPtr handler, bool add);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool GenerateConsoleCtrlEvent(uint ctrlEvent, uint processGroupId);
'@
    [void][L2Stop.Native]::FreeConsole()
    if (-not [L2Stop.Native]::AttachConsole($SendCtrlCTo)) { exit 2 }
    [void][L2Stop.Native]::SetConsoleCtrlHandler([System.IntPtr]::Zero, $true)
    $sent = [L2Stop.Native]::GenerateConsoleCtrlEvent(0, 0)
    Start-Sleep -Milliseconds 500
    [void][L2Stop.Native]::FreeConsole()
    if ($sent) { exit 0 } else { exit 3 }
}

$servers = @(
    @{ Name = 'L2NPC';     TimeoutSeconds = 60 }
    @{ Name = 'L2Server';  TimeoutSeconds = 60 }
    @{ Name = 'CacheD';    TimeoutSeconds = 120 }
    @{ Name = 'AuthGateD'; TimeoutSeconds = 30 }
    @{ Name = 'AuthD';     TimeoutSeconds = 60 }
)

function Write-Status([string] $Message, [ConsoleColor] $Color = 'Cyan') {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Message" -ForegroundColor $Color
}

# PowerShell windows running run-server.ps1 for this server, including the
# L2NPC supervision while it waits to start L2NPC again.
function Get-RunnerWindows([string] $Name) {
    @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
        Where-Object { $_.CommandLine -match 'run-server\.ps1' -and $_.CommandLine -match "-Name $Name(\s|$)" })
}

function Send-CtrlC([int] $ProcessId) {
    $sender = Start-Process powershell.exe -WindowStyle Hidden -Wait -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-SendCtrlCTo', $ProcessId)
    $sender.ExitCode -eq 0
}

function Stop-Server($Server) {
    $name = $Server.Name
    $windows = Get-RunnerWindows $name
    $processes = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    if ($windows.Count -eq 0 -and $processes.Count -eq 0) {
        Write-Status "$name não está rodando."
        return
    }

    Write-Status "Parando $name..."
    # One Ctrl+C per window reaches both run-server.ps1 and the server in it.
    # A server started some other way gets its own.
    $windowIds = @($windows | ForEach-Object { [int] $_.ProcessId })
    foreach ($id in $windowIds) {
        if (-not (Send-CtrlC $id)) { Write-Status "Não foi possível enviar Ctrl+C à janela do $name (PID $id)." Yellow }
    }
    foreach ($process in $processes) {
        $parent = (Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)").ParentProcessId
        if ($windowIds -notcontains [int] $parent -and -not (Send-CtrlC $process.Id)) {
            Write-Status "Não foi possível enviar Ctrl+C ao $name (PID $($process.Id))." Yellow
        }
    }

    $deadline = (Get-Date).AddSeconds($Server.TimeoutSeconds)
    while ((Get-Process -Name $name -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    $remaining = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    if ($remaining.Count -gt 0) {
        Write-Status "$name não terminou em $($Server.TimeoutSeconds) s; encerrando à força (PID $($remaining.Id -join ', '))." Yellow
        $remaining | Stop-Process -Force
        $remaining | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
        Write-Status "$name encerrado à força. A janela dele ficou aberta para consulta." Yellow
        return
    }

    # The windows are idle now; console.log keeps their output.
    foreach ($id in $windowIds) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
    Write-Status "$name parado." Green
}

foreach ($server in $servers) {
    Stop-Server $server
}
Write-Status 'Todos os servidores foram parados.' Green
