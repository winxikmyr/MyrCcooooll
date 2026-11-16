#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MyrLegit Auth & Cloud Config Server
Web-интерфейс и база данных HWID/Пользователей
"""

import http.server
import socketserver
import json
import sqlite3
import os
import urllib.parse
from datetime import datetime

PORT = 8000
DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "users.db")

def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            hwid TEXT,
            created_at TEXT NOT NULL,
            last_seen TEXT NOT NULL,
            cloud_cfg TEXT DEFAULT ''
        )
    ''')
    conn.commit()
    conn.close()

class AuthHandler(http.server.BaseHTTPRequestHandler):
    def send_json(self, data, status_code=200):
        body = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(status_code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def send_html(self, html_content, status_code=200):
        body = html_content.encode('utf-8')
        self.send_response(status_code)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == '/' or path == '/index.html' or path == '/admin':
            self.render_dashboard()
            return

        if path == '/api/config/load':
            params = urllib.parse.parse_qs(parsed.query)
            username = params.get('username', [''])[0]
            hwid = params.get('hwid', [''])[0]
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            c.execute('SELECT cloud_cfg FROM users WHERE username = ? AND hwid = ?', (username, hwid))
            row = c.fetchone()
            conn.close()
            if row:
                self.send_json({"status": "ok", "config": row[0]})
            else:
                self.send_json({"status": "error", "message": "User/HWID not found"}, 404)
            return

        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get('Content-Length', 0))
        raw_data = self.rfile.read(length).decode('utf-8') if length > 0 else ""

        # Admin Actions
        if path == '/admin/reset_hwid':
            try:
                params = urllib.parse.parse_qs(raw_data)
                uid = params.get('id', [''])[0]
                conn = sqlite3.connect(DB_FILE)
                c = conn.cursor()
                c.execute('UPDATE users SET hwid = NULL WHERE id = ?', (uid,))
                conn.commit()
                conn.close()
                self.send_response(303)
                self.send_header('Location', '/')
                self.end_headers()
            except Exception as e:
                self.send_html(f"Error: {e}", 500)
            return

        if path == '/admin/delete_user':
            try:
                params = urllib.parse.parse_qs(raw_data)
                uid = params.get('id', [''])[0]
                conn = sqlite3.connect(DB_FILE)
                c = conn.cursor()
                c.execute('DELETE FROM users WHERE id = ?', (uid,))
                conn.commit()
                conn.close()
                self.send_response(303)
                self.send_header('Location', '/')
                self.end_headers()
            except Exception as e:
                self.send_html(f"Error: {e}", 500)
            return

        # API Endpoints (JSON)
        data = {}
        if raw_data:
            try:
                data = json.loads(raw_data)
            except Exception:
                pass

        if path == '/api/register':
            username = data.get('username', '').strip()
            password = data.get('password', '').strip()
            if not username or not password:
                self.send_json({"status": "error", "message": "Логин и пароль обязательны"}, 400)
                return

            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            try:
                c.execute('INSERT INTO users (username, password, hwid, created_at, last_seen) VALUES (?, ?, NULL, ?, ?)',
                          (username, password, now, now))
                conn.commit()
                self.send_json({"status": "ok", "message": "Регистрация успешна!"})
            except sqlite3.IntegrityError:
                self.send_json({"status": "error", "message": "Пользователь с таким логином уже существует"}, 400)
            finally:
                conn.close()
            return

        if path == '/api/login':
            username = data.get('username', '').strip()
            password = data.get('password', '').strip()
            hwid = data.get('hwid', '').strip()
            if not username or not password:
                self.send_json({"status": "error", "message": "Заполните все поля"}, 400)
                return

            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            c.execute('SELECT id, password, hwid, cloud_cfg FROM users WHERE username = ?', (username,))
            row = c.fetchone()
            if not row:
                conn.close()
                self.send_json({"status": "error", "message": "Пользователь не найден"}, 404)
                return

            uid, db_pwd, db_hwid, db_cfg = row
            if db_pwd != password:
                conn.close()
                self.send_json({"status": "error", "message": "Неверный пароль"}, 401)
                return

            # Если HWID еще не привязан — привязываем текущий
            if not db_hwid and hwid:
                c.execute('UPDATE users SET hwid = ?, last_seen = ? WHERE id = ?', (hwid, now, uid))
                conn.commit()
                db_hwid = hwid
            elif db_hwid != hwid:
                conn.close()
                self.send_json({"status": "error", "message": "HWID не совпадает! Обратитесь к администратору."}, 403)
                return
            else:
                c.execute('UPDATE users SET last_seen = ? WHERE id = ?', (now, uid))
                conn.commit()

            conn.close()
            self.send_json({
                "status": "ok",
                "message": "Успешная авторизация",
                "username": username,
                "hwid": db_hwid,
                "cloud_cfg": db_cfg or ""
            })
            return

        if path == '/api/hwid_login':
            hwid = data.get('hwid', '').strip()
            if not hwid:
                self.send_json({"status": "error", "message": "HWID отсутствует"}, 400)
                return

            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            c.execute('SELECT username, cloud_cfg FROM users WHERE hwid = ?', (hwid,))
            row = c.fetchone()
            if row:
                username, cfg = row
                c.execute('UPDATE users SET last_seen = ? WHERE hwid = ?', (now, hwid))
                conn.commit()
                conn.close()
                self.send_json({
                    "status": "ok",
                    "username": username,
                    "cloud_cfg": cfg or ""
                })
            else:
                conn.close()
                self.send_json({"status": "not_found", "message": "HWID не зарегистрирован"})
            return

        if path == '/api/config/save':
            username = data.get('username', '').strip()
            hwid = data.get('hwid', '').strip()
            cfg = data.get('config', '')
            conn = sqlite3.connect(DB_FILE)
            c = conn.cursor()
            c.execute('UPDATE users SET cloud_cfg = ? WHERE username = ? AND hwid = ?', (cfg, username, hwid))
            conn.commit()
            updated = c.rowcount > 0
            conn.close()
            if updated:
                self.send_json({"status": "ok", "message": "Конфиг сохранен в облаке"})
            else:
                self.send_json({"status": "error", "message": "Пользователь не найден"}, 404)
            return

        self.send_response(404)
        self.end_headers()

    def render_dashboard(self):
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('SELECT id, username, password, hwid, created_at, last_seen, LENGTH(cloud_cfg) FROM users ORDER BY id DESC')
        users = c.fetchall()
        conn.close()

        total_users = len(users)
        active_hwids = sum(1 for u in users if u[3])

        user_rows = ""
        for u in users:
            uid, uname, upwd, uhwid, ucreated, ulast, ucfg_len = u
            hwid_badge = f'<span class="badge hwid">{uhwid}</span>' if uhwid else '<span class="badge no-hwid">Не привязан</span>'
            user_rows += f"""
            <tr>
                <td>{uid}</td>
                <td><strong>{uname}</strong></td>
                <td><code class="pwd">{upwd}</code></td>
                <td>{hwid_badge}</td>
                <td>{ucreated}</td>
                <td>{ulast}</td>
                <td>{ucfg_len or 0} B</td>
                <td>
                    <form method="POST" action="/admin/reset_hwid" style="display:inline;">
                        <input type="hidden" name="id" value="{uid}">
                        <button class="btn btn-warning" type="submit" title="Сбросить HWID">Сбросить HWID</button>
                    </form>
                    <form method="POST" action="/admin/delete_user" style="display:inline;" onsubmit="return confirm('Удалить пользователя {uname}?');">
                        <input type="hidden" name="id" value="{uid}">
                        <button class="btn btn-danger" type="submit" title="Удалить">Удалить</button>
                    </form>
                </td>
            </tr>
            """

        if not user_rows:
            user_rows = '<tr><td colspan="8" style="text-align:center; padding: 25px; color:#888;">Пока нет зарегистрированных пользователей</td></tr>'

        html = f"""<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>MyrLegit Database & Auth Server</title>
    <style>
        * {{ box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }}
        body {{ background: #0b0e14; color: #d8e2ec; padding: 30px; }}
        .header {{ display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #1a2233; padding-bottom: 20px; margin-bottom: 30px; }}
        .logo {{ font-size: 26px; font-weight: 800; letter-spacing: 1px; color: #00d2ff; text-transform: uppercase; }}
        .logo span {{ color: #ffffff; }}
        .stats {{ display: flex; gap: 20px; }}
        .stat-card {{ background: #131926; border: 1px solid #1f2a40; border-radius: 8px; padding: 15px 25px; min-width: 170px; }}
        .stat-card .num {{ font-size: 28px; font-weight: 700; color: #00d2ff; }}
        .stat-card .lbl {{ font-size: 13px; color: #8899ac; text-transform: uppercase; letter-spacing: 0.5px; margin-top: 4px; }}
        .container {{ background: #131926; border: 1px solid #1f2a40; border-radius: 10px; overflow: hidden; }}
        .table-title {{ padding: 18px 25px; font-size: 17px; font-weight: 600; background: #171e2e; border-bottom: 1px solid #1f2a40; color: #e1e7f0; }}
        table {{ width: 100%; border-collapse: collapse; text-align: left; font-size: 14px; }}
        th {{ background: #111622; color: #7f93aa; font-weight: 600; padding: 14px 20px; border-bottom: 1px solid #1f2a40; text-transform: uppercase; font-size: 12px; }}
        td {{ padding: 14px 20px; border-bottom: 1px solid #171f30; vertical-align: middle; }}
        tr:hover td {{ background: #161e2f; }}
        .badge {{ padding: 4px 10px; border-radius: 4px; font-size: 12px; font-weight: 600; }}
        .badge.hwid {{ background: rgba(0, 210, 255, 0.15); color: #00d2ff; border: 1px solid rgba(0, 210, 255, 0.3); font-family: Consolas, monospace; }}
        .badge.no-hwid {{ background: rgba(255, 170, 0, 0.15); color: #ffaa00; border: 1px solid rgba(255, 170, 0, 0.3); }}
        .pwd {{ background: #0c0f17; padding: 3px 8px; border-radius: 4px; color: #ff7675; border: 1px solid #232d42; }}
        .btn {{ border: none; padding: 6px 12px; border-radius: 5px; cursor: pointer; font-size: 12px; font-weight: 600; transition: 0.2s; }}
        .btn-warning {{ background: #e17055; color: #fff; margin-right: 6px; }}
        .btn-warning:hover {{ background: #d63031; }}
        .btn-danger {{ background: #2d3436; color: #b2bec3; }}
        .btn-danger:hover {{ background: #d63031; color: #fff; }}
        .footer {{ margin-top: 25px; text-align: center; color: #636e72; font-size: 13px; }}
    </style>
</head>
<body>
    <div class="header">
        <div class="logo">MYR<span>LEGIT</span> <span style="font-size: 14px; color: #7f93aa; font-weight: 400; margin-left: 10px;">Cloud & Auth Server</span></div>
        <div class="stats">
            <div class="stat-card">
                <div class="num">{total_users}</div>
                <div class="lbl">Пользователей</div>
            </div>
            <div class="stat-card">
                <div class="num">{active_hwids}</div>
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
                {user_rows}
            </tbody>
        </table>
    </div>

    <div class="footer">
        Сервер активен на порту <strong>{PORT}</strong> • База данных SQLite: users.db
    </div>
</body>
</html>
"""
        self.send_html(html)

def run():
    init_db()
    with socketserver.TCPServer(("", PORT), AuthHandler) as httpd:
        print(f"=== [MyrLegit Server] Запущен на http://localhost:{PORT} ===")
        print(f"=== База данных: {DB_FILE} ===")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nОстановка сервера...")
            httpd.server_close()

if __name__ == '__main__':
    run()

