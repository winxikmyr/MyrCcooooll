# server/server.ps1 - MyrLegit Auth & Cloud Server
Add-Type -AssemblyName System.Web -ErrorAction SilentlyContinue
$port = 8000
$dbFile = Join-Path $PSScriptRoot "users.json"

if (-not (Test-Path $dbFile)) {
    Set-Content -Path $dbFile -Value "[]" -Encoding UTF8
}

function Get-Users {
    if (-not (Test-Path $dbFile)) { return @() }
    $raw = Get-Content -Path $dbFile -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw) -or $raw.Trim() -eq "[]") { return @() }
    $parsed = ConvertFrom-Json $raw
    if (-not $parsed) { return @() }
    $list = @()
    foreach ($item in @($parsed)) {
        if ($item.PSObject.Properties['value']) {
            foreach ($sub in $item.value) {
                if ($sub -and $sub.username) { $list += $sub }
            }
        } elseif ($item -and $item.username) {
            $list += $item
        }
    }
    return $list
}

function Save-Users($users) {
    $clean = @()
    foreach ($item in @($users)) {
        if ($item.PSObject.Properties['value']) {
            foreach ($sub in $item.value) {
                if ($sub -and $sub.username) { $clean += $sub }
            }
        } elseif ($item -and $item.username) {
            $clean += $item
        }
    }
    if ($clean.Count -eq 0) {
        Set-Content -Path $dbFile -Value "[]" -Encoding UTF8
    } else {
        $json = @($clean) | ConvertTo-Json -Depth 10
        if (-not $json.Trim().StartsWith("[")) {
            $json = "[`n" + $json + "`n]"
        }
        Set-Content -Path $dbFile -Value $json -Encoding UTF8
    }
}

$listener = New-Object System.Net.HttpListener
$isAllInterfaces = $false

# Сначала пробуем слушать на всех сетевых интерфейсах (0.0.0.0 / +) для друзей из Radmin VPN / локальной сети
try {
    $listener.Prefixes.Add("http://+:$port/")
    $listener.Start()
    $isAllInterfaces = $true
} catch {
    # Если не хватает прав URLACL на '+', запускаем на localhost
    $listener = New-Object System.Net.HttpListener
    $listener.Prefixes.Add("http://localhost:$port/")
    $listener.Prefixes.Add("http://127.0.0.1:$port/")
    try {
        $listener.Start()
        $isAllInterfaces = $false
    } catch {
        Write-Host "[!] Не удалось запустить сервер на порту $port : $_" -ForegroundColor Red
        exit 1
    }
}

$allIps = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.IPAddress -notlike "127*" -and $_.IPAddress -notlike "169.254*" }

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   MYRLEGIT AUTH & CLOUD CONFIG SERVER ЗАПУЩЕН!" -ForegroundColor Green
Write-Host "   Панель управления (для вас, админа): http://localhost:${port}/" -ForegroundColor Yellow
Write-Host "   База данных пользователей: $dbFile" -ForegroundColor Gray
Write-Host "----------------------------------------------------------" -ForegroundColor DarkCyan

if ($isAllInterfaces) {
    Write-Host "   [OK] Сервер принимает подключения от ваших друзей!" -ForegroundColor Green
    Write-Host "   Адреса для подключения друзей в Injector.exe:" -ForegroundColor White
    foreach ($item in $allIps) {
        Write-Host "     -> http://$($item.IPAddress):${port}/  ($($item.InterfaceAlias))" -ForegroundColor Cyan
    }
} else {
    Write-Host "   [!] Сервер запущен в режиме ТОЛЬКО ДЛЯ ВАС (localhost)." -ForegroundColor Yellow
    Write-Host "   Чтобы друзья могли подключаться по сети (Radmin VPN / LAN):" -ForegroundColor Yellow
    Write-Host "   Запустите 'Запустить_Сервер.bat' один раз от имени Администратора!" -ForegroundColor Yellow
}
Write-Host "==========================================================" -ForegroundColor Cyan

# Автоматически открываем браузер админки
try {
    Start-Process "http://localhost:$port/"
} catch {}

while ($listener.IsListening) {
    $context = $listener.GetContext()
    $request = $context.Request
    $response = $context.Response
    $response.Headers.Add("Access-Control-Allow-Origin", "*")

    $path = $request.Url.AbsolutePath
    $method = $request.HttpMethod

    $response.Headers.Add("Access-Control-Allow-Origin", "*")
    $response.Headers.Add("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
    $response.Headers.Add("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With")

    if ($method -eq "OPTIONS") {
        $response.StatusCode = 200
        $response.Close()
        continue
    }

    # Чтение тела POST
    $body = ""
    if ($request.HasEntityBody) {
        $reader = New-Object System.IO.StreamReader($request.InputStream, $request.ContentEncoding)
        $body = $reader.ReadToEnd()
        $reader.Close()
    }

    $users = @(Get-Users)

    # ── API Users List (для AJAX веб-сайта) ──
    if ($method -eq "GET" -and $path -eq "/api/users") {
        $clean = @($users | Where-Object { $_ -and $_.PSObject.Properties['username'] })
        $json = if ($clean.Count -eq 0) { "[]" } else {
            $j = ConvertTo-Json $clean -Depth 10
            if (-not $j.Trim().StartsWith("[")) { "[`n$j`n]" } else { $j }
        }
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($json)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── Главная страница (Web Dashboard) ──
    if ($method -eq "GET" -and ($path -eq "/" -or $path -eq "/admin" -or $path -eq "/index.html")) {
        $htmlFile = Join-Path $PSScriptRoot "public\index.html"
        if (-not (Test-Path $htmlFile)) {
            $htmlFile = Join-Path $PSScriptRoot "..\server\public\index.html"
        }
        if (Test-Path $htmlFile) {
            $buffer = [System.IO.File]::ReadAllBytes($htmlFile)
            $response.ContentType = "text/html; charset=utf-8"
            $response.ContentLength64 = $buffer.Length
            $response.OutputStream.Write($buffer, 0, $buffer.Length)
            $response.Close()
            continue
        }

        # Fallback на встроенный шаблон, если файл не найден
        $totalUsers = $users.Count
        $activeHwids = ($users | Where-Object { -not [string]::IsNullOrEmpty($_.hwid) }).Count

        $rows = ""
        foreach ($u in $users) {
            $hwidHtml = if (-not [string]::IsNullOrEmpty($u.hwid)) {
                "<span class='badge hwid'>$($u.hwid)</span>"
            } else {
                "<span class='badge no-hwid'>Не привязан</span>"
            }
            $cfgLen = if ($u.cloud_cfg) { ($u.cloud_cfg).Length } else { 0 }

            $rows += @"
            <tr>
                <td>$($u.id)</td>
                <td><strong>$($u.username)</strong></td>
                <td><code class='pwd'>$($u.password)</code></td>
                <td>$hwidHtml</td>
                <td>$($u.created_at)</td>
                <td>$($u.last_seen)</td>
                <td>$cfgLen B</td>
                <td>
                    <form method='POST' action='/admin/reset_hwid' style='display:inline;'>
                        <input type='hidden' name='id' value='$($u.id)'>
                        <button class='btn btn-warning' type='submit'>Сбросить HWID</button>
                    </form>
                    <form method='POST' action='/admin/delete_user' style='display:inline;' onsubmit="return confirm('Удалить $($u.username)?');">
                        <input type='hidden' name='id' value='$($u.id)'>
                        <button class='btn btn-danger' type='submit'>Удалить</button>
                    </form>
                </td>
            </tr>
"@
        }
        if (-not $rows) {
            $rows = "<tr><td colspan='8' style='text-align:center; padding: 25px; color:#888;'>Пока нет зарегистрированных пользователей</td></tr>"
        }

        $html = @"
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>MyrLegit Database & Auth Server</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
        body { background: #0b0e14; color: #d8e2ec; padding: 30px; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #1a2233; padding-bottom: 20px; margin-bottom: 30px; }
        .logo { font-size: 26px; font-weight: 800; letter-spacing: 1px; color: #00d2ff; text-transform: uppercase; }
        .logo span { color: #ffffff; }
        .stats { display: flex; gap: 20px; }
        .stat-card { background: #131926; border: 1px solid #1f2a40; border-radius: 8px; padding: 15px 25px; min-width: 170px; }
        .stat-card .num { font-size: 28px; font-weight: 700; color: #00d2ff; }
        .stat-card .lbl { font-size: 13px; color: #8899ac; text-transform: uppercase; letter-spacing: 0.5px; margin-top: 4px; }
        .container { background: #131926; border: 1px solid #1f2a40; border-radius: 10px; overflow: hidden; }
        .table-title { padding: 18px 25px; font-size: 17px; font-weight: 600; background: #171e2e; border-bottom: 1px solid #1f2a40; color: #e1e7f0; }
        table { width: 100%; border-collapse: collapse; text-align: left; font-size: 14px; }
        th { background: #111622; color: #7f93aa; font-weight: 600; padding: 14px 20px; border-bottom: 1px solid #1f2a40; text-transform: uppercase; font-size: 12px; }
        td { padding: 14px 20px; border-bottom: 1px solid #171f30; vertical-align: middle; }
        tr:hover td { background: #161e2f; }
        .badge { padding: 4px 10px; border-radius: 4px; font-size: 12px; font-weight: 600; }
        .badge.hwid { background: rgba(0, 210, 255, 0.15); color: #00d2ff; border: 1px solid rgba(0, 210, 255, 0.3); font-family: Consolas, monospace; }
        .badge.no-hwid { background: rgba(255, 170, 0, 0.15); color: #ffaa00; border: 1px solid rgba(255, 170, 0, 0.3); }
        .pwd { background: #0c0f17; padding: 3px 8px; border-radius: 4px; color: #ff7675; border: 1px solid #232d42; }
        .btn { border: none; padding: 6px 12px; border-radius: 5px; cursor: pointer; font-size: 12px; font-weight: 600; transition: 0.2s; }
        .btn-warning { background: #e17055; color: #fff; margin-right: 6px; }
        .btn-warning:hover { background: #d63031; }
        .btn-danger { background: #2d3436; color: #b2bec3; }
        .btn-danger:hover { background: #d63031; color: #fff; }
        .footer { margin-top: 25px; text-align: center; color: #636e72; font-size: 13px; }
    </style>
</head>
<body>
    <div class="header">
        <div class="logo">MYR<span>LEGIT</span> <span style="font-size: 14px; color: #7f93aa; font-weight: 400; margin-left: 10px;">Cloud & Auth Server</span></div>
        <div class="stats">
            <div class="stat-card">
                <div class="num">$totalUsers</div>
                <div class="lbl">Пользователей</div>
            </div>
            <div class="stat-card">
                <div class="num">$activeHwids</div>
                <div class="lbl">Привязанных HWID</div>
            </div>
        </div>
    </div>

    <div class="container">
        <div class="table-title">База данных пользователей и компьютеров (HWID)</div>
        <table>
            <thead>
                <tr>
                    <th>ID</th>
                    <th>Логин</th>
                    <th>Пароль</th>
                    <th>HWID Компьютера</th>
                    <th>Дата регистрации</th>
                    <th>Последняя активность</th>
                    <th>Облачный КФГ</th>
                    <th>Действия</th>
                </tr>
            </thead>
            <tbody>
                $rows
            </tbody>
        </table>
    </div>

    <div class="footer">
        Сервер активен на порту <strong>$port</strong> • База данных: users.json
    </div>
</body>
</html>
"@
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($html)
        $response.ContentType = "text/html; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── Admin Reset HWID ──
    if ($method -eq "POST" -and $path -eq "/admin/reset_hwid") {
        $id = [System.Web.HttpUtility]::ParseQueryString($body)["id"]
        foreach ($u in $users) {
            if ($u.id -eq [int]$id) {
                $u.hwid = ""
                break
            }
        }
        Save-Users $users
        $respJson = '{"status":"ok","message":"HWID сброшен"}'
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── Admin Delete User ──
    if ($method -eq "POST" -and $path -eq "/admin/delete_user") {
        $id = [System.Web.HttpUtility]::ParseQueryString($body)["id"]
        $users = @($users | Where-Object { $_.id -ne [int]$id })
        Save-Users $users
        $respJson = '{"status":"ok","message":"Пользователь удален"}'
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── Admin Edit User (изменение логина, пароля или HWID вручную) ──
    if ($method -eq "POST" -and $path -eq "/admin/update_user") {
        $data = if ($body.Trim().StartsWith("{")) { ConvertFrom-Json $body } else {
            $qs = [System.Web.HttpUtility]::ParseQueryString($body)
            @{ id = $qs["id"]; username = $qs["username"]; password = $qs["password"]; hwid = $qs["hwid"] }
        }
        $id = [int]$data.id
        $newUname = if ($data.username) { ($data.username).ToString().Trim() } else { "" }
        $newPwd   = if ($data.password) { ($data.password).ToString().Trim() } else { "" }
        $newHwid  = if ($data.hwid -ne $null) { ($data.hwid).ToString().Trim() } else { "" }

        foreach ($u in $users) {
            if ($u.id -eq $id) {
                if ($newUname) { $u.username = $newUname }
                if ($newPwd) { $u.password = $newPwd }
                $u.hwid = $newHwid
                break
            }
        }
        Save-Users $users
        $respJson = '{"status":"ok","message":"Данные пользователя обновлены"}'
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── Скачивание DLL с сервера ──
    if ($method -eq "GET" -and ($path -eq "/api/download_dll" -or $path -eq "/download/MyrLegit.dll")) {
        $paths = @(
            (Join-Path $PSScriptRoot "..\MyrLegit.dll"),
            (Join-Path $PSScriptRoot "..\..\БИЛДЫ\MyrLegit.dll"),
            (Join-Path $PSScriptRoot "MyrLegit.dll")
        )
        $target = $paths | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($target) {
            $bytes = [System.IO.File]::ReadAllBytes($target)
            $response.ContentType = "application/octet-stream"
            $response.AddHeader("Content-Disposition", "attachment; filename=MyrLegit.dll")
            $response.ContentLength64 = $bytes.Length
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
            $response.Close()
            continue
        } else {
            $response.StatusCode = 404
            $response.Close()
            continue
        }
    }

    # ── Скачивание Injector с сервера ──
    if ($method -eq "GET" -and ($path -eq "/api/download_injector" -or $path -eq "/download/Injector.exe")) {
        $paths = @(
            (Join-Path $PSScriptRoot "..\Injector.exe"),
            (Join-Path $PSScriptRoot "..\..\БИЛДЫ\Injector.exe"),
            (Join-Path $PSScriptRoot "Injector.exe")
        )
        $target = $paths | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($target) {
            $bytes = [System.IO.File]::ReadAllBytes($target)
            $response.ContentType = "application/octet-stream"
            $response.AddHeader("Content-Disposition", "attachment; filename=Injector.exe")
            $response.ContentLength64 = $bytes.Length
            $response.OutputStream.Write($bytes, 0, $bytes.Length)
            $response.Close()
            continue
        } else {
            $response.StatusCode = 404
            $response.Close()
            continue
        }
    }

    # ── API Register ──
    if ($method -eq "POST" -and $path -eq "/api/register") {
        $data = ConvertFrom-Json $body
        $uname = ($data.username).Trim()
        $pwd   = ($data.password).Trim()

        $existing = $users | Where-Object { $_.username -eq $uname }
        if ($existing) {
            $respJson = '{"status":"error","message":"Пользователь с таким логином уже существует"}'
        } else {
            $now = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
            $newId = if ($users.Count -gt 0) { (($users | Measure-Object -Property id -Maximum).Maximum + 1) } else { 1 }
            $newUser = [PSCustomObject]@{
                id         = $newId
                username   = $uname
                password   = $pwd
                hwid       = ""
                created_at = $now
                last_seen  = $now
                cloud_cfg  = ""
            }
            $users += $newUser
            Save-Users $users
            $respJson = '{"status":"ok","message":"Регистрация успешна!"}'
        }
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── API Login (Проверка пароля и привязка HWID) ──
    if ($method -eq "POST" -and $path -eq "/api/login") {
        $data = ConvertFrom-Json $body
        $uname = ($data.username).Trim()
        $pwd   = ($data.password).Trim()
        $hwid  = ($data.hwid).Trim()

        $user = $users | Where-Object { $_.username -eq $uname }
        $respJson = ""
        if (-not $user) {
            $respJson = '{"status":"error","message":"Пользователь не найден"}'
        } elseif ($user.password -ne $pwd) {
            $respJson = '{"status":"error","message":"Неверный пароль"}'
        } else {
            $now = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
            if ([string]::IsNullOrEmpty($user.hwid) -and -not [string]::IsNullOrEmpty($hwid)) {
                $user.hwid = $hwid
                $user.last_seen = $now
                Save-Users $users
            } elseif ($user.hwid -ne $hwid) {
                $respJson = '{"status":"error","message":"HWID не совпадает! Обратитесь к администратору."}'
            } else {
                $user.last_seen = $now
                Save-Users $users
            }

            if (-not $respJson) {
                $cfgEscaped = if ($user.cloud_cfg) { $user.cloud_cfg } else { "" }
                $respObj = [PSCustomObject]@{
                    status    = "ok"
                    message   = "Успешная авторизация"
                    username  = $user.username
                    hwid      = $user.hwid
                    cloud_cfg = $cfgEscaped
                }
                $respJson = ConvertTo-Json -InputObject $respObj -Compress
            }
        }
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── API HWID Login (Авто-вход без пароля) ──
    if ($method -eq "POST" -and $path -eq "/api/hwid_login") {
        $data = ConvertFrom-Json $body
        $hwid = ($data.hwid).Trim()

        $user = $users | Where-Object { $_.hwid -eq $hwid }
        $respJson = ""
        if ($user) {
            $user.last_seen = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
            Save-Users $users
            $respObj = [PSCustomObject]@{
                status    = "ok"
                username  = $user.username
                cloud_cfg = if ($user.cloud_cfg) { $user.cloud_cfg } else { "" }
            }
            $respJson = ConvertTo-Json -InputObject $respObj -Compress
        } else {
            $respJson = '{"status":"not_found","message":"HWID не зарегистрирован"}'
        }
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    # ── API Save Config ──
    if ($method -eq "POST" -and $path -eq "/api/config/save") {
        $data = ConvertFrom-Json $body
        $uname = ($data.username).Trim()
        $hwid  = ($data.hwid).Trim()
        $cfg   = $data.config

        $user = $users | Where-Object { $_.username -eq $uname -and $_.hwid -eq $hwid }
        if ($user) {
            $user.cloud_cfg = $cfg
            Save-Users $users
            $respJson = '{"status":"ok","message":"Конфиг сохранен"}'
        } else {
            $respJson = '{"status":"error","message":"Пользователь не найден"}'
        }
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($respJson)
        $response.ContentType = "application/json; charset=utf-8"
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.Close()
        continue
    }

    $response.StatusCode = 404
    $response.Close()
}

