# VasyCheat — Minecraft 1.12.2 Internal DLL Cheat

Внутренний чит для Minecraft 1.12.2 (Vanilla / OptiFine).  
Инжектируется через LoadLibraryW в `javaw.exe`.

---

## Возможности

| Функция | Описание |
|---|---|
| **HUD** | `User : ник \| FPS : N \| Ping : N ms` в левом верхнем углу |
| **ClickGUI** | RSHIFT — открыть/закрыть |
| **Выгрузка** | END — выгрузить DLL |

HUD цветовая индикация:
- 🟢 FPS ≥ 60 / Ping < 80 ms
- 🟡 FPS 30–59 / Ping 80–149 ms
- 🔴 FPS < 30 / Ping ≥ 150 ms

---

## Требования для сборки

| Инструмент | Версия |
|---|---|
| Visual Studio | 2022 или 2019 (компонент **Desktop development with C++**) |
| CMake | ≥ 3.20 |
| Git | любая |
| JDK | 8 (для `jni.h`) — задай `JAVA_HOME` |

---

## Сборка

```bat
build.bat
```

Готовые файлы появятся в папке `БИЛДЫ\`:
- `VasyCheat.dll` — основная DLL чита
- `Injector.exe` — инжектор

---

## Использование

1. Запусти **Minecraft 1.12.2**
2. Запусти `БИЛДЫ\Injector.exe` **от Администратора**
3. Подождёт ~3 секунды (инициализация OpenGL)
4. В игре нажми **RSHIFT** для ClickGUI
5. **END** — выгрузить чит

---

## Маппинги Minecraft 1.12.2 (vanilla, MCP stable 39)

| MCP Name | SRG field | Obf class | Obf field |
|---|---|---|---|
| `Minecraft.debugFPS` | `field_71470_ab` | `bib` | `W` (static int) |
| `Minecraft.theMinecraft` | `field_71462_r` | `bib` | `M` (static bib) |
| `Minecraft.mcNetHandler` | `field_71439_g` | `bib` | `ba` (bmo) |
| `NetHandlerPlayClient.playerInfoMap` | `field_147310_g` | `bmo` | `e` (Map) |
| `NetworkPlayerInfo.responseTime` | `field_147090_k` | `bou` | `k` (int) |
| `NetworkPlayerInfo.getGameProfile()` | — | `bou` | `b()` |
| `Session.getUsername()` | — | `az` | `b()` |
| `Minecraft.getSession()` | — | `bib` | `ab()` |

> **⚠️ Если обфусцированные имена не совпадают** — открой `minecraft.jar` в
> [Recaf](https://github.com/Col-E/Recaf) или [jd-gui](http://java-decompiler.github.io/)
> и найди статический `int` с именем похожим на `debugFPS` в классе `Minecraft`.
> OptiFine иногда сдвигает нумерацию полей.

---

## Структура проекта

```
MinecraftCheat/
├── build.bat                   ← Запусти для сборки
├── CMakeLists.txt
├── src/
│   ├── dllmain.cpp             ← Точка входа DLL
│   ├── hooks/
│   │   ├── gl_hook.*           ← Hook wglSwapBuffers (рендер ImGui)
│   │   └── wnd_hook.*          ← Hook WndProc (ввод ImGui)
│   ├── jvm/
│   │   └── jvm_bridge.*        ← JNI: FPS, Ping, Username
│   ├── gui/
│   │   ├── gui_manager.*       ← Главный менеджер GUI
│   │   ├── clickgui.*          ← ClickGUI окно
│   │   └── hud.*               ← HUD панель
│   └── modules/
│       ├── module_base.h       ← Базовый класс модуля
│       └── module_hud.h        ← Модуль HUD
├── injector/
│   └── main.cpp                ← Injector.exe
└── ../БИЛДЫ/                   ← Выходная папка
```

---

## Добавление нового модуля

1. Создай `src/modules/module_xxx.h` наследуя `ModuleBase`
2. Добавь его в `ClickGUI::ClickGUI()` в `clickgui.cpp`
3. Реализуй `OnEnable()`, `OnDisable()`, `OnTick()`

