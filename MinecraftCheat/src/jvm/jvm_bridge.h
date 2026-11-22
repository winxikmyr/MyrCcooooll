// src/jvm/jvm_bridge.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <jni.h>
#include <windows.h>

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct ArmorItem {
    std::string name;
    int         itemId     = 0;
    int         count      = 1;
    int         curDamage  = 0;
    int         maxDamage  = 0;
    float       durability = 1.0f;
    bool        isEmpty    = true;
    bool        hasIcon    = false;
    float       minU       = 0.0f;
    float       maxU       = 0.0f;
    float       minV       = 0.0f;
    float       maxV       = 0.0f;
    std::vector<std::string> enchantments;
};

struct DroppedItemInfo {
    Vec3        pos;
    float       distance = 0.0f;
    std::string name;
    int         itemId   = 0;
    int         count    = 1;
    bool        hasIcon  = false;
    float       minU     = 0.0f;
    float       maxU     = 0.0f;
    float       minV     = 0.0f;
    float       maxV     = 0.0f;
    bool        isGroundStand = false;
};

struct PotionInfo {
    std::string name;
    unsigned int color = 0xFFFFFFFF;
};

struct PlayerTagInfo {
    int         entId      = 0;
    std::string uuidStr;
    std::string name;
    std::string realName;
    std::string rankPrefix;
    std::string clanSuffix;
    bool        isBot      = false;
    bool        isZoomed   = false;
    bool        canBeSeen  = true;
    Vec3        pos;
    Vec3        rawPos;        // posX, posY, posZ
    Vec3        lastRender;    // lastTickPosX, lastTickPosY, lastTickPosZ
    Vec3        prevTick;      // prevPosX, prevPosY, prevPosZ
    Vec3        vel{ 0.0, 0.0, 0.0 };
    float       yaw        = 0.0f;
    double      width      = 0.6;
    double      height     = 1.8;
    float       distance   = 0.0f;
    float       health     = 20.0f;
    float       maxHealth  = 20.0f;
    bool        isSneaking = false;
    ArmorItem   armor[4];  // 0=шлем, 1=нагрудник, 2=поножи, 3=ботинки
    ArmorItem   heldItem;
    std::vector<ArmorItem>  items; // mainhand, offhand, boots, legs, chest, helm
    std::vector<PotionInfo> potions;
};

struct BlockESPInfo {
    Vec3         pos;
    double       red   = 1.0;
    double       green = 1.0;
    double       blue  = 1.0;
    unsigned int color = 0xFFFFFFFF;
    std::string  name;
    bool         isEntity = false;
};

struct AutoMineTarget {
    Vec3 standPos; // Позиция стойки брони (искра/метка)
    Vec3 blockPos; // Позиция реального твердого блока руды рядом
};

struct MCData {
    int         fps        = 0;
    int         ping       = -1;
    std::string username   = "Unknown";
    bool        isInWorld  = false;
    bool        hasScreen  = false;
    bool        isChatOpen = false;
    bool        isTitleActive = false;
    int         guiScale   = 2;
    float       fov        = 70.0f;
    int         thirdPersonView = 0; // 0 = 1st person, 1 = 3rd person back, 2 = 3rd person front
    int         currentItem = 0;     // Выбранный слот хотбара (0..8)
    unsigned int atlasTextureId = 0; // OpenGL ID текстурного атласа предметов/блоков Minecraft

    Vec3        playerPos; // Точные координаты ног локального игрока
    Vec3        camPos;
    float       camYaw     = 0.0f;
    float       camPitch   = 0.0f;
    float       timerPartialTicks = 1.0f;

    // Локальный игрок: 0=Шлем, 1=Нагрудник, 2=Поножи, 3=Ботинки, 4=В руке
    ArmorItem   equipment[5];
    ArmorItem   hotbar[9];          // 9 слотов хотбара (mainInventory 0..8)
    ArmorItem   mainInventory[27];  // 27 слотов инвентаря (mainInventory 9..35)
    ArmorItem   offHandItem;        // Предмет во второй руке (offHandInventory 0)
    float       experience = 0.f;   // Шкала опыта (0.0..1.0)
    int         experienceLevel = 0;// Числовой уровень опыта

    // Мир
    std::vector<DroppedItemInfo> droppedItems;
    std::vector<PlayerTagInfo>   players;
    std::vector<PlayerTagInfo>   mobs;        // Враждебные мобы (скелеты, зомби, криперы, пауки и т.д.)
    std::vector<PlayerTagInfo>   animals;     // Мирные животные (коровы, овцы, свиньи и т.д.)
    std::vector<Vec3>            armorStands; // Позиции стоек брони для WallHack
    std::vector<AutoMineTarget>  mineTargets; // Стойки с твердыми блоками рядом для AutoMine
    std::vector<BlockESPInfo>    espBlocks;        // Найденные руды и растения из чанков для BlockESP
    std::vector<BlockESPInfo>    entityOreBlocks;  // Найденные жилы руд (RustEx nodes) для BlockESP

    // Список игроков на сервере (TAB) с пингом: (никнейм, пинг)
    std::vector<std::pair<std::string, int>> tabPlayers;
};

class JVMBridge {
public:
    static JVMBridge& Get() { static JVMBridge i; return i; }

    bool Initialize();
    void Shutdown();
    void Tick();

    // Преобразование мировых 3D-координат в 2D-координаты экрана
    bool WorldToScreen(const Vec3& worldPos, Vec2& screenOut) const;

    // Запись проекционной матрицы напрямую из перехвата OpenGL (Zoom OptiFine / AspectRatio)
    void SetCapturedProjection(const float* proj);

    // Запись аппаратных матриц напрямую из перехвата рендера OpenGL (glClear/glOrtho)
    void SetGLMatrices(const float* mv, const float* proj, const int* vp);

    // Обновление матриц рендера из ActiveRenderInfo (bhv)
    void UpdateRenderMatrices(JNIEnv* env = nullptr);

    // Нативный рендер ArmorHud из ArmorHud.java
    void RenderNativeArmorHUD(int yOffset = 55);

    // Нативный рендер иконок предметов для ItemESP (только иконки)
    void RenderNativeItemESP(float range, float iconSize);

    // Управление игроком и пакетами (AimBot, AutoMine, GuiWalk, MCF)
    void SetPlayerRotation(float yaw, float pitch);
    void PlayerSwingArm();
    void SendDiggingPacket(int x, int y, int z, int facing = 0);
    void UpdateGuiWalkKeys(bool forward, bool back, bool left, bool right, bool jump);
    void ResetGuiWalkKeys();
    std::string QueryPointedPlayer();
    bool QueryAutoMineTarget(float maxDist, Vec3& outBlockPos, Vec3& outAimPos);

    const MCData& Data() const { return m_data; }
    void ClearExceptions();

    void SetAspectRatio(float asp) { m_aspectRatio = asp; }
    float GetAspectRatio() const { return m_aspectRatio; }

    void SetBlockESPSettings(bool enabled, float range, bool ores, bool food) {
        m_blockEspEnabled = enabled;
        m_blockEspRange = range;
        m_blockEspOres = ores;
        m_blockEspFood = food;
    }

    void SetAutoMineEnabled(bool enabled) {
        m_autoMineEnabled = enabled;
    }

    void SanitizeChat(const std::string& targetName, const std::string& replacement);
    void SanitizeNameProtect(const std::string& myNick, const std::string& protectedNick, const std::vector<std::string>& friends, bool protectFriends);
    void SetHideVanillaCrosshair(bool hide);

private:
    JVMBridge() = default;

    bool     AttachJVM();
    JNIEnv*  GetJNIEnv();
    void     CacheRefs(JNIEnv* env);
    jobject  GetMinecraftInstance(JNIEnv* env);

    int         QueryFPS(JNIEnv* env);
    int         QueryPing(JNIEnv* env);
    void        UpdateOfflineTabNames(JNIEnv* env);
    std::string QueryUsername(JNIEnv* env);
    void        QueryPlayerEquipment(JNIEnv* env);
    bool        QueryIsChatOpen(JNIEnv* env);
    bool        QueryIsTitleActive(JNIEnv* env);
    int         QueryGuiScale(JNIEnv* env);
    void        QueryWorldEntities(JNIEnv* env);
    void        ScanBlockESP(JNIEnv* env, float range, bool scanOres, bool scanFood);

    JavaVM*   m_jvm        = nullptr;
    bool      m_ok         = false;
    bool      m_refsCached = false;

    jclass    m_cMC        = nullptr;
    jobject   m_classLoader = nullptr;
    jfieldID  m_fFPS       = nullptr;
    jmethodID m_mFPS       = nullptr;
    jfieldID  m_fInst      = nullptr;
    jmethodID m_mGetMC     = nullptr;

    // Матрицы камеры Minecraft (из bhv - ActiveRenderInfo)
    float     m_modelView[16]{};
    float     m_projection[16]{};
    int       m_viewport[4]{};
    Vec3      m_camPos;
    bool      m_hasMatrices = false;
    DWORD     m_lastProjCapture = 0;
    float     m_aspectRatio = 1.0f;

    bool      m_blockEspEnabled = false;
    float     m_blockEspRange   = 300.0f;
    bool      m_blockEspOres    = true;
    bool      m_blockEspFood    = true;
    bool      m_autoMineEnabled = false;

    // Многопоточный пул воркеров для параллельного сканирования BlockESP на ВСЕХ ядрах процессора
    void StartBlockESPWorker();
    void StopBlockESPWorker();
    void BlockESPWorkerLoop(unsigned int threadIndex, unsigned int totalThreads);
    void ScanBlockESPParallel(JNIEnv* env, float range, bool scanOres, bool scanFood, unsigned int threadIndex, unsigned int totalThreads);

    std::vector<std::thread>  m_blockEspWorkers;
    std::atomic<bool>         m_blockEspRunning{false};
    std::mutex                m_blockEspMutex;
    std::vector<BlockESPInfo> m_pendingBlocks;
    std::vector<BlockESPInfo> m_workerBlocks[16];
    unsigned int              m_numWorkers = 4;

    MCData    m_data;
    std::unordered_map<std::string, std::string> m_offlineTabNames;
    std::unordered_map<int, std::string>         m_knownPlayerNamesByEntId;
    std::unordered_map<std::string, std::string> m_knownPlayerNamesByUUID;

    struct PlayerVelTracker {
        Vec3 lastPos{ 0.0, 0.0, 0.0 };
        Vec3 smoothVel{ 0.0, 0.0, 0.0 };
        DWORD lastTime = 0;
    };
    std::unordered_map<int, PlayerVelTracker>    m_playerTrackers;

    DWORD     m_lastTabParseTime = 0;
    DWORD     m_lastTick   = 0;
    int       m_tick       = 0;
};
