// src/jvm/jvm_bridge.cpp
#include "jvm_bridge.h"
#include <Windows.h>
#include <vector>
#include <imgui.h>
#include <GL/gl.h>
#include <unordered_map>
#include <unordered_set>
#include <cctype>

static const std::unordered_map<int, const char*> CUSTOM_ITEM_IDS = {
    { 1125, "Stone" },
    { 1396, "Bow" },
    { 1451, "Stone Pickaxe" },
    { 190,  "Machete" },
    { 367,  "Stone Spear" },
    { 711,  "Stone Axe" },
    { 191,  "Bone Knife" },
    { 245,  "Iron Axe" },
    { 249,  "Crossbow" },
    { 57,   "Flint" },
    { 232,  "Wooden Spear" },
    { 10,   "Pawn" }
};

using PFN_GetCreatedJVMs = jint(JNICALL*)(JavaVM**, jsize, jsize*);

bool JVMBridge::Initialize() {
    bool ok = AttachJVM();
    if (ok) {
        StartBlockESPWorker();
    }
    return ok;
}

void JVMBridge::Shutdown() {
    StopBlockESPWorker();
    if (m_jvm) {
        JNIEnv* env = GetJNIEnv();
        if (env) {
            if (m_cMC) {
                env->DeleteGlobalRef(m_cMC);
                m_cMC = nullptr;
            }
            if (m_classLoader) {
                env->DeleteGlobalRef(m_classLoader);
                m_classLoader = nullptr;
            }
        }
    }
    m_ok = false;
    m_refsCached = false;
}

void JVMBridge::StartBlockESPWorker() {
    if (m_blockEspRunning) return;
    m_blockEspRunning = true;

    // Задействуем все доступные физические и логические ядра процессора (от 2 до 16 потоков)
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw < 2u) hw = 2u;
    if (hw > 16u) hw = 16u;
    m_numWorkers = hw;

    for (unsigned int i = 0; i < m_numWorkers; ++i) {
        m_blockEspWorkers.emplace_back(&JVMBridge::BlockESPWorkerLoop, this, i, m_numWorkers);
    }
}

void JVMBridge::StopBlockESPWorker() {
    if (!m_blockEspRunning) return;
    m_blockEspRunning = false;
    for (auto& t : m_blockEspWorkers) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_blockEspWorkers.clear();
}

void JVMBridge::BlockESPWorkerLoop(unsigned int threadIndex, unsigned int totalThreads) {
    JNIEnv* env = nullptr;
    if (!m_jvm) return;
    if (m_jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK || !env) {
        return;
    }

    if (m_classLoader) {
        jclass cThread = env->FindClass("java/lang/Thread");
        if (cThread) {
            jmethodID mCur = env->GetStaticMethodID(cThread, "currentThread", "()Ljava/lang/Thread;");
            jmethodID mSetCCL = env->GetMethodID(cThread, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");
            if (mCur && mSetCCL) {
                jobject curT = env->CallStaticObjectMethod(cThread, mCur);
                if (curT && !env->ExceptionCheck()) {
                    env->CallVoidMethod(curT, mSetCCL, m_classLoader);
                    env->DeleteLocalRef(curT);
                } else if (env->ExceptionCheck()) env->ExceptionClear();
            }
            env->DeleteLocalRef(cThread);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    }

    while (m_blockEspRunning) {
        if (m_blockEspEnabled && (m_blockEspOres || m_blockEspFood) && m_ok) {
            ScanBlockESPParallel(env, m_blockEspRange, m_blockEspOres, m_blockEspFood, threadIndex, totalThreads);
        } else {
            std::lock_guard<std::mutex> lock(m_blockEspMutex);
            if (threadIndex < 16) m_workerBlocks[threadIndex].clear();
        }

        // Оптимальный интервал между параллельными сканированиями для нулевой нагрузки на CPU
        for (int i = 0; i < 20 && m_blockEspRunning; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    m_jvm->DetachCurrentThread();
}

bool JVMBridge::AttachJVM() {
    HMODULE jvmDll = GetModuleHandleA("jvm.dll");
    if (!jvmDll) jvmDll = LoadLibraryA("jvm.dll");
    if (!jvmDll) return false;

    auto getJVMs = reinterpret_cast<PFN_GetCreatedJVMs>(
        GetProcAddress(jvmDll, "JNI_GetCreatedJavaVMs"));
    if (!getJVMs) return false;

    jsize count = 0;
    JavaVM* vm = nullptr;
    if (getJVMs(&vm, 1, &count) != JNI_OK || count == 0) return false;

    m_jvm = vm;
    m_ok = true;
    return true;
}

JNIEnv* JVMBridge::GetJNIEnv() {
    if (!m_jvm) return nullptr;
    JNIEnv* env = nullptr;
    jint res = m_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
    if (res == JNI_OK && env) return env;
    if (res == JNI_EDETACHED) {
        if (m_jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK) {
            return env;
        }
    }
    return nullptr;
}

static jobject GetMinecraftClassLoader(JNIEnv* env) {
    jclass cThread = env->FindClass("java/lang/Thread");
    if (!cThread) { env->ExceptionClear(); return nullptr; }

    jmethodID mCurThread = env->GetStaticMethodID(cThread, "currentThread", "()Ljava/lang/Thread;");
    if (!mCurThread) { env->ExceptionClear(); env->DeleteLocalRef(cThread); return nullptr; }

    jobject curThread = env->CallStaticObjectMethod(cThread, mCurThread);
    env->DeleteLocalRef(cThread);
    if (!curThread || env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }

    jclass curThreadCls = env->GetObjectClass(curThread);
    jmethodID mGetCCL = env->GetMethodID(curThreadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    env->DeleteLocalRef(curThreadCls);
    if (!mGetCCL) { env->ExceptionClear(); env->DeleteLocalRef(curThread); return nullptr; }

    jobject cl = env->CallObjectMethod(curThread, mGetCCL);
    env->DeleteLocalRef(curThread);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return cl;
}

static jclass FindMCClass(JNIEnv* env, jobject classLoader, const char* name) {
    if (!env || !name) return nullptr;

    // Сначала пробуем загрузить через LaunchClassLoader Minecraft (чтобы совпадал класс-лоадер с Netty)
    if (classLoader) {
        jclass cCL = env->GetObjectClass(classLoader);
        if (cCL) {
            jmethodID mLoad = env->GetMethodID(cCL, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            env->DeleteLocalRef(cCL);
            if (mLoad) {
                std::string dotName = name;
                for (char& c : dotName) if (c == '/') c = '.';
                jstring jName = env->NewStringUTF(dotName.c_str());
                jclass loaded = reinterpret_cast<jclass>(env->CallObjectMethod(classLoader, mLoad, jName));
                env->DeleteLocalRef(jName);
                if (!env->ExceptionCheck() && loaded) {
                    return loaded;
                }
                env->ExceptionClear();
            }
        }
    }

    // Fallback на FindClass JVM
    jclass cls = env->FindClass(name);
    if (cls) return cls;
    env->ExceptionClear();

    return nullptr;
}

void JVMBridge::CacheRefs(JNIEnv* env) {
    if (!env) return;

    jobject cl = GetMinecraftClassLoader(env);
    if (cl) {
        if (m_classLoader) {
            env->DeleteGlobalRef(m_classLoader);
            m_classLoader = nullptr;
        }
        m_classLoader = env->NewGlobalRef(cl);
    }

    jclass localMC = FindMCClass(env, cl, "bib");
    if (!localMC) {
        localMC = FindMCClass(env, cl, "net/minecraft/client/Minecraft");
    }

    if (cl) env->DeleteLocalRef(cl);

    if (!localMC) return;

    m_cMC = reinterpret_cast<jclass>(env->NewGlobalRef(localMC));
    env->DeleteLocalRef(localMC);

    // FPS (debugFPS в MCP, field_71470_ab, или notch-имена 1.12.2: ak, ai, al, aj, am, an, W)
    const char* fpsCandidates[] = { "debugFPS", "field_71470_ab", "ak", "ai", "al", "aj", "am", "an", "ao", "W", "X", "Y" };
    for (const char* f : fpsCandidates) {
        m_fFPS = env->GetStaticFieldID(m_cMC, f, "I");
        if (m_fFPS) break;
        env->ExceptionClear();
    }

    // theMinecraft instance field
    const char* instCandidates[] = { "z", "field_71432_P", "instance", "theMinecraft", "M", "field_71462_r" };
    for (const char* f : instCandidates) {
        m_fInst = env->GetStaticFieldID(m_cMC, f, "Lbib;");
        if (!m_fInst) {
            env->ExceptionClear();
            m_fInst = env->GetStaticFieldID(m_cMC, f, "Lnet/minecraft/client/Minecraft;");
        }
        if (m_fInst) break;
        env->ExceptionClear();
    }

    // theMinecraft static method: Minecraft.getMinecraft()
    const char* methodCandidates[] = { "z", "getMinecraft", "func_71410_x" };
    for (const char* m : methodCandidates) {
        m_mGetMC = env->GetStaticMethodID(m_cMC, m, "()Lbib;");
        if (!m_mGetMC) {
            env->ExceptionClear();
            m_mGetMC = env->GetStaticMethodID(m_cMC, m, "()Lnet/minecraft/client/Minecraft;");
        }
        if (m_mGetMC) break;
        env->ExceptionClear();
    }

    m_refsCached = (m_cMC != nullptr);
}

int JVMBridge::QueryFPS(JNIEnv* env) {
    if (!m_cMC || !m_fFPS) return 0;
    jint v = env->GetStaticIntField(m_cMC, m_fFPS);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    return static_cast<int>(v);
}

jobject JVMBridge::GetMinecraftInstance(JNIEnv* env) {
    if (!m_cMC) return nullptr;

    // 1. Через метод getMinecraft() (самый надежный способ во всех версиях)
    if (m_mGetMC) {
        jobject mc = env->CallStaticObjectMethod(m_cMC, m_mGetMC);
        if (mc && !env->ExceptionCheck()) return mc;
        env->ExceptionClear();
    }

    // 2. Через статическое поле instance
    if (m_fInst) {
        jobject mc = env->GetStaticObjectField(m_cMC, m_fInst);
        if (mc && !env->ExceptionCheck()) return mc;
        env->ExceptionClear();
    }

    // 3. Динамический поиск на лету если кэш промахнулся
    const char* methodCandidates[] = { "z", "getMinecraft", "func_71410_x" };
    for (const char* m : methodCandidates) {
        jmethodID mGet = env->GetStaticMethodID(m_cMC, m, "()Lbib;");
        if (!mGet) {
            env->ExceptionClear();
            mGet = env->GetStaticMethodID(m_cMC, m, "()Lnet/minecraft/client/Minecraft;");
        }
        if (mGet) {
            m_mGetMC = mGet;
            jobject mc = env->CallStaticObjectMethod(m_cMC, mGet);
            if (mc && !env->ExceptionCheck()) return mc;
            env->ExceptionClear();
        }
    }

    const char* instCandidates[] = { "z", "field_71432_P", "instance", "theMinecraft", "M" };
    for (const char* f : instCandidates) {
        jfieldID fld = env->GetStaticFieldID(m_cMC, f, "Lbib;");
        if (!fld) {
            env->ExceptionClear();
            fld = env->GetStaticFieldID(m_cMC, f, "Lnet/minecraft/client/Minecraft;");
        }
        if (fld) {
            m_fInst = fld;
            jobject mc = env->GetStaticObjectField(m_cMC, fld);
            if (mc && !env->ExceptionCheck()) return mc;
            env->ExceptionClear();
        }
    }

    return nullptr;
}

std::string JVMBridge::QueryUsername(JNIEnv* env) {
    jobject mc = GetMinecraftInstance(env);
    if (!mc) return "Unknown";

    jclass cMC = env->GetObjectClass(mc);
    std::string username = "Unknown";

    // 1. Приоритет 1: mc.player (bud / EntityPlayerSP) -> getName()
    const char* playerFields[] = { "h", "player", "field_71439_g" };
    for (const char* pf : playerFields) {
        jfieldID fPl = env->GetFieldID(cMC, pf, "Lbud;");
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, pf, "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, pf, "Laed;"); }
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, pf, "Lnet/minecraft/entity/player/EntityPlayer;"); }
        if (fPl) {
            jobject pl = env->GetObjectField(mc, fPl);
            if (pl && !env->ExceptionCheck()) {
                jclass cPl = env->GetObjectClass(pl);
                const char* nameMethods[] = { "h_", "getName", "func_70005_c_" };
                for (const char* nm : nameMethods) {
                    jmethodID mName = env->GetMethodID(cPl, nm, "()Ljava/lang/String;");
                    if (mName) {
                        auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(pl, mName));
                        if (jStr && !env->ExceptionCheck()) {
                            const char* chars = env->GetStringUTFChars(jStr, nullptr);
                            if (chars && strlen(chars) > 0 && strcmp(chars, "Unknown") != 0) {
                                username = chars;
                                env->ReleaseStringUTFChars(jStr, chars);
                                env->DeleteLocalRef(jStr);
                                env->DeleteLocalRef(cPl);
                                env->DeleteLocalRef(pl);
                                env->DeleteLocalRef(cMC);
                                env->DeleteLocalRef(mc);
                                return username;
                            }
                            if (chars) env->ReleaseStringUTFChars(jStr, chars);
                            env->DeleteLocalRef(jStr);
                        } else env->ExceptionClear();
                    } else env->ExceptionClear();
                }
                env->DeleteLocalRef(cPl);
                env->DeleteLocalRef(pl);
            } else env->ExceptionClear();
        }
    }

    // 2. Приоритет 2: mc.session (K / Session) -> getUsername()
    const char* sessFields[] = { "K", "session", "field_71449_j" };
    for (const char* sf : sessFields) {
        jfieldID fSess = env->GetFieldID(cMC, sf, "Lcub;");
        if (!fSess) { env->ExceptionClear(); fSess = env->GetFieldID(cMC, sf, "Lnet/minecraft/util/Session;"); }
        if (fSess) {
            jobject sess = env->GetObjectField(mc, fSess);
            if (sess && !env->ExceptionCheck()) {
                jclass cSess = env->GetObjectClass(sess);
                const char* sessMethods[] = { "c", "getUsername", "func_111285_a" };
                for (const char* sm : sessMethods) {
                    jmethodID mGet = env->GetMethodID(cSess, sm, "()Ljava/lang/String;");
                    if (mGet) {
                        auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(sess, mGet));
                        if (jStr && !env->ExceptionCheck()) {
                            const char* chars = env->GetStringUTFChars(jStr, nullptr);
                            if (chars && strlen(chars) > 0 && strcmp(chars, "Unknown") != 0) {
                                username = chars;
                                env->ReleaseStringUTFChars(jStr, chars);
                                env->DeleteLocalRef(jStr);
                                env->DeleteLocalRef(cSess);
                                env->DeleteLocalRef(sess);
                                env->DeleteLocalRef(cMC);
                                env->DeleteLocalRef(mc);
                                return username;
                            }
                            if (chars) env->ReleaseStringUTFChars(jStr, chars);
                            env->DeleteLocalRef(jStr);
                        } else env->ExceptionClear();
                    } else env->ExceptionClear();
                }
                env->DeleteLocalRef(cSess);
                env->DeleteLocalRef(sess);
            } else env->ExceptionClear();
        }
    }

    // 3. Fallback: getSession() метод
    jmethodID mGetSess = env->GetMethodID(cMC, "ab", "()Laz;");
    if (!mGetSess) {
        env->ExceptionClear();
        mGetSess = env->GetMethodID(cMC, "getSession", "()Lnet/minecraft/util/Session;");
    }

    if (mGetSess) {
        jobject sess = env->CallObjectMethod(mc, mGetSess);
        if (sess && !env->ExceptionCheck()) {
            jclass cSess = env->GetObjectClass(sess);
            jmethodID mGetName = env->GetMethodID(cSess, "b", "()Ljava/lang/String;");
            if (!mGetName) {
                env->ExceptionClear();
                mGetName = env->GetMethodID(cSess, "getUsername", "()Ljava/lang/String;");
            }

            if (mGetName) {
                auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(sess, mGetName));
                if (jStr && !env->ExceptionCheck()) {
                    const char* chars = env->GetStringUTFChars(jStr, nullptr);
                    if (chars && strlen(chars) > 0) {
                        username = chars;
                        env->ReleaseStringUTFChars(jStr, chars);
                    }
                    env->DeleteLocalRef(jStr);
                } else env->ExceptionClear();
            }
            env->DeleteLocalRef(cSess);
            env->DeleteLocalRef(sess);
        } else env->ExceptionClear();
    }

    // 4. Fallback: mc.player.getName() (h_ / getName)
    if (username.empty() || username == "Unknown") {
        jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (fPlayer) {
            jobject player = env->GetObjectField(mc, fPlayer);
            if (player && !env->ExceptionCheck()) {
                jclass cPlayer = env->GetObjectClass(player);
                const char* nameMethods[] = { "h_", "getName", "func_70005_c_", "getCommandSenderName" };
                for (const char* nm : nameMethods) {
                    jmethodID mGetName = env->GetMethodID(cPlayer, nm, "()Ljava/lang/String;");
                    if (mGetName) {
                        auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(player, mGetName));
                        if (jStr && !env->ExceptionCheck()) {
                            const char* chars = env->GetStringUTFChars(jStr, nullptr);
                            if (chars && strlen(chars) > 0 && strcmp(chars, "Unknown") != 0) {
                                username = chars;
                                env->ReleaseStringUTFChars(jStr, chars);
                                env->DeleteLocalRef(jStr);
                                break;
                            }
                            if (chars) env->ReleaseStringUTFChars(jStr, chars);
                            env->DeleteLocalRef(jStr);
                        } else env->ExceptionClear();
                    } else env->ExceptionClear();
                }
                env->DeleteLocalRef(cPlayer);
                env->DeleteLocalRef(player);
            } else env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);
    return username;
}

static bool IsValidPlayerName(const std::string& name) {
    if (name.length() < 3 || name.length() > 16) return false;
    for (char c : name) {
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    std::string low = name;
    for (char& lc : low) lc = static_cast<char>(tolower(static_cast<unsigned char>(lc)));
    if (low == "player" || low == "unknown" || low == "ground" ||
        low == "bot" || low == "npc" || low.rfind("slot", 0) == 0 ||
        low == "vip" || low == "admin" || low == "owner" || low == "mod" ||
        low == "helper" || low == "media" || low == "legend" || low == "prem" || low == "yt") {
        return false;
    }
    return true;
}

static std::string ExtractRealPlayerName(const std::string& raw, const std::string& localPlayerName = "") {
    if (raw.empty()) return "";
    std::string clean;
    clean.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ) {
        unsigned char c1 = static_cast<unsigned char>(raw[i]);
        if (c1 == 0xC2 && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xA7) {
            i += 2;
            if (i < raw.size()) {
                unsigned char c2 = static_cast<unsigned char>(raw[i]);
                if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z')) i += 1;
            }
            continue;
        }
        if (c1 == 0xA7) {
            i += 1;
            if (i < raw.size()) {
                unsigned char c2 = static_cast<unsigned char>(raw[i]);
                if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z')) i += 1;
            }
            continue;
        }
        if (c1 == '&' && i + 1 < raw.size()) {
            char c2 = raw[i + 1];
            if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'f') || (c2 >= 'k' && c2 <= 'o') || c2 == 'r' || (c2 >= 'A' && c2 <= 'F') || c2 == 'R') {
                i += 2;
                continue;
            }
        }
        clean.push_back(raw[i]);
        i += 1;
    }

    std::string noBrackets;
    noBrackets.reserve(clean.size());
    bool inBracket = false;
    for (char c : clean) {
        if (c == '[' || c == '(') inBracket = true;
        else if (c == ']' || c == ')') { inBracket = false; noBrackets.push_back(' '); }
        else if (!inBracket) noBrackets.push_back(c);
    }

    std::vector<std::string> candidates;
    std::string token;
    for (size_t i = 0; i <= noBrackets.size(); ++i) {
        char c = (i < noBrackets.size()) ? noBrackets[i] : ' ';
        if (isalnum(static_cast<unsigned char>(c)) || c == '_') {
            token.push_back(c);
        } else {
            if (IsValidPlayerName(token)) {
                candidates.push_back(token);
            }
            token.clear();
        }
    }

    if (candidates.empty()) return "";

    std::string lowerLocal = localPlayerName;
    for (char& lc : lowerLocal) lc = static_cast<char>(tolower(static_cast<unsigned char>(lc)));

    for (int i = static_cast<int>(candidates.size()) - 1; i >= 0; --i) {
        std::string candLow = candidates[i];
        for (char& lc : candLow) lc = static_cast<char>(tolower(static_cast<unsigned char>(lc)));
        if (lowerLocal.empty() || candLow != lowerLocal) {
            return candidates[i];
        }
    }
    return candidates.back();
}

int JVMBridge::QueryPing(JNIEnv* env) {
    jobject mc = GetMinecraftInstance(env);
    if (!mc) return -1;

    jclass cMC = env->GetObjectClass(mc);

    // 0. Проверяем одиночную игру (Singleplayer) — пинг всегда 0
    jmethodID mIsSP = env->GetMethodID(cMC, "E", "()Z");
    if (!mIsSP) { env->ExceptionClear(); mIsSP = env->GetMethodID(cMC, "isSingleplayer", "()Z"); }
    if (!mIsSP) { env->ExceptionClear(); mIsSP = env->GetMethodID(cMC, "func_71356_B", "()Z"); }
    if (mIsSP && env->CallBooleanMethod(mc, mIsSP)) {
        env->DeleteLocalRef(cMC);
        env->DeleteLocalRef(mc);
        m_data.ping = 0;
        return 0;
    }

    jobject localPlayer = nullptr;
    jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
    if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
    if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
    if (fPlayer) {
        localPlayer = env->GetObjectField(mc, fPlayer);
        if (env->ExceptionCheck()) { env->ExceptionClear(); localPlayer = nullptr; }
    }

    jobject conn = nullptr;
    // Способ 1: через mc.v() -> brz (NetHandlerPlayClient в 1.12.2)
    jmethodID mGetConn = env->GetMethodID(cMC, "v", "()Lbrz;");
    if (!mGetConn) { env->ExceptionClear(); mGetConn = env->GetMethodID(cMC, "getConnection", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
    if (!mGetConn) { env->ExceptionClear(); mGetConn = env->GetMethodID(cMC, "func_147114_u", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
    if (mGetConn) {
        conn = env->CallObjectMethod(mc, mGetConn);
        if (env->ExceptionCheck()) { env->ExceptionClear(); conn = nullptr; }
    }

    // Способ 2: через mc.player.d (bud.d) -> brz
    if (!conn && localPlayer) {
        jclass cPlayerLP = env->GetObjectClass(localPlayer);
        if (cPlayerLP) {
            jfieldID fPConn = env->GetFieldID(cPlayerLP, "d", "Lbrz;");
            if (!fPConn) { env->ExceptionClear(); fPConn = env->GetFieldID(cPlayerLP, "connection", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
            if (!fPConn) { env->ExceptionClear(); fPConn = env->GetFieldID(cPlayerLP, "field_71174_a", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
            if (fPConn) {
                conn = env->GetObjectField(localPlayer, fPConn);
                if (env->ExceptionCheck()) { env->ExceptionClear(); conn = nullptr; }
            }
            env->DeleteLocalRef(cPlayerLP);
        }
    }

    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);

    if (!conn) {
        if (localPlayer) env->DeleteLocalRef(localPlayer);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return -1;
    }

    int ping = -1;

    // Прямое чтение ping через UniqueID локального игрока (1-в-1 как F3 и сетевой стек клиента)
    if (localPlayer) {
        jclass cPlayer = env->GetObjectClass(localPlayer);
        jmethodID mGetUUID = env->GetMethodID(cPlayer, "bm", "()Ljava/util/UUID;");
        if (!mGetUUID) { env->ExceptionClear(); mGetUUID = env->GetMethodID(cPlayer, "getUniqueID", "()Ljava/util/UUID;"); }
        if (!mGetUUID) { env->ExceptionClear(); mGetUUID = env->GetMethodID(cPlayer, "func_110124_au", "()Ljava/util/UUID;"); }

        jobject uObj = mGetUUID ? env->CallObjectMethod(localPlayer, mGetUUID) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); uObj = nullptr; }

        if (uObj) {
            jclass cConn = env->GetObjectClass(conn);
            jmethodID mGetNPI = env->GetMethodID(cConn, "a", "(Ljava/util/UUID;)Lbsc;");
            if (!mGetNPI) { env->ExceptionClear(); mGetNPI = env->GetMethodID(cConn, "getPlayerInfo", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;"); }
            if (!mGetNPI) { env->ExceptionClear(); mGetNPI = env->GetMethodID(cConn, "func_175102_a", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;"); }

            if (mGetNPI) {
                jobject npi = env->CallObjectMethod(conn, mGetNPI, uObj);
                if (npi && !env->ExceptionCheck()) {
                    jclass cNPI = env->GetObjectClass(npi);
                    jmethodID mPing = env->GetMethodID(cNPI, "c", "()I");
                    if (!mPing) { env->ExceptionClear(); mPing = env->GetMethodID(cNPI, "getResponseTime", "()I"); }
                    if (!mPing) { env->ExceptionClear(); mPing = env->GetMethodID(cNPI, "func_178853_c", "()I"); }

                    if (mPing) {
                        ping = env->CallIntMethod(npi, mPing);
                        if (ping < 0) ping = 0;
                    }
                    env->DeleteLocalRef(cNPI);
                    env->DeleteLocalRef(npi);
                } else env->ExceptionClear();
            }
            env->DeleteLocalRef(cConn);
            env->DeleteLocalRef(uObj);
        }
        env->DeleteLocalRef(cPlayer);
        env->DeleteLocalRef(localPlayer);
    }

    if (ping >= 0) {
        env->DeleteLocalRef(conn);
        m_data.ping = ping;
        return ping;
    }

    jclass cNH = env->GetObjectClass(conn);
    jobject vals = nullptr;

    // getPlayerInfoMap() Collection<NetworkPlayerInfo/bsc>
    jmethodID mGetPlayerInfoMap = env->GetMethodID(cNH, "d", "()Ljava/util/Collection;");
    if (!mGetPlayerInfoMap) { env->ExceptionClear(); mGetPlayerInfoMap = env->GetMethodID(cNH, "getPlayerInfoMap", "()Ljava/util/Collection;"); }
    if (!mGetPlayerInfoMap) { env->ExceptionClear(); mGetPlayerInfoMap = env->GetMethodID(cNH, "func_175106_d", "()Ljava/util/Collection;"); }

    if (mGetPlayerInfoMap) {
        vals = env->CallObjectMethod(conn, mGetPlayerInfoMap);
        if (env->ExceptionCheck()) { env->ExceptionClear(); vals = nullptr; }
    }

    if (!vals) {
        const char* mapFields[] = { "i", "playerInfoMap", "field_175104_f", "f", "e" };
        jfieldID fMap = nullptr;
        for (const char* mf : mapFields) {
            fMap = env->GetFieldID(cNH, mf, "Ljava/util/Map;");
            if (fMap) break;
            env->ExceptionClear();
        }

        if (fMap) {
            jobject map = env->GetObjectField(conn, fMap);
            if (map && !env->ExceptionCheck()) {
                jclass cMap = env->GetObjectClass(map);
                jmethodID mValues = env->GetMethodID(cMap, "values", "()Ljava/util/Collection;");
                if (mValues) {
                    vals = env->CallObjectMethod(map, mValues);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); vals = nullptr; }
                }
                env->DeleteLocalRef(cMap);
                env->DeleteLocalRef(map);
            } else env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(cNH);
    env->DeleteLocalRef(conn);

    std::vector<std::pair<std::string, int>> currentTab;

    if (vals && !env->ExceptionCheck()) {
        jclass cColl = env->GetObjectClass(vals);
        jmethodID mIter = env->GetMethodID(cColl, "iterator", "()Ljava/util/Iterator;");
        env->DeleteLocalRef(cColl);

        if (mIter) {
            jobject iter = env->CallObjectMethod(vals, mIter);
            if (iter && !env->ExceptionCheck()) {
                jclass cIter = env->GetObjectClass(iter);
                jmethodID mHasNext = env->GetMethodID(cIter, "hasNext", "()Z");
                jmethodID mNext = env->GetMethodID(cIter, "next", "()Ljava/lang/Object;");

                int count = 0;
                while (mHasNext && mNext && env->CallBooleanMethod(iter, mHasNext) && count < 1000) {
                    ++count;
                    if (env->PushLocalFrame(32) != 0) break;
                    jobject npi = env->CallObjectMethod(iter, mNext);
                    if (!npi || env->ExceptionCheck()) { env->ExceptionClear(); env->PopLocalFrame(nullptr); break; }

                    jclass cNPI = env->GetObjectClass(npi);

                    // responseTime (ping) в 1.12.2: c()I, getResponseTime()I, func_178853_c()I
                    jmethodID mPing = env->GetMethodID(cNPI, "c", "()I");
                    if (!mPing) { env->ExceptionClear(); mPing = env->GetMethodID(cNPI, "getResponseTime", "()I"); }
                    if (!mPing) { env->ExceptionClear(); mPing = env->GetMethodID(cNPI, "func_178853_c", "()I"); }
                    int playerPing = mPing ? env->CallIntMethod(npi, mPing) : 0;
                    if (playerPing < 0) playerPing = 0;

                    // Имя игрока из GameProfile: a()Lcom/mojang/authlib/GameProfile;
                    std::string pName = "";
                    std::string profileUUID = "";
                    jmethodID mProfile = env->GetMethodID(cNPI, "a", "()Lcom/mojang/authlib/GameProfile;");
                    if (!mProfile) { env->ExceptionClear(); mProfile = env->GetMethodID(cNPI, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;"); }
                    if (!mProfile) { env->ExceptionClear(); mProfile = env->GetMethodID(cNPI, "func_178845_a", "()Lcom/mojang/authlib/GameProfile;"); }

                    if (mProfile) {
                        jobject prof = env->CallObjectMethod(npi, mProfile);
                        if (prof && !env->ExceptionCheck()) {
                            jclass cProf = env->GetObjectClass(prof);
                            jmethodID mGetName = env->GetMethodID(cProf, "getName", "()Ljava/lang/String;");
                            if (mGetName) {
                                auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(prof, mGetName));
                                if (jName && !env->ExceptionCheck()) {
                                    const char* chars = env->GetStringUTFChars(jName, nullptr);
                                    if (chars) { pName = chars; env->ReleaseStringUTFChars(jName, chars); }
                                    env->DeleteLocalRef(jName);
                                } else env->ExceptionClear();
                            }

                            // Извлекаем истинный UUID игрока из GameProfile (getId)
                            jmethodID mGetUUID = env->GetMethodID(cProf, "getId", "()Ljava/util/UUID;");
                            if (mGetUUID) {
                                jobject uObj = env->CallObjectMethod(prof, mGetUUID);
                                if (uObj && !env->ExceptionCheck()) {
                                    jclass cUUID = env->GetObjectClass(uObj);
                                    jmethodID mToStr = env->GetMethodID(cUUID, "toString", "()Ljava/lang/String;");
                                    if (mToStr) {
                                        auto jUStr = reinterpret_cast<jstring>(env->CallObjectMethod(uObj, mToStr));
                                        if (jUStr && !env->ExceptionCheck()) {
                                            const char* uChars = env->GetStringUTFChars(jUStr, nullptr);
                                            if (uChars) { profileUUID = uChars; env->ReleaseStringUTFChars(jUStr, uChars); }
                                            env->DeleteLocalRef(jUStr);
                                        } else env->ExceptionClear();
                                    }
                                    env->DeleteLocalRef(cUUID);
                                    env->DeleteLocalRef(uObj);
                                } else env->ExceptionClear();
                            }

                            env->DeleteLocalRef(cProf);
                            env->DeleteLocalRef(prof);
                        } else env->ExceptionClear();
                    }

                    // Читаем форматированное имя из Tab (getDisplayName / l) для RustEx bypass
                    std::string tabDisplayStr = "";
                    jmethodID mGetDisp = env->GetMethodID(cNPI, "l", "()Lhh;");
                    if (!mGetDisp) { env->ExceptionClear(); mGetDisp = env->GetMethodID(cNPI, "getDisplayName", "()Lnet/minecraft/util/text/ITextComponent;"); }
                    if (!mGetDisp) { env->ExceptionClear(); mGetDisp = env->GetMethodID(cNPI, "func_178854_k", "()Lnet/minecraft/util/text/ITextComponent;"); }
                    if (mGetDisp) {
                        jobject comp = env->CallObjectMethod(npi, mGetDisp);
                        if (comp && !env->ExceptionCheck()) {
                            jclass cComp = env->GetObjectClass(comp);
                            jmethodID mGetText = env->GetMethodID(cComp, "d", "()Ljava/lang/String;");
                            if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "getFormattedText", "()Ljava/lang/String;"); }
                            if (mGetText) {
                                auto jText = reinterpret_cast<jstring>(env->CallObjectMethod(comp, mGetText));
                                if (jText && !env->ExceptionCheck()) {
                                    const char* rawDisp = env->GetStringUTFChars(jText, nullptr);
                                    if (rawDisp) {
                                        tabDisplayStr = rawDisp;
                                        env->ReleaseStringUTFChars(jText, rawDisp);
                                    }
                                    env->DeleteLocalRef(jText);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cComp);
                            env->DeleteLocalRef(comp);
                        } else env->ExceptionClear();
                    }

                    // Читаем имя игрока для отображения в HUD
                    std::string realNick = ExtractRealPlayerName(tabDisplayStr, m_data.username);
                    if (!IsValidPlayerName(realNick) && IsValidPlayerName(pName)) realNick = pName;
                    if (realNick.empty()) realNick = pName;

                    if (!realNick.empty() && realNick != "?") {
                        currentTab.push_back({ realNick, playerPing });
                        if (realNick == m_data.username || (ping < 0 && !m_data.username.empty() && realNick.find(m_data.username) != std::string::npos)) {
                            ping = playerPing;
                        }
                    }

                    env->DeleteLocalRef(cNPI);
                    env->DeleteLocalRef(npi);
                    env->PopLocalFrame(nullptr);
                }
                env->DeleteLocalRef(cIter);
                env->DeleteLocalRef(iter);
            }
        }
        env->DeleteLocalRef(vals);
    }

    if (!currentTab.empty()) {
        m_data.tabPlayers = std::move(currentTab);
    }

    if (ping >= 0) m_data.ping = ping;
    return ping;
}

// ── RustEx Bypass: 1-в-1 как в NameTags.java updateOfflineTabNames() ────────
void JVMBridge::UpdateOfflineTabNames(JNIEnv* env) {
    if (!env || !m_ok || !m_cMC) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass cMC = env->GetObjectClass(mc);
    jobject conn = nullptr;

    jmethodID mGetConn = env->GetMethodID(cMC, "v", "()Lbrz;");
    if (!mGetConn) { env->ExceptionClear(); mGetConn = env->GetMethodID(cMC, "getConnection", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
    if (!mGetConn) { env->ExceptionClear(); mGetConn = env->GetMethodID(cMC, "func_147114_u", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
    if (mGetConn) {
        conn = env->CallObjectMethod(mc, mGetConn);
        if (env->ExceptionCheck()) { env->ExceptionClear(); conn = nullptr; }
    }
    if (!conn) {
        jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (fPlayer) {
            jobject player = env->GetObjectField(mc, fPlayer);
            if (player && !env->ExceptionCheck()) {
                jclass cPlayer = env->GetObjectClass(player);
                jfieldID fPConn = env->GetFieldID(cPlayer, "d", "Lbrz;");
                if (!fPConn) { env->ExceptionClear(); fPConn = env->GetFieldID(cPlayer, "connection", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
                if (!fPConn) { env->ExceptionClear(); fPConn = env->GetFieldID(cPlayer, "field_71174_a", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
                if (fPConn) {
                    conn = env->GetObjectField(player, fPConn);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); conn = nullptr; }
                }
                env->DeleteLocalRef(cPlayer);
                env->DeleteLocalRef(player);
            } else env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);

    if (!conn) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    jclass cNH = env->GetObjectClass(conn);
    jobject vals = nullptr;
    jmethodID mGetPlayerInfoMap = env->GetMethodID(cNH, "d", "()Ljava/util/Collection;");
    if (!mGetPlayerInfoMap) { env->ExceptionClear(); mGetPlayerInfoMap = env->GetMethodID(cNH, "getPlayerInfoMap", "()Ljava/util/Collection;"); }
    if (!mGetPlayerInfoMap) { env->ExceptionClear(); mGetPlayerInfoMap = env->GetMethodID(cNH, "func_175106_d", "()Ljava/util/Collection;"); }
    if (mGetPlayerInfoMap) {
        vals = env->CallObjectMethod(conn, mGetPlayerInfoMap);
        if (env->ExceptionCheck()) { env->ExceptionClear(); vals = nullptr; }
    }
    if (!vals) {
        const char* mapFields[] = { "i", "playerInfoMap", "field_175104_f", "f", "e" };
        for (const char* mf : mapFields) {
            jfieldID fMap = env->GetFieldID(cNH, mf, "Ljava/util/Map;");
            if (fMap) {
                jobject map = env->GetObjectField(conn, fMap);
                if (map && !env->ExceptionCheck()) {
                    jclass cMap = env->GetObjectClass(map);
                    jmethodID mValues = env->GetMethodID(cMap, "values", "()Ljava/util/Collection;");
                    if (mValues) {
                        vals = env->CallObjectMethod(map, mValues);
                        if (env->ExceptionCheck()) { env->ExceptionClear(); vals = nullptr; }
                    }
                    env->DeleteLocalRef(cMap);
                    env->DeleteLocalRef(map);
                    break;
                } else env->ExceptionClear();
            }
            env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(cNH);
    env->DeleteLocalRef(conn);

    if (!vals) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    jclass cColl = env->GetObjectClass(vals);
    jmethodID mIter = env->GetMethodID(cColl, "iterator", "()Ljava/util/Iterator;");
    env->DeleteLocalRef(cColl);
    if (!mIter) {
        env->DeleteLocalRef(vals);
        return;
    }

    jobject iter = env->CallObjectMethod(vals, mIter);
    env->DeleteLocalRef(vals);
    if (!iter || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass cIter = env->GetObjectClass(iter);
    jmethodID mHasNext = env->GetMethodID(cIter, "hasNext", "()Z");
    jmethodID mNext = env->GetMethodID(cIter, "next", "()Ljava/lang/Object;");

    jclass cUUID = env->FindClass("java/util/UUID");
    jmethodID mNameUUID = cUUID ? env->GetStaticMethodID(cUUID, "nameUUIDFromBytes", "([B)Ljava/util/UUID;") : nullptr;
    jmethodID mToStr = cUUID ? env->GetMethodID(cUUID, "toString", "()Ljava/lang/String;") : nullptr;

    std::unordered_map<std::string, std::string> newOfflineNames;

    int count = 0;
    while (mHasNext && mNext && env->CallBooleanMethod(iter, mHasNext) && count < 1000) {
        ++count;
        if (env->PushLocalFrame(32) != 0) break;
        jobject npi = env->CallObjectMethod(iter, mNext);
        if (!npi || env->ExceptionCheck()) { env->ExceptionClear(); env->PopLocalFrame(nullptr); break; }

        jclass cNPI = env->GetObjectClass(npi);

        // Читаем full tab string (1-в-1 getTabString(info) из NameTags.java)
        std::string tabString = "";

        // 1. info.getDisplayName() -> ITextComponent.getFormattedText() / unformatted
        jmethodID mGetDisp = env->GetMethodID(cNPI, "l", "()Lhh;");
        if (!mGetDisp) { env->ExceptionClear(); mGetDisp = env->GetMethodID(cNPI, "getDisplayName", "()Lnet/minecraft/util/text/ITextComponent;"); }
        if (!mGetDisp) { env->ExceptionClear(); mGetDisp = env->GetMethodID(cNPI, "func_178854_k", "()Lnet/minecraft/util/text/ITextComponent;"); }
        if (mGetDisp) {
            jobject comp = env->CallObjectMethod(npi, mGetDisp);
            if (comp && !env->ExceptionCheck()) {
                jclass cComp = env->GetObjectClass(comp);
                jmethodID mGetText = env->GetMethodID(cComp, "d", "()Ljava/lang/String;");
                if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "getFormattedText", "()Ljava/lang/String;"); }
                if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "c", "()Ljava/lang/String;"); }
                if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "getUnformattedText", "()Ljava/lang/String;"); }
                if (mGetText) {
                    auto jText = reinterpret_cast<jstring>(env->CallObjectMethod(comp, mGetText));
                    if (jText && !env->ExceptionCheck()) {
                        const char* rawDisp = env->GetStringUTFChars(jText, nullptr);
                        if (rawDisp) {
                            tabString = rawDisp;
                            env->ReleaseStringUTFChars(jText, rawDisp);
                        }
                        env->DeleteLocalRef(jText);
                    } else env->ExceptionClear();
                }
                env->DeleteLocalRef(cComp);
                env->DeleteLocalRef(comp);
            } else env->ExceptionClear();
        }

        // 2. GameProfile (a()Lcom/mojang/authlib/GameProfile;)
        std::string profName = "";
        std::string profUUID = "";
        jmethodID mProfile = env->GetMethodID(cNPI, "a", "()Lcom/mojang/authlib/GameProfile;");
        if (!mProfile) { env->ExceptionClear(); mProfile = env->GetMethodID(cNPI, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;"); }
        if (!mProfile) { env->ExceptionClear(); mProfile = env->GetMethodID(cNPI, "func_178845_a", "()Lcom/mojang/authlib/GameProfile;"); }
        if (mProfile) {
            jobject prof = env->CallObjectMethod(npi, mProfile);
            if (prof && !env->ExceptionCheck()) {
                jclass cProf = env->GetObjectClass(prof);
                jmethodID mGetName = env->GetMethodID(cProf, "getName", "()Ljava/lang/String;");
                if (mGetName) {
                    auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(prof, mGetName));
                    if (jName && !env->ExceptionCheck()) {
                        const char* chars = env->GetStringUTFChars(jName, nullptr);
                        if (chars) { profName = chars; env->ReleaseStringUTFChars(jName, chars); }
                        env->DeleteLocalRef(jName);
                    } else env->ExceptionClear();
                }
                jmethodID mGetUUID = env->GetMethodID(cProf, "getId", "()Ljava/util/UUID;");
                if (mGetUUID && mToStr) {
                    jobject uObj = env->CallObjectMethod(prof, mGetUUID);
                    if (uObj && !env->ExceptionCheck()) {
                        auto jUStr = reinterpret_cast<jstring>(env->CallObjectMethod(uObj, mToStr));
                        if (jUStr && !env->ExceptionCheck()) {
                            const char* uChars = env->GetStringUTFChars(jUStr, nullptr);
                            if (uChars) { profUUID = uChars; env->ReleaseStringUTFChars(jUStr, uChars); }
                            env->DeleteLocalRef(jUStr);
                        } else env->ExceptionClear();
                        env->DeleteLocalRef(uObj);
                    } else env->ExceptionClear();
                }
                env->DeleteLocalRef(cProf);
                env->DeleteLocalRef(prof);
            } else env->ExceptionClear();
        }

        if (tabString.empty() && !profName.empty()) {
            tabString = profName;
        }

        // Парсим реальный ник (строго как parseNickFromTab в NameTags.java)
        std::string realNick = ExtractRealPlayerName(tabString, m_data.username);
        if (!IsValidPlayerName(realNick) && IsValidPlayerName(profName)) {
            realNick = profName;
        }

        if (IsValidPlayerName(realNick)) {
            // Вычисляем OfflinePlayer:<realNick> UUID
            if (cUUID && mNameUUID && mToStr) {
                std::string offStr = "OfflinePlayer:" + realNick;
                jbyteArray bArr = env->NewByteArray(static_cast<jsize>(offStr.length()));
                if (bArr) {
                    env->SetByteArrayRegion(bArr, 0, static_cast<jsize>(offStr.length()), reinterpret_cast<const jbyte*>(offStr.c_str()));
                    jobject uObj = env->CallStaticObjectMethod(cUUID, mNameUUID, bArr);
                    if (uObj && !env->ExceptionCheck()) {
                        auto jUStr = reinterpret_cast<jstring>(env->CallObjectMethod(uObj, mToStr));
                        if (jUStr && !env->ExceptionCheck()) {
                            const char* uChars = env->GetStringUTFChars(jUStr, nullptr);
                            if (uChars && uChars[0] != '\0') {
                                newOfflineNames[uChars] = realNick;
                            }
                            if (uChars) env->ReleaseStringUTFChars(jUStr, uChars);
                            env->DeleteLocalRef(jUStr);
                        } else env->ExceptionClear();
                        env->DeleteLocalRef(uObj);
                    } else env->ExceptionClear();
                    env->DeleteLocalRef(bArr);
                }
            }
            if (!profUUID.empty()) {
                newOfflineNames[profUUID] = realNick;
            }
        }

        env->DeleteLocalRef(cNPI);
        env->DeleteLocalRef(npi);
        env->PopLocalFrame(nullptr);
    }

    env->DeleteLocalRef(cIter);
    env->DeleteLocalRef(iter);
    if (cUUID) env->DeleteLocalRef(cUUID);

    if (!newOfflineNames.empty()) {
        for (const auto& kv : newOfflineNames) {
            m_offlineTabNames[kv.first] = kv.second;
        }
    }
}

bool JVMBridge::QueryIsChatOpen(JNIEnv* env) {
    jobject mc = GetMinecraftInstance(env);
    if (!mc) return false;

    jclass cMC = env->GetObjectClass(mc);
    const char* screenFields[] = { "m", "currentScreen", "field_71462_r" };
    jfieldID fScreen = nullptr;
    for (const char* sf : screenFields) {
        fScreen = env->GetFieldID(cMC, sf, "Lblk;");
        if (!fScreen) { env->ExceptionClear(); fScreen = env->GetFieldID(cMC, sf, "Lnet/minecraft/client/gui/GuiScreen;"); }
        if (fScreen) break;
        env->ExceptionClear();
    }

    bool isChat = false;
    bool hasScreen = false;
    if (fScreen) {
        jobject screen = env->GetObjectField(mc, fScreen);
        if (screen && !env->ExceptionCheck()) {
            hasScreen = true;
            jclass cScreen = env->GetObjectClass(screen);
            jclass cClass = env->FindClass("java/lang/Class");
            if (cClass) {
                jmethodID mGetName = env->GetMethodID(cClass, "getName", "()Ljava/lang/String;");
                if (mGetName) {
                    auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(cScreen, mGetName));
                    if (jName && !env->ExceptionCheck()) {
                        const char* nameStr = env->GetStringUTFChars(jName, nullptr);
                        if (nameStr) {
                            std::string sName = nameStr;
                            if (sName.find("bln") != std::string::npos ||
                                sName.find("GuiChat") != std::string::npos ||
                                sName.find("Chat") != std::string::npos ||
                                sName.find("chat") != std::string::npos ||
                                sName.find("bll") != std::string::npos) {
                                isChat = true;
                            }
                            env->ReleaseStringUTFChars(jName, nameStr);
                        }
                        env->DeleteLocalRef(jName);
                    }
                }
                env->DeleteLocalRef(cClass);
            }
            env->DeleteLocalRef(cScreen);
            env->DeleteLocalRef(screen);
        } else {
            env->ExceptionClear();
        }
    }

    m_data.hasScreen = hasScreen;
    m_data.isChatOpen = isChat;

    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);
    return isChat;
}

bool JVMBridge::QueryIsTitleActive(JNIEnv* env) {
    if (!env || !m_ok || !m_cMC) return false;
    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    static jfieldID s_fIngameGUI = nullptr;
    static jfieldID s_fTimer = nullptr;
    static bool s_resolved = false;

    jclass cMC = env->GetObjectClass(mc);
    if (!s_resolved && cMC) {
        s_resolved = true;
        const char* igNames[] = { "q", "ingameGUI", "field_71456_v" };
        for (const char* ign : igNames) {
            s_fIngameGUI = env->GetFieldID(cMC, ign, "Lbiq;");
            if (!s_fIngameGUI) { env->ExceptionClear(); s_fIngameGUI = env->GetFieldID(cMC, ign, "Lnet/minecraft/client/gui/GuiIngame;"); }
            if (s_fIngameGUI) break;
            env->ExceptionClear();
        }
    }

    bool active = false;
    if (s_fIngameGUI) {
        jobject ig = env->GetObjectField(mc, s_fIngameGUI);
        if (ig && !env->ExceptionCheck()) {
            if (!s_fTimer) {
                jclass cIG = env->GetObjectClass(ig);
                if (cIG) {
                    const char* timerNames[] = { "field_175195_w", "titlesTimer", "w", "C", "B" };
                    for (const char* tn : timerNames) {
                        s_fTimer = env->GetFieldID(cIG, tn, "I");
                        if (s_fTimer) break;
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(cIG);
                }
            }
            if (s_fTimer) {
                jint timer = env->GetIntField(ig, s_fTimer);
                if (env->ExceptionCheck()) { env->ExceptionClear(); timer = 0; }
                if (timer > 0) active = true;
            }
            env->DeleteLocalRef(ig);
        } else env->ExceptionClear();
    }

    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);
    return active;
}

int JVMBridge::QueryGuiScale(JNIEnv* env) {
    jobject mc = GetMinecraftInstance(env);
    if (!mc) return 2;

    jclass cMC = env->GetObjectClass(mc);

    // bib.t -> bid (GameSettings)
    jfieldID fSettings = env->GetFieldID(cMC, "t", "Lbid;");
    if (!fSettings) {
        env->ExceptionClear();
        fSettings = env->GetFieldID(cMC, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
    }

    int userGuiScale = 0;
    if (fSettings) {
        jobject settings = env->GetObjectField(mc, fSettings);
        if (settings && !env->ExceptionCheck()) {
            jclass cSettings = env->GetObjectClass(settings);
            // bid.aG is guiScale (0=auto, 1=small, 2=normal, 3=large)
            const char* scaleFields[] = { "aG", "guiScale", "field_74335_Z", "aF", "aE" };
            jfieldID fScale = nullptr;
            for (const char* scf : scaleFields) {
                fScale = env->GetFieldID(cSettings, scf, "I");
                if (fScale) break;
                env->ExceptionClear();
            }
            if (fScale) {
                userGuiScale = env->GetIntField(settings, fScale);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();

            // bid.aw is thirdPersonView in 1.12.2 (0=first person, 1=third person back, 2=third person front)
            const char* tpFields[] = { "aw", "thirdPersonView", "field_74320_O", "aA", "as", "at", "au", "av" };
            jfieldID fTP = nullptr;
            for (const char* tpf : tpFields) {
                fTP = env->GetFieldID(cSettings, tpf, "I");
                if (fTP) break;
                env->ExceptionClear();
            }
            if (fTP) {
                m_data.thirdPersonView = env->GetIntField(settings, fTP);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();

            // Читаем реальный FOV игрока из GameSettings (bid.aD / fovSetting)
            jfieldID fFOV = env->GetFieldID(cSettings, "aD", "F");
            if (!fFOV) { env->ExceptionClear(); fFOV = env->GetFieldID(cSettings, "fovSetting", "F"); }
            if (!fFOV) { env->ExceptionClear(); fFOV = env->GetFieldID(cSettings, "field_74334_X", "F"); }
            if (fFOV) {
                float fovVal = env->GetFloatField(settings, fFOV);
                if (fovVal > 10.0f && fovVal < 170.0f) {
                    m_data.fov = fovVal;
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();

            env->DeleteLocalRef(cSettings);
            env->DeleteLocalRef(settings);
        } else {
            env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    int scale = 1;
    int maxScale = (userGuiScale > 0) ? userGuiScale : 1000;
    while (scale < maxScale && (disp.x / (scale + 1)) >= 320 && (disp.y / (scale + 1)) >= 240) {
        scale++;
    }

    return (scale > 0) ? scale : 2;
}

// ── Удаление цветовых кодов Minecraft (§r, §f, &a) ──
static std::string StripColorCodes(const std::string& str) {
    std::string clean = "";
    clean.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (static_cast<unsigned char>(str[i]) == 0xC2 && i + 1 < str.size() && static_cast<unsigned char>(str[i + 1]) == 0xA7) {
            if (i + 2 < str.size()) i += 2;
            else i += 1;
            continue;
        }
        if (static_cast<unsigned char>(str[i]) == 0xA7) {
            if (i + 1 < str.size()) i += 1;
            continue;
        }
        if (str[i] == '&' && i + 1 < str.size() && isalnum(static_cast<unsigned char>(str[i + 1]))) {
            i += 1;
            continue;
        }
        clean.push_back(str[i]);
    }
    size_t start = clean.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = clean.find_last_not_of(" \t\r\n");
    return clean.substr(start, end - start + 1);
}

// ── Очистка от Minecraft-форматирования (§r, §f, &a) и приведение к нижнему регистру (ASCII + UTF-8 кириллица) ──
static std::string CleanAndLower(const std::string& str) {
    std::string clean = "";
    clean.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        // Пропуск Minecraft §-кодов цвета (UTF-8 \xc2\xa7 + символ цвета, либо 1-байтный \xa7 + символ)
        if (static_cast<unsigned char>(str[i]) == 0xC2 && i + 1 < str.size() && static_cast<unsigned char>(str[i + 1]) == 0xA7) {
            if (i + 2 < str.size()) i += 2;
            else i += 1;
            continue;
        }
        if (static_cast<unsigned char>(str[i]) == 0xA7) {
            if (i + 1 < str.size()) i += 1;
            continue;
        }
        // Пропуск &-кодов цвета (& + буква/цифра)
        if (str[i] == '&' && i + 1 < str.size() && isalnum(static_cast<unsigned char>(str[i + 1]))) {
            i += 1;
            continue;
        }

        // Преобразование заглавных русских букв UTF-8 в строчные
        if (static_cast<unsigned char>(str[i]) == 0xD0 && i + 1 < str.size()) {
            unsigned char b2 = static_cast<unsigned char>(str[i + 1]);
            if (b2 == 0x81) { // Ё (D0 81) -> ё (D1 91)
                clean.push_back(static_cast<char>(0xD1));
                clean.push_back(static_cast<char>(0x91));
                i++;
                continue;
            }
            if (b2 >= 0x90 && b2 <= 0x9F) { // А..П (D0 90 .. D0 9F) -> а..п (D0 B0 .. D0 BF)
                clean.push_back(static_cast<char>(0xD0));
                clean.push_back(static_cast<char>(b2 + 0x20));
                i++;
                continue;
            }
            if (b2 >= 0xA0 && b2 <= 0xAF) { // Р..Я (D0 A0 .. D0 AF) -> р..я (D1 80 .. D1 8F)
                clean.push_back(static_cast<char>(0xD1));
                clean.push_back(static_cast<char>(b2 - 0x20));
                i++;
                continue;
            }
        }

        clean.push_back(static_cast<char>(tolower(static_cast<unsigned char>(str[i]))));
    }
    return clean;
}

static const char* GetVanillaItemSpriteNameById(int itemId) {
    switch (itemId) {
        // ── Еда ──
        case 392: return "minecraft:items/potato";
        case 393: return "minecraft:items/potato_baked";
        case 394: return "minecraft:items/potato_poisonous";
        case 297: return "minecraft:items/bread";
        case 260: return "minecraft:items/apple";
        case 322: return "minecraft:items/apple_golden";
        case 391: return "minecraft:items/carrot";
        case 396: return "minecraft:items/carrot_golden";
        case 360: return "minecraft:items/melon";
        case 86:  return "minecraft:blocks/pumpkin_face_off";
        case 364: return "minecraft:items/beef_cooked";
        case 363: return "minecraft:items/beef_raw";
        case 320: return "minecraft:items/porkchop_cooked";
        case 319: return "minecraft:items/porkchop_raw";
        case 366: return "minecraft:items/chicken_cooked";
        case 365: return "minecraft:items/chicken_raw";
        case 424: return "minecraft:items/mutton_cooked";
        case 423: return "minecraft:items/mutton_raw";
        case 350: return "minecraft:items/fish_cooked";
        case 349: return "minecraft:items/fish_raw";
        case 354: return "minecraft:items/cake";
        case 357: return "minecraft:items/cookie";
        case 282: return "minecraft:items/mushroom_stew";

        // ── Броня ──
        case 298: return "minecraft:items/leather_helmet";
        case 299: return "minecraft:items/leather_chestplate";
        case 300: return "minecraft:items/leather_leggings";
        case 301: return "minecraft:items/leather_boots";
        case 302: return "minecraft:items/chainmail_helmet";
        case 303: return "minecraft:items/chainmail_chestplate";
        case 304: return "minecraft:items/chainmail_leggings";
        case 305: return "minecraft:items/chainmail_boots";
        case 306: return "minecraft:items/iron_helmet";
        case 307: return "minecraft:items/iron_chestplate";
        case 308: return "minecraft:items/iron_leggings";
        case 309: return "minecraft:items/iron_boots";
        case 310: return "minecraft:items/diamond_helmet";
        case 311: return "minecraft:items/diamond_chestplate";
        case 312: return "minecraft:items/diamond_leggings";
        case 313: return "minecraft:items/diamond_boots";
        case 314: return "minecraft:items/golden_helmet";
        case 315: return "minecraft:items/golden_chestplate";
        case 316: return "minecraft:items/golden_leggings";
        case 317: return "minecraft:items/golden_boots";
        case 442: return "minecraft:items/shield";
        case 443: return "minecraft:items/elytra";

        // ── Оружие и инструменты ──
        case 276: return "minecraft:items/diamond_sword";
        case 277: return "minecraft:items/diamond_shovel";
        case 278: return "minecraft:items/diamond_pickaxe";
        case 279: return "minecraft:items/diamond_axe";
        case 267: return "minecraft:items/iron_sword";
        case 256: return "minecraft:items/iron_shovel";
        case 257: return "minecraft:items/iron_pickaxe";
        case 258: return "minecraft:items/iron_axe";
        case 272: return "minecraft:items/stone_sword";
        case 273: return "minecraft:items/stone_shovel";
        case 274: return "minecraft:items/stone_pickaxe";
        case 275: return "minecraft:items/stone_axe";
        case 268: return "minecraft:items/wood_sword";
        case 269: return "minecraft:items/wood_shovel";
        case 270: return "minecraft:items/wood_pickaxe";
        case 271: return "minecraft:items/wood_axe";
        case 283: return "minecraft:items/gold_sword";
        case 284: return "minecraft:items/gold_shovel";
        case 285: return "minecraft:items/gold_pickaxe";
        case 286: return "minecraft:items/gold_axe";
        case 261: return "minecraft:items/bow_standby";
        case 262: return "minecraft:items/arrow";
        case 346: return "minecraft:items/fishing_rod";
        case 359: return "minecraft:items/shears";
        case 259: return "minecraft:items/flint_and_steel";

        // ── Ресурсы ──
        case 264: return "minecraft:items/diamond";
        case 265: return "minecraft:items/iron_ingot";
        case 266: return "minecraft:items/gold_ingot";
        case 263: return "minecraft:items/coal";
        case 331: return "minecraft:items/redstone_dust";
        case 388: return "minecraft:items/emerald";
        case 351: return "minecraft:items/dye_powder";
        case 280: return "minecraft:items/stick";
        case 287: return "minecraft:items/string";
        case 288: return "minecraft:items/feather";
        case 289: return "minecraft:items/gunpowder";
        case 334: return "minecraft:items/leather";
        case 352: return "minecraft:items/bone";
        case 318: return "minecraft:items/flint";
        case 341: return "minecraft:items/slimeball";
        case 368: return "minecraft:items/ender_pearl";
        case 381: return "minecraft:items/ender_eye";
        case 384: return "minecraft:items/experience_bottle";
        case 373: return "minecraft:items/potion";
        case 438: return "minecraft:items/potion_splash";
        case 441: return "minecraft:items/potion_lingering";
        case 325: return "minecraft:items/bucket_empty";
        case 326: return "minecraft:items/bucket_water";
        case 327: return "minecraft:items/bucket_lava";
        case 335: return "minecraft:items/bucket_milk";

        // ── Блоки ──
        case 1:   return "minecraft:blocks/stone";
        case 4:   return "minecraft:blocks/cobblestone";
        case 12:  return "minecraft:blocks/sand";
        case 13:  return "minecraft:blocks/gravel";
        case 17:  return "minecraft:blocks/log_oak";
        case 162: return "minecraft:blocks/log_acacia";
        case 5:   return "minecraft:blocks/planks_oak";
        case 20:  return "minecraft:blocks/glass";
        case 46:  return "minecraft:blocks/tnt_side";
        case 49:  return "minecraft:blocks/obsidian";
        case 50:  return "minecraft:blocks/torch_on";
        case 54:  return "minecraft:blocks/chest";
        case 58:  return "minecraft:blocks/crafting_table_top";
        case 61:  return "minecraft:blocks/furnace_front_off";
        case 116: return "minecraft:blocks/enchanting_table_top";
        case 145: return "minecraft:blocks/anvil_top_damaged_0";
        case 130: return "minecraft:blocks/ender_chest";
        case 14:  return "minecraft:blocks/gold_ore";
        case 15:  return "minecraft:blocks/iron_ore";
        case 16:  return "minecraft:blocks/coal_ore";
        case 21:  return "minecraft:blocks/lapis_ore";
        case 56:  return "minecraft:blocks/diamond_ore";
        case 73:  return "minecraft:blocks/redstone_ore";
        case 129: return "minecraft:blocks/emerald_ore";
        case 153: return "minecraft:blocks/quartz_ore";
        case 41:  return "minecraft:blocks/gold_block";
        case 42:  return "minecraft:blocks/iron_block";
        case 57:  return "minecraft:blocks/diamond_block";
        case 133: return "minecraft:blocks/emerald_block";
        case 152: return "minecraft:blocks/redstone_block";
        case 173: return "minecraft:blocks/coal_block";
        default: return nullptr;
    }
}

static const char* GetGeneralItemSpriteName(int itemId, const std::string& name) {
    std::string lower = CleanAndLower(name);

    // ── 1. ЕДА И ЯБЛОКИ (проверяем ДО инструментов, т.к. на RustEx яблоко сделано на базе pickaxe/257!) ──
    if (lower.find("яблок") != std::string::npos || lower.find("apple") != std::string::npos) {
        if (lower.find("золот") != std::string::npos || lower.find("gold") != std::string::npos) {
            return "minecraft:items/apple_golden";
        }
        return "minecraft:items/apple";
    }
    if (lower.find("хлеб") != std::string::npos || lower.find("bread") != std::string::npos) return "minecraft:items/bread";
    if (lower.find("картоф") != std::string::npos || lower.find("картош") != std::string::npos || lower.find("potato") != std::string::npos) {
        if (lower.find("жар") != std::string::npos || lower.find("печен") != std::string::npos || lower.find("baked") != std::string::npos) return "minecraft:items/potato_baked";
        return "minecraft:items/potato";
    }
    if (lower.find("морков") != std::string::npos || lower.find("carrot") != std::string::npos) return "minecraft:items/carrot";
    if (lower.find("тыкв") != std::string::npos || lower.find("pumpkin") != std::string::npos) {
        return "minecraft:blocks/pumpkin_face_off";
    }
    if (lower.find("арбуз") != std::string::npos || lower.find("melon") != std::string::npos) return "minecraft:items/melon";
    if (lower.find("свинин") != std::string::npos || lower.find("pork") != std::string::npos) {
        if (lower.find("жар") != std::string::npos || lower.find("cook") != std::string::npos) return "minecraft:items/porkchop_cooked";
        return "minecraft:items/porkchop_raw";
    }
    if (lower.find("говядин") != std::string::npos || lower.find("стейк") != std::string::npos || lower.find("мясо") != std::string::npos || lower.find("beef") != std::string::npos || lower.find("meat") != std::string::npos) {
        if (lower.find("сыр") != std::string::npos || lower.find("raw") != std::string::npos) return "minecraft:items/beef_raw";
        return "minecraft:items/beef_cooked";
    }
    if (lower.find("куриц") != std::string::npos || lower.find("chicken") != std::string::npos) {
        if (lower.find("жар") != std::string::npos || lower.find("cook") != std::string::npos) return "minecraft:items/chicken_cooked";
        return "minecraft:items/chicken_raw";
    }
    if (lower.find("баранин") != std::string::npos || lower.find("mutton") != std::string::npos) {
        if (lower.find("жар") != std::string::npos || lower.find("cook") != std::string::npos) return "minecraft:items/mutton_cooked";
        return "minecraft:items/mutton_raw";
    }
    if (lower.find("рыб") != std::string::npos || lower.find("fish") != std::string::npos) return "minecraft:items/fish_cod_cooked";
    if (lower.find("вода") != std::string::npos || lower.find("water") != std::string::npos || lower.find("бутылк") != std::string::npos || lower.find("bottle") != std::string::npos) {
        return "minecraft:items/potion_bottle_drinkable";
    }

    // ── 2. ТОПОРЫ (Axes) — проверяем ДО пирок и камня! ──
    if (lower.find("топор") != std::string::npos || lower.find("axe") != std::string::npos) {
        if (lower.find("камен") != std::string::npos || lower.find("stone") != std::string::npos) return "minecraft:items/stone_axe";
        if (lower.find("желез") != std::string::npos || lower.find("iron") != std::string::npos) return "minecraft:items/iron_axe";
        if (lower.find("алмаз") != std::string::npos || lower.find("diamond") != std::string::npos) return "minecraft:items/diamond_axe";
        if (lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos) return "minecraft:items/wooden_axe";
        return "minecraft:items/stone_axe"; // На RustEx основной рабочий топор — каменный топор!
    }

    // ── 3. КИРКИ (Pickaxes) — проверяем ДО ресурсов! ──
    if (lower.find("кирк") != std::string::npos || lower.find("pickaxe") != std::string::npos) {
        if (lower.find("камен") != std::string::npos || lower.find("stone") != std::string::npos) return "minecraft:items/stone_pickaxe";
        if (lower.find("желез") != std::string::npos || lower.find("iron") != std::string::npos) return "minecraft:items/iron_pickaxe";
        if (lower.find("алмаз") != std::string::npos || lower.find("diamond") != std::string::npos) return "minecraft:items/diamond_pickaxe";
        if (lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos) return "minecraft:items/wooden_pickaxe";
        return "minecraft:items/iron_pickaxe";
    }

    // ── 4. МЕЧИ, НОЖИ, МАЧЕТЕ, КОПЬЯ ──
    if (lower.find("мачете") != std::string::npos || lower.find("machete") != std::string::npos) return "minecraft:items/iron_sword";
    if (lower.find("нож") != std::string::npos || lower.find("knife") != std::string::npos) return "minecraft:items/bone";
    if (lower.find("копь") != std::string::npos || lower.find("spear") != std::string::npos) return "minecraft:items/stick";
    if (lower.find("меч") != std::string::npos || lower.find("sword") != std::string::npos) {
        if (lower.find("алмаз") != std::string::npos || lower.find("diamond") != std::string::npos) return "minecraft:items/diamond_sword";
        return "minecraft:items/iron_sword";
    }

    // ── 5. ДАЛЬНИЙ БОЙ И ОРУЖИЕ ──
    if (lower.find("арбалет") != std::string::npos || lower.find("crossbow") != std::string::npos) return "minecraft:items/bow_standby";
    if (lower.find("лук") != std::string::npos || lower.find("bow") != std::string::npos) return "minecraft:items/bow_standby";
    if (lower.find("стрел") != std::string::npos || lower.find("arrow") != std::string::npos) return "minecraft:items/arrow";
    if (lower.find("патрон") != std::string::npos || lower.find("пул") != std::string::npos || lower.find("ammo") != std::string::npos) return "minecraft:items/gold_nugget";
    if (lower.find("ракета") != std::string::npos || lower.find("rocket") != std::string::npos) return "minecraft:items/firework";
    if (lower.find("c4") != std::string::npos || lower.find("взрывчат") != std::string::npos || lower.find("гранат") != std::string::npos || lower.find("grenade") != std::string::npos) return "minecraft:items/fireball";

    // ── 6. МЕДИКАМЕНТЫ ──
    if (lower.find("бинт") != std::string::npos || lower.find("bandage") != std::string::npos) return "minecraft:items/paper";
    if (lower.find("аптечк") != std::string::npos || lower.find("шприц") != std::string::npos || lower.find("med") != std::string::npos || lower.find("syringe") != std::string::npos) return "minecraft:items/paper";

    // ── 7. БАЗОВЫЕ РЕСУРСЫ RUSTEX ──
    // Ткань
    if (lower.find("ткань") != std::string::npos || lower.find("cloth") != std::string::npos ||
        lower.find("нить") != std::string::npos || lower.find("string") != std::string::npos) {
        return "minecraft:items/string";
    }

    // Дерево и бревна
    if (lower.find("бревн") != std::string::npos || lower.find("log") != std::string::npos) {
        return "minecraft:blocks/log_oak";
    }
    if (lower.find("доск") != std::string::npos || lower.find("plank") != std::string::npos) {
        return "minecraft:blocks/planks_oak";
    }
    if (lower.find("дерево") != std::string::npos || lower.find("wood") != std::string::npos ||
        lower.find("палк") != std::string::npos || lower.find("stick") != std::string::npos) {
        return "minecraft:blocks/log_oak";
    }

    // Камень (ресурс, не топор и не кирка)
    if (lower.find("cobblestone") != std::string::npos || lower.find("булыжник") != std::string::npos ||
        lower.find("stone") != std::string::npos || lower.find("камен") != std::string::npos) {
        return "minecraft:items/flint";
    }

    // Сера и Порох
    if (lower.find("сера") != std::string::npos || lower.find("серн") != std::string::npos || lower.find("sulfur") != std::string::npos) return "minecraft:items/blaze_powder";
    if (lower.find("порох") != std::string::npos || lower.find("gunpowder") != std::string::npos) return "minecraft:items/gunpowder";

    // Металл (слитки/фрагменты, не инструменты)
    if (lower.find("металл") != std::string::npos || lower.find("желез") != std::string::npos || lower.find("metal") != std::string::npos || lower.find("iron_ingot") != std::string::npos) {
        return "minecraft:items/iron_ingot";
    }
    if (lower.find("мвк") != std::string::npos || lower.find("hqm") != std::string::npos) return "minecraft:items/iron_ingot";
    if (lower.find("золот") != std::string::npos || lower.find("gold") != std::string::npos) return "minecraft:items/gold_ingot";
    if (lower.find("алмаз") != std::string::npos || lower.find("diamond") != std::string::npos) return "minecraft:items/diamond";

    // Скрап / Шестеренки
    if (lower.find("скрап") != std::string::npos || lower.find("scrap") != std::string::npos || lower.find("шестеренк") != std::string::npos) return "minecraft:items/iron_nugget";

    // Кожа, Кость, Уголь, Топливо
    if (lower.find("кожа") != std::string::npos || lower.find("leather") != std::string::npos) return "minecraft:items/leather";
    if (lower.find("кость") != std::string::npos || lower.find("bone") != std::string::npos) return "minecraft:items/bone";
    if (lower.find("уголь") != std::string::npos || lower.find("coal") != std::string::npos) return "minecraft:items/coal";
    if (lower.find("жир") != std::string::npos || lower.find("топливо") != std::string::npos || lower.find("fuel") != std::string::npos) return "minecraft:items/magma_cream";

    // Щит, Тотем, Элитры
    if (lower.find("щит") != std::string::npos || lower.find("shield") != std::string::npos) return "minecraft:items/shield";
    if (lower.find("тотем") != std::string::npos || lower.find("totem") != std::string::npos) return "minecraft:items/totem";
    if (lower.find("элитр") != std::string::npos || lower.find("elytra") != std::string::npos) return "minecraft:items/elytra";

    // ── 8. БРОНЯ И ОДЕЖДА (СТРОГО ПО ТИПУ ЭКИПИРОВКИ) ──
    // Сначала точное совпадение по itemId брони:
    if (itemId == 300) return "minecraft:items/leather_leggings";
    if (itemId == 299) return "minecraft:items/leather_chestplate";
    if (itemId == 298) return "minecraft:items/leather_helmet";
    if (itemId == 301) return "minecraft:items/leather_boots";
    if (itemId == 308) return "minecraft:items/iron_leggings";
    if (itemId == 307) return "minecraft:items/iron_chestplate";
    if (itemId == 306) return "minecraft:items/iron_helmet";
    if (itemId == 309) return "minecraft:items/iron_boots";
    if (itemId == 312) return "minecraft:items/diamond_leggings";
    if (itemId == 311) return "minecraft:items/diamond_chestplate";
    if (itemId == 310) return "minecraft:items/diamond_helmet";
    if (itemId == 313) return "minecraft:items/diamond_boots";
    if (itemId == 304) return "minecraft:items/chainmail_leggings";
    if (itemId == 303) return "minecraft:items/chainmail_chestplate";
    if (itemId == 302) return "minecraft:items/chainmail_helmet";
    if (itemId == 305) return "minecraft:items/chainmail_boots";
    if (itemId == 316) return "minecraft:items/golden_leggings";
    if (itemId == 315) return "minecraft:items/golden_chestplate";
    if (itemId == 314) return "minecraft:items/golden_helmet";
    if (itemId == 317) return "minecraft:items/golden_boots";

    // Затем по ключевым словам:
    if (lower.find("понож") != std::string::npos || lower.find("штан") != std::string::npos || lower.find("брюк") != std::string::npos || lower.find("leggings") != std::string::npos || lower.find("pants") != std::string::npos || lower.find("trouser") != std::string::npos || lower.find("kilt") != std::string::npos) {
        if (lower.find("алмаз") != std::string::npos) return "minecraft:items/diamond_leggings";
        if (lower.find("желез") != std::string::npos) return "minecraft:items/iron_leggings";
        return "minecraft:items/leather_leggings";
    }
    if (lower.find("шлем") != std::string::npos || lower.find("helmet") != std::string::npos || lower.find("маск") != std::string::npos || lower.find("повязк") != std::string::npos || lower.find("каск") != std::string::npos || lower.find("шапк") != std::string::npos) {
        if (lower.find("алмаз") != std::string::npos) return "minecraft:items/diamond_helmet";
        if (lower.find("желез") != std::string::npos) return "minecraft:items/iron_helmet";
        return "minecraft:items/leather_helmet";
    }
    if (lower.find("нагрудник") != std::string::npos || lower.find("куртк") != std::string::npos || lower.find("рубах") != std::string::npos || lower.find("жилет") != std::string::npos || lower.find("chestplate") != std::string::npos || lower.find("jacket") != std::string::npos || lower.find("shirt") != std::string::npos) {
        if (lower.find("алмаз") != std::string::npos) return "minecraft:items/diamond_chestplate";
        if (lower.find("желез") != std::string::npos) return "minecraft:items/iron_chestplate";
        return "minecraft:items/leather_chestplate";
    }
    if (lower.find("ботинк") != std::string::npos || lower.find("обувь") != std::string::npos || lower.find("boots") != std::string::npos || lower.find("shoes") != std::string::npos || lower.find("сапог") != std::string::npos) {
        if (lower.find("алмаз") != std::string::npos) return "minecraft:items/diamond_boots";
        if (lower.find("желез") != std::string::npos) return "minecraft:items/iron_boots";
        return "minecraft:items/leather_boots";
    }

    // ── 9. Fallback по чистому числу itemId (ТОЛЬКО ЕСЛИ ИМЯ НЕ РАСПОЗНАНО) ──
    // Ванильная броня (298 - 317)
    if (itemId == 298) return "minecraft:items/leather_helmet";
    if (itemId == 299) return "minecraft:items/leather_chestplate";
    if (itemId == 300) return "minecraft:items/leather_leggings";
    if (itemId == 301) return "minecraft:items/leather_boots";
    if (itemId == 302) return "minecraft:items/chainmail_helmet";
    if (itemId == 303) return "minecraft:items/chainmail_chestplate";
    if (itemId == 304) return "minecraft:items/chainmail_leggings";
    if (itemId == 305) return "minecraft:items/chainmail_boots";
    if (itemId == 306) return "minecraft:items/iron_helmet";
    if (itemId == 307) return "minecraft:items/iron_chestplate";
    if (itemId == 308) return "minecraft:items/iron_leggings";
    if (itemId == 309) return "minecraft:items/iron_boots";
    if (itemId == 310) return "minecraft:items/diamond_helmet";
    if (itemId == 311) return "minecraft:items/diamond_chestplate";
    if (itemId == 312) return "minecraft:items/diamond_leggings";
    if (itemId == 313) return "minecraft:items/diamond_boots";
    if (itemId == 314) return "minecraft:items/golden_helmet";
    if (itemId == 315) return "minecraft:items/golden_chestplate";
    if (itemId == 316) return "minecraft:items/golden_leggings";
    if (itemId == 317) return "minecraft:items/golden_boots";
    if (itemId == 322) return "minecraft:items/apple_golden";
    if (itemId == 260) return "minecraft:items/apple";
    if (itemId == 257) return "minecraft:items/iron_pickaxe";
    if (itemId == 278) return "minecraft:items/diamond_pickaxe";
    if (itemId == 274) return "minecraft:items/stone_pickaxe";
    if (itemId == 258) return "minecraft:items/iron_axe";
    if (itemId == 275) return "minecraft:items/stone_axe";
    if (itemId == 279) return "minecraft:items/diamond_axe";
    if (itemId == 267) return "minecraft:items/iron_sword";
    if (itemId == 276) return "minecraft:items/diamond_sword";
    if (itemId == 261) return "minecraft:items/bow_standby";
    if (itemId == 262) return "minecraft:items/arrow";
    if (itemId == 287) return "minecraft:items/string";
    if (itemId == 280) return "minecraft:items/stick";
    if (itemId == 318) return "minecraft:items/flint";
    if (itemId == 265) return "minecraft:items/iron_ingot";
    if (itemId == 266) return "minecraft:items/gold_ingot";
    if (itemId == 264) return "minecraft:items/diamond";
    if (itemId == 377) return "minecraft:items/blaze_powder";
    if (itemId == 289) return "minecraft:items/gunpowder";
    if (itemId == 339) return "minecraft:items/paper";
    if (itemId == 334) return "minecraft:items/leather";
    if (itemId == 352) return "minecraft:items/bone";
    if (itemId == 263) return "minecraft:items/coal";

    const char* vSp = GetVanillaItemSpriteNameById(itemId);
    return vSp ? vSp : "";
}

static const char* GetArmorSpriteName(int itemId, const std::string& name) {
    return GetGeneralItemSpriteName(itemId, name);
}

// ── Быстрый кэширующий поиск TextureMap через методы и поля ─────────────────
static jobject GetMCTextureMap(JNIEnv* env, jobject mc) {
    if (!env || !mc) return nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass cMC = env->GetObjectClass(mc);

    static jmethodID s_cachedMethod = nullptr;
    static jfieldID  s_cachedField  = nullptr;
    static bool      s_lookupDone   = false;

    if (s_lookupDone) {
        jobject tm = nullptr;
        if (s_cachedMethod) tm = env->CallObjectMethod(mc, s_cachedMethod);
        else if (s_cachedField) tm = env->GetObjectField(mc, s_cachedField);
        if (env->ExceptionCheck()) { env->ExceptionClear(); tm = nullptr; }
        env->DeleteLocalRef(cMC);
        return tm;
    }

    // 1. Прямой поиск для Minecraft 1.12.2: bib.R() -> cdp (TextureMap)
    jmethodID midR = env->GetMethodID(cMC, "R", "()Lcdp;");
    if (!midR && env->ExceptionCheck()) env->ExceptionClear();
    if (midR) {
        jobject tm = env->CallObjectMethod(mc, midR);
        if (tm && !env->ExceptionCheck()) {
            s_cachedMethod = midR;
            s_lookupDone = true;
            env->DeleteLocalRef(cMC);
            return tm;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // 2. Прямое поле bib.aK -> cdp в 1.12.2
    jfieldID fidAK = env->GetFieldID(cMC, "aK", "Lcdp;");
    if (!fidAK && env->ExceptionCheck()) env->ExceptionClear();
    if (fidAK) {
        jobject tm = env->GetObjectField(mc, fidAK);
        if (tm && !env->ExceptionCheck()) {
            s_cachedField = fidAK;
            s_lookupDone = true;
            env->DeleteLocalRef(cMC);
            return tm;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // 3. Fallback методы с гарантированной очисткой исключений на каждой итерации
    const char* mNames[] = { "getTextureMapBlocks", "func_147117_R", "aN", "aM", "R" };
    const char* mSigs[]  = { "()Lcdp;", "()Lcds;", "()Lnet/minecraft/client/renderer/texture/TextureMap;" };
    for (const char* mn : mNames) {
        for (const char* sg : mSigs) {
            jmethodID mid = env->GetMethodID(cMC, mn, sg);
            if (!mid && env->ExceptionCheck()) env->ExceptionClear();
            if (mid) {
                jobject tm = env->CallObjectMethod(mc, mid);
                if (tm && !env->ExceptionCheck()) {
                    s_cachedMethod = mid;
                    s_lookupDone = true;
                    env->DeleteLocalRef(cMC);
                    return tm;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }
    }

    // 4. Fallback поля
    const char* fNames[] = { "textureMapBlocks", "field_147128_aI", "aK", "aJ" };
    for (const char* fn : fNames) {
        for (const char* sg : mSigs) {
            jfieldID fid = env->GetFieldID(cMC, fn, sg);
            if (!fid && env->ExceptionCheck()) env->ExceptionClear();
            if (fid) {
                jobject tm = env->GetObjectField(mc, fid);
                if (tm && !env->ExceptionCheck()) {
                    s_cachedField = fid;
                    s_lookupDone = true;
                    env->DeleteLocalRef(cMC);
                    return tm;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }
    }

    env->DeleteLocalRef(cMC);
    return nullptr;
}

// ── Быстрый кэширующий поиск RenderItem через методы и поля ──────────────────
static jobject GetMCRenderItem(JNIEnv* env, jobject mc) {
    if (!env || !mc) return nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass cMC = env->GetObjectClass(mc);

    static jmethodID s_cachedMethod = nullptr;
    static jfieldID  s_cachedField  = nullptr;
    static bool      s_lookupDone   = false;

    if (s_lookupDone) {
        jobject ri = nullptr;
        if (s_cachedMethod) ri = env->CallObjectMethod(mc, s_cachedMethod);
        else if (s_cachedField) ri = env->GetObjectField(mc, s_cachedField);
        if (env->ExceptionCheck()) { env->ExceptionClear(); ri = nullptr; }
        env->DeleteLocalRef(cMC);
        return ri;
    }

    // 1. Прямой поиск для Minecraft 1.12.2: bib.ad() -> bzw (RenderItem)
    jmethodID midAd = env->GetMethodID(cMC, "ad", "()Lbzw;");
    if (!midAd && env->ExceptionCheck()) env->ExceptionClear();
    if (midAd) {
        jobject ri = env->CallObjectMethod(mc, midAd);
        if (ri && !env->ExceptionCheck()) {
            s_cachedMethod = midAd;
            s_lookupDone = true;
            env->DeleteLocalRef(cMC);
            return ri;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // 2. Прямое поле bib.ab -> bzw в 1.12.2
    jfieldID fidAb = env->GetFieldID(cMC, "ab", "Lbzw;");
    if (!fidAb && env->ExceptionCheck()) env->ExceptionClear();
    if (fidAb) {
        jobject ri = env->GetObjectField(mc, fidAb);
        if (ri && !env->ExceptionCheck()) {
            s_cachedField = fidAb;
            s_lookupDone = true;
            env->DeleteLocalRef(cMC);
            return ri;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // 3. Fallback методы
    const char* mNames[] = { "getRenderItem", "func_175599_af", "ad", "ae" };
    const char* mSigs[]  = { "()Lbzw;", "()Lbvh;", "()Lnet/minecraft/client/renderer/RenderItem;" };
    for (const char* mn : mNames) {
        for (const char* sg : mSigs) {
            jmethodID mid = env->GetMethodID(cMC, mn, sg);
            if (!mid && env->ExceptionCheck()) env->ExceptionClear();
            if (mid) {
                jobject ri = env->CallObjectMethod(mc, mid);
                if (ri && !env->ExceptionCheck()) {
                    s_cachedMethod = mid;
                    s_lookupDone = true;
                    env->DeleteLocalRef(cMC);
                    return ri;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }
    }

    // 4. Fallback поля
    const char* fNames[] = { "renderItem", "field_175621_X", "ab", "aD" };
    for (const char* fn : fNames) {
        for (const char* sg : mSigs) {
            jfieldID fid = env->GetFieldID(cMC, fn, sg);
            if (!fid && env->ExceptionCheck()) env->ExceptionClear();
            if (fid) {
                jobject ri = env->GetObjectField(mc, fid);
                if (ri && !env->ExceptionCheck()) {
                    s_cachedField = fid;
                    s_lookupDone = true;
                    env->DeleteLocalRef(cMC);
                    return ri;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
        }
    }

    env->DeleteLocalRef(cMC);
    return nullptr;
}

struct CachedSpriteInfo {
    bool found = false;
    float minU = 0.f, maxU = 0.f, minV = 0.f, maxV = 0.f;
};
static std::unordered_map<std::string, CachedSpriteInfo> s_spriteCache;

// TextureAtlasSprite хранит только UV. После перезагрузки серверного ресурспака
// старые UV относятся к уже уничтоженному/пересобранному атласу, поэтому их
// нельзя переносить на новый OpenGL texture id.
static unsigned int s_cachedAtlasTextureId = 0;


static bool QuerySpriteByName(JNIEnv* env, jobject mc, const char* spriteName, float& minU, float& maxU, float& minV, float& maxV) {
    if (!env || !mc || !spriteName || spriteName[0] == '\0') return false;

    // Быстрый O(1) поиск в кэше — нулевые JNI-вызовы при повторном опросе
    auto it = s_spriteCache.find(spriteName);
    if (it != s_spriteCache.end()) {
        if (!it->second.found) return false;
        minU = it->second.minU; maxU = it->second.maxU;
        minV = it->second.minV; maxV = it->second.maxV;
        return true;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject tm = GetMCTextureMap(env, mc);
    if (!tm || env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    jclass cTM = env->GetObjectClass(tm);
    jmethodID mGetSprite = env->GetMethodID(cTM, "a", "(Ljava/lang/String;)Lcdq;");
    if (!mGetSprite && env->ExceptionCheck()) { env->ExceptionClear(); mGetSprite = env->GetMethodID(cTM, "a", "(Ljava/lang/String;)Lcdt;"); }
    if (!mGetSprite && env->ExceptionCheck()) { env->ExceptionClear(); mGetSprite = env->GetMethodID(cTM, "getAtlasSprite", "(Ljava/lang/String;)Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;"); }
    if (!mGetSprite && env->ExceptionCheck()) { env->ExceptionClear(); mGetSprite = env->GetMethodID(cTM, "func_110572_b", "(Ljava/lang/String;)Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;"); }
    env->DeleteLocalRef(cTM);

    if (!mGetSprite) { if (env->ExceptionCheck()) env->ExceptionClear(); env->DeleteLocalRef(tm); return false; }

    // Пробуем несколько вариантов имени: как есть, и с префиксом/без префикса "minecraft:"
    std::vector<std::string> namesToTry;
    namesToTry.push_back(spriteName);
    if (strncmp(spriteName, "minecraft:", 10) == 0) {
        namesToTry.push_back(spriteName + 10);
    } else {
        namesToTry.push_back(std::string("minecraft:") + spriteName);
    }

    jobject validSprite = nullptr;
    for (const auto& tryName : namesToTry) {
        jstring jName = env->NewStringUTF(tryName.c_str());
        jobject sp = env->CallObjectMethod(tm, mGetSprite, jName);
        env->DeleteLocalRef(jName);
        if (env->ExceptionCheck()) { env->ExceptionClear(); sp = nullptr; }
        if (!sp) continue;

        // Проверяем имя спрайта: если это "missingno" (черно-розовый шахматный квадрат), пробуем следующий вариант
        jclass cSp = env->GetObjectClass(sp);
        jmethodID mGetSpName = env->GetMethodID(cSp, "i", "()Ljava/lang/String;");
        if (!mGetSpName && env->ExceptionCheck()) { env->ExceptionClear(); mGetSpName = env->GetMethodID(cSp, "getIconName", "()Ljava/lang/String;"); }
        bool isMissing = false;
        if (mGetSpName) {
            auto jSpN = reinterpret_cast<jstring>(env->CallObjectMethod(sp, mGetSpName));
            if (jSpN && !env->ExceptionCheck()) {
                const char* spChars = env->GetStringUTFChars(jSpN, nullptr);
                if (spChars) {
                    if (strcmp(spChars, "missingno") == 0) isMissing = true;
                    env->ReleaseStringUTFChars(jSpN, spChars);
                }
                env->DeleteLocalRef(jSpN);
            } else env->ExceptionClear();
        }
        env->DeleteLocalRef(cSp);

        if (!isMissing) {
            validSprite = sp;
            break;
        }
        env->DeleteLocalRef(sp);
    }

    env->DeleteLocalRef(tm);
    if (!validSprite) return false;

    jclass cSprite = env->GetObjectClass(validSprite);
    jmethodID mMinU = env->GetMethodID(cSprite, "e", "()F");
    if (!mMinU && env->ExceptionCheck()) { env->ExceptionClear(); mMinU = env->GetMethodID(cSprite, "getMinU", "()F"); }
    jmethodID mMaxU = env->GetMethodID(cSprite, "f", "()F");
    if (!mMaxU && env->ExceptionCheck()) { env->ExceptionClear(); mMaxU = env->GetMethodID(cSprite, "getMaxU", "()F"); }
    jmethodID mMinV = env->GetMethodID(cSprite, "g", "()F");
    if (!mMinV && env->ExceptionCheck()) { env->ExceptionClear(); mMinV = env->GetMethodID(cSprite, "getMinV", "()F"); }
    jmethodID mMaxV = env->GetMethodID(cSprite, "h", "()F");
    if (!mMaxV && env->ExceptionCheck()) { env->ExceptionClear(); mMaxV = env->GetMethodID(cSprite, "getMaxV", "()F"); }
    env->DeleteLocalRef(cSprite);

    bool ok = false;
    if (mMinU && mMaxU && mMinV && mMaxV) {
        minU = env->CallFloatMethod(validSprite, mMinU);
        maxU = env->CallFloatMethod(validSprite, mMaxU);
        minV = env->CallFloatMethod(validSprite, mMinV);
        maxV = env->CallFloatMethod(validSprite, mMaxV);
        ok = (maxU > minU && maxV > minV);
    }
    env->DeleteLocalRef(validSprite);
    if (env->ExceptionCheck()) env->ExceptionClear();

    if (ok) {
        CachedSpriteInfo csi;
        csi.found = true;
        csi.minU = minU; csi.maxU = maxU;
        csi.minV = minV; csi.maxV = maxV;
        s_spriteCache[spriteName] = csi;
    }

    return ok;
}

// ── Кэш спрайтов активного Ресурс-Пака (OptiFine CIT / Custom Items) ─────────
struct CachedSprite {
    std::string name;
    std::string lower;
};
static std::vector<CachedSprite> s_resourcePackSpriteNames;
static DWORD s_lastCITScanTime = 0;

static void RefreshResourcePackSpriteCache(JNIEnv* env, jobject mc) {
    DWORD now = GetTickCount();
    if (s_lastCITScanTime != 0 && (now - s_lastCITScanTime < 15000)) {
        return; // обновляем кэш не чаще раз в 15 секунд
    }
    s_lastCITScanTime = now;
    s_resourcePackSpriteNames.clear();

    jobject tm = GetMCTextureMap(env, mc);
    if (!tm || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass cTM = env->GetObjectClass(tm);
    // В Minecraft 1.12.2 cdp (TextureMap) содержит поля mapRegisteredSprites и mapUploadedSprites
    const char* mapFields[] = { "k", "j", "h", "f", "g", "mapUploadedSprites", "mapRegisteredSprites", "field_110574_e", "field_110573_f" };
    jobject mapObj = nullptr;
    for (const char* mf : mapFields) {
        jfieldID fM = env->GetFieldID(cTM, mf, "Ljava/util/Map;");
        if (fM) {
            mapObj = env->GetObjectField(tm, fM);
            if (mapObj && !env->ExceptionCheck()) break;
            env->ExceptionClear();
            mapObj = nullptr;
        } else env->ExceptionClear();
    }

    // Если прямое поле не подошло — ищем через рефлексию любое поле с интерфейсом java.util.Map
    if (!mapObj) {
        jclass cClass = env->FindClass("java/lang/Class");
        if (cClass) {
            jmethodID mGetDeclaredFields = env->GetMethodID(cClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
            if (mGetDeclaredFields) {
                jobjectArray fields = reinterpret_cast<jobjectArray>(env->CallObjectMethod(cTM, mGetDeclaredFields));
                if (fields && !env->ExceptionCheck()) {
                    jclass cField = env->FindClass("java/lang/reflect/Field");
                    jclass cMapClass = env->FindClass("java/util/Map");
                    jmethodID mSetAcc = cField ? env->GetMethodID(cField, "setAccessible", "(Z)V") : nullptr;
                    jmethodID mGet = cField ? env->GetMethodID(cField, "get", "(Ljava/lang/Object;)Ljava/lang/Object;") : nullptr;
                    jmethodID mGetType = cField ? env->GetMethodID(cField, "getType", "()Ljava/lang/Class;") : nullptr;
                    jsize fLen = env->GetArrayLength(fields);
                    for (jsize fi = 0; fi < fLen && !mapObj; ++fi) {
                        jobject fld = env->GetObjectArrayElement(fields, fi);
                        if (fld) {
                            if (mGetType && cMapClass) {
                                jobject fType = env->CallObjectMethod(fld, mGetType);
                                if (fType && env->IsAssignableFrom(reinterpret_cast<jclass>(fType), cMapClass)) {
                                    if (mSetAcc) env->CallVoidMethod(fld, mSetAcc, JNI_TRUE);
                                    if (mGet) {
                                        mapObj = env->CallObjectMethod(fld, mGet, tm);
                                        if (env->ExceptionCheck()) { env->ExceptionClear(); mapObj = nullptr; }
                                    }
                                }
                                if (fType) env->DeleteLocalRef(fType);
                            }
                            env->DeleteLocalRef(fld);
                        }
                    }
                    if (cMapClass) env->DeleteLocalRef(cMapClass);
                    if (cField) env->DeleteLocalRef(cField);
                    env->DeleteLocalRef(fields);
                } else env->ExceptionClear();
            }
            env->DeleteLocalRef(cClass);
        }
    }

    env->DeleteLocalRef(cTM);
    env->DeleteLocalRef(tm);

    if (!mapObj) return;

    jclass cMap = env->GetObjectClass(mapObj);
    jmethodID mKeySet = env->GetMethodID(cMap, "keySet", "()Ljava/util/Set;");
    if (mKeySet) {
        jobject setObj = env->CallObjectMethod(mapObj, mKeySet);
        if (setObj && !env->ExceptionCheck()) {
            jclass cSet = env->GetObjectClass(setObj);
            jmethodID mToArr = env->GetMethodID(cSet, "toArray", "()[Ljava/lang/Object;");
            if (mToArr) {
                jobjectArray arr = reinterpret_cast<jobjectArray>(env->CallObjectMethod(setObj, mToArr));
                if (arr && !env->ExceptionCheck()) {
                    jsize len = env->GetArrayLength(arr);
                    for (jsize i = 0; i < len; ++i) {
                        jobject strObj = env->GetObjectArrayElement(arr, i);
                        if (strObj) {
                            jstring jS = reinterpret_cast<jstring>(strObj);
                            const char* chars = env->GetStringUTFChars(jS, nullptr);
                            if (chars) {
                                std::string s(chars);
                                env->ReleaseStringUTFChars(jS, chars);
                                if (!s.empty()) {
                                    std::string sLow = CleanAndLower(s);
                                    s_resourcePackSpriteNames.push_back({ s, sLow });
                                }
                            }
                            env->DeleteLocalRef(strObj);
                        }
                    }
                    env->DeleteLocalRef(arr);
                } else env->ExceptionClear();
            }
            env->DeleteLocalRef(cSet);
            env->DeleteLocalRef(setObj);
        } else env->ExceptionClear();
    }
    env->DeleteLocalRef(cMap);
    env->DeleteLocalRef(mapObj);
}

// ── Поиск подлинной иконки предмета напрямую из Ресурс-Пака (CIT / OptiFine / RustEx icons) ──
static std::unordered_map<std::string, CachedSpriteInfo> s_resourcePackCache;

static void InvalidateSpriteCachesForAtlas(unsigned int atlasTextureId) {
    if (atlasTextureId == 0 || atlasTextureId == s_cachedAtlasTextureId) return;

    s_cachedAtlasTextureId = atlasTextureId;
    s_spriteCache.clear();
    s_resourcePackCache.clear();
    s_resourcePackSpriteNames.clear();
    s_lastCITScanTime = 0;
}

static bool QueryResourcePackSprite(JNIEnv* env, jobject mc, int itemId, const std::string& itemName, float& minU, float& maxU, float& minV, float& maxV) {
    if (!env || !mc) return false;

    std::string lower = CleanAndLower(itemName);

    // O(1) Кэш найденных иконок ресурс-пака
    std::string rKey = std::to_string(itemId) + ":" + lower;
    auto itR = s_resourcePackCache.find(rKey);
    if (itR != s_resourcePackCache.end()) {
        if (!itR->second.found) return false;
        minU = itR->second.minU; maxU = itR->second.maxU;
        minV = itR->second.minV; maxV = itR->second.maxV;
        return true;
    }

    std::vector<const char*> candidates;

    if (lower.find("яблок") != std::string::npos || lower.find("apple") != std::string::npos) {
        candidates = { "icons/apple", "minecraft:icons/apple", "icon/apple", "items/apple" };
    } else if (lower.find("stone") != std::string::npos || lower.find("камен") != std::string::npos || lower.find("булыжник") != std::string::npos || lower.find("rock") != std::string::npos || itemId == 1 || itemId == 4 || itemId == 1125) {
        // Камень на сервере RustEx использует иконку icons/rock!
        candidates = { "icons/rock", "minecraft:icons/rock", "icon/rock", "icons/stones", "icons/stone", "items/rock", "items/stone", "blocks/stone", "rock", "stone" };
    } else if (lower.find("топор") != std::string::npos || lower.find("axe") != std::string::npos || lower.find("hatchet") != std::string::npos || lower.find("колун") != std::string::npos) {
        if (lower.find("самодел") != std::string::npos || lower.find("salvage") != std::string::npos) {
            candidates = { "icons/axe_salvaged", "icons/stonehatchet", "minecraft:icons/axe_salvaged", "axe_salvaged" };
        } else {
            candidates = { "icons/stonehatchet", "minecraft:icons/stonehatchet", "icon/stonehatchet", "icons/axe_salvaged", "items/iron_axe", "stonehatchet" };
        }
    } else if (lower.find("кирк") != std::string::npos || lower.find("pickaxe") != std::string::npos || lower.find("кайло") != std::string::npos) {
        if (lower.find("камен") != std::string::npos || itemId == 1451) {
            candidates = { "icons/stone_pickaxe", "minecraft:icons/stone_pickaxe", "stone_pickaxe", "icons/pickaxe", "icons/icepick_salvaged" };
        } else {
            candidates = { "icons/stone_pickaxe", "icons/pickaxe", "icons/icepick_salvaged", "minecraft:icons/stone_pickaxe", "items/iron_pickaxe", "stone_pickaxe" };
        }
    } else if (lower.find("деревянное копь") != std::string::npos || lower.find("деревянное копье") != std::string::npos || lower.find("wood spear") != std::string::npos || lower.find("wooden spear") != std::string::npos || itemId == 232) {
        candidates = { "icons/spear_wooden", "minecraft:icons/spear_wooden", "spear_wooden", "icons/spear_stone" };
    } else if (lower.find("каменное копь") != std::string::npos || lower.find("каменное копье") != std::string::npos || lower.find("stone spear") != std::string::npos || itemId == 367) {
        candidates = { "icons/spear_stone", "minecraft:icons/spear_stone", "spear_stone", "icons/spear_wooden" };
    } else if (lower.find("копь") != std::string::npos || lower.find("spear") != std::string::npos) {
        candidates = { "icons/spear_wooden", "icons/spear_stone", "minecraft:icons/spear_wooden", "spear_wooden" };
    } else if (itemId == 300 || itemId == 304 || itemId == 308 || itemId == 312 || itemId == 316 ||
               lower.find("штан") != std::string::npos || lower.find("понож") != std::string::npos || lower.find("брюк") != std::string::npos || lower.find("pant") != std::string::npos || lower.find("legging") != std::string::npos || lower.find("trouser") != std::string::npos || lower.find("kilt") != std::string::npos) {
        if (lower.find("мешковин") != std::string::npos || lower.find("burlap") != std::string::npos || itemId == 300) {
            candidates = { "item/rustme/icons/burlap_trousers", "rustme/icons/burlap_trousers", "icons/burlap_trousers", "icons/burlap_pants", "burlap_trousers", "icons/pants", "rustme/items/pants", "minecraft:icons/burlap_trousers", "minecraft:items/leather_leggings" };
        } else if (lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos) {
            candidates = { "icons/wood_armor_pants", "minecraft:icons/wood_armor_pants", "icons/burlap_trousers" };
        } else if (lower.find("знак") != std::string::npos || lower.find("roadsign") != std::string::npos || lower.find("kilt") != std::string::npos) {
            candidates = { "icons/roadsign_kilt", "minecraft:icons/roadsign_kilt" };
        } else if (lower.find("метал") != std::string::npos || lower.find("metal") != std::string::npos) {
            candidates = { "icons/metal_pants", "minecraft:icons/metal_pants", "minecraft:items/iron_leggings" };
        } else if (lower.find("кожа") != std::string::npos || lower.find("leather") != std::string::npos) {
            candidates = { "icons/pants", "icons/leather_pants", "minecraft:icons/pants", "minecraft:items/leather_leggings" };
        } else {
            candidates = { "item/rustme/icons/burlap_trousers", "icons/burlap_trousers", "icons/pants", "icons/roadsign_kilt", "icons/wood_armor_pants", "icons/metal_pants", "minecraft:icons/pants", "minecraft:items/leather_leggings" };
        }
    } else if (itemId == 299 || itemId == 303 || itemId == 307 || itemId == 311 || itemId == 315 ||
               lower.find("нагрудник") != std::string::npos || lower.find("куртк") != std::string::npos || lower.find("рубах") != std::string::npos || lower.find("жилет") != std::string::npos || lower.find("толстов") != std::string::npos || lower.find("худи") != std::string::npos || lower.find("футбол") != std::string::npos || lower.find("chest") != std::string::npos || lower.find("torso") != std::string::npos || lower.find("jacket") != std::string::npos || lower.find("shirt") != std::string::npos || lower.find("hoodie") != std::string::npos) {
        if (lower.find("мешковин") != std::string::npos || lower.find("burlap") != std::string::npos || itemId == 299) {
            candidates = { "item/rustme/icons/burlap_shirt", "rustme/icons/burlap_shirt", "icons/burlap_shirt", "burlap_shirt", "icons/jacket", "minecraft:icons/burlap_shirt", "minecraft:items/leather_chestplate" };
        } else if (lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos) {
            candidates = { "icons/wood_armor_torso", "minecraft:icons/wood_armor_torso", "icons/burlap_shirt" };
        } else if (lower.find("знак") != std::string::npos || lower.find("roadsign") != std::string::npos) {
            candidates = { "icons/roadsign_jacket", "minecraft:icons/roadsign_jacket" };
        } else if (lower.find("метал") != std::string::npos || lower.find("metal") != std::string::npos) {
            candidates = { "icons/metal_plate_torso", "minecraft:icons/metal_plate_torso", "minecraft:items/iron_chestplate" };
        } else if (lower.find("худи") != std::string::npos || lower.find("толстов") != std::string::npos || lower.find("hoodie") != std::string::npos) {
            candidates = { "icons/hoodie", "minecraft:icons/hoodie", "icons/jacket" };
        } else if (lower.find("рубах") != std::string::npos || lower.find("футбол") != std::string::npos || lower.find("shirt") != std::string::npos) {
            candidates = { "icons/shirt_collared", "icons/burlap_shirt", "icons/jacket" };
        } else if (lower.find("кожа") != std::string::npos || lower.find("leather") != std::string::npos || lower.find("куртк") != std::string::npos || lower.find("jacket") != std::string::npos) {
            candidates = { "icons/jacket", "icons/leather_jacket", "minecraft:icons/jacket", "minecraft:items/leather_chestplate" };
        } else {
            candidates = { "item/rustme/icons/burlap_shirt", "icons/burlap_shirt", "icons/jacket", "icons/metal_plate_torso", "icons/roadsign_jacket", "icons/wood_armor_torso", "minecraft:items/leather_chestplate" };
        }
    } else if (itemId == 301 || itemId == 305 || itemId == 309 || itemId == 313 || itemId == 317 ||
               lower.find("ботин") != std::string::npos || lower.find("обув") != std::string::npos || lower.find("сапог") != std::string::npos || lower.find("boot") != std::string::npos || lower.find("shoe") != std::string::npos) {
        if (lower.find("мешковин") != std::string::npos || lower.find("burlap") != std::string::npos || itemId == 301) {
            candidates = { "item/rustme/icons/burlap_shoes", "icons/burlap_shoes", "icons/shoes_boots", "minecraft:icons/burlap_shoes" };
        } else {
            candidates = { "icons/shoes_boots", "icons/shoes_workboots", "icons/burlap_shoes", "minecraft:icons/shoes_boots", "minecraft:items/leather_boots" };
        }
    } else if (itemId == 298 || itemId == 302 || itemId == 306 || itemId == 310 || itemId == 314 ||
               lower.find("шлем") != std::string::npos || lower.find("маск") != std::string::npos || lower.find("каск") != std::string::npos || lower.find("шапк") != std::string::npos || lower.find("кепк") != std::string::npos || lower.find("балаклав") != std::string::npos || lower.find("бандан") != std::string::npos || lower.find("повязк") != std::string::npos || lower.find("helmet") != std::string::npos || lower.find("mask") != std::string::npos || lower.find("hat") != std::string::npos || lower.find("cap") != std::string::npos) {
        if (lower.find("метал") != std::string::npos || lower.find("metal") != std::string::npos || lower.find("маск") != std::string::npos || lower.find("facemask") != std::string::npos) {
            candidates = { "icons/metal_facemask", "minecraft:icons/metal_facemask", "icons/coffeecan_helmet" };
        } else if (lower.find("кофей") != std::string::npos || lower.find("coffeecan") != std::string::npos) {
            candidates = { "icons/coffeecan_helmet", "minecraft:icons/coffeecan_helmet" };
        } else if (lower.find("ведр") != std::string::npos || lower.find("bucket") != std::string::npos) {
            candidates = { "icons/bucket_helmet", "minecraft:icons/bucket_helmet" };
        } else if (lower.find("каск") != std::string::npos || lower.find("riot") != std::string::npos) {
            candidates = { "icons/riot_helmet", "minecraft:icons/riot_helmet" };
        } else if (lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos) {
            candidates = { "icons/wood_armor_helmet", "minecraft:icons/wood_armor_helmet" };
        } else if (lower.find("бандан") != std::string::npos || lower.find("bandana") != std::string::npos) {
            candidates = { "icons/mask_bandana", "minecraft:icons/mask_bandana" };
        } else if (lower.find("балаклав") != std::string::npos || lower.find("balaclava") != std::string::npos) {
            candidates = { "icons/mask_balaclava", "minecraft:icons/mask_balaclava" };
        } else if (lower.find("повязк") != std::string::npos || lower.find("headwrap") != std::string::npos || itemId == 298) {
            candidates = { "item/rustme/icons/burlap_headwrap", "icons/burlap_headwrap", "minecraft:icons/burlap_headwrap" };
        } else {
            candidates = { "icons/metal_facemask", "icons/coffeecan_helmet", "icons/bucket_helmet", "icons/riot_helmet", "icons/hat_boonie", "icons/hat_cap", "minecraft:icons/metal_facemask", "minecraft:items/leather_helmet" };
        }
    } else if (lower.find("меч") != std::string::npos || lower.find("sword") != std::string::npos) {
        candidates = { "icons/salvaged_sword", "minecraft:icons/salvaged_sword", "salvaged_sword" };
    } else if (lower.find("мачет") != std::string::npos || lower.find("machete") != std::string::npos) {
        candidates = { "item/rustme/icons/machete", "rustme/icons/machete", "icons/machete", "minecraft:icons/machete", "machete" };
    } else if (lower.find("нож") != std::string::npos || lower.find("knife") != std::string::npos) {
        candidates = { "item/rustme/icons/knife_bone", "rustme/icons/knife_bone", "icons/knife_bone", "icons/knife_combat", "icons/knife_butcher", "knife_bone" };
    } else if (lower.find("ткань") != std::string::npos || lower.find("cloth") != std::string::npos) {
        candidates = { "item/rustme/icons/cloth", "rustme/icons/cloth", "icons/cloth", "cloth", "minecraft:icons/cloth", "minecraft:items/string" };
    } else if (lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos || lower.find("бревн") != std::string::npos || lower.find("log") != std::string::npos) {
        candidates = { "item/rustme/icons/wood", "rustme/icons/wood", "icons/wood", "item/rustme/icons/wood_log", "rustme/icons/wood_log", "icons/wood_log", "icons/log", "wood", "minecraft:icons/wood", "minecraft:blocks/log_oak", "blocks/log_oak" };
    } else if (lower.find("кост") != std::string::npos || lower.find("bone") != std::string::npos) {
        candidates = { "item/rustme/icons/bone_fragments", "rustme/icons/bone_fragments", "icons/bone_fragments", "icons/bone", "minecraft:icons/bone_fragments", "minecraft:items/bone", "bone" };
    } else if (lower.find("жир") != std::string::npos || lower.find("fat") != std::string::npos) {
        candidates = { "item/rustme/icons/fat_animal", "rustme/icons/fat_animal", "icons/fat_animal", "icons/fat", "minecraft:items/magma_cream" };
    } else if (lower.find("скрап") != std::string::npos || lower.find("металлолом") != std::string::npos || lower.find("scrap") != std::string::npos) {
        candidates = { "item/rustme/icons/scrap", "rustme/icons/scrap", "icons/scrap", "minecraft:icons/scrap", "scrap" };
    } else if (lower.find("фрагмент") != std::string::npos || (lower.find("метал") != std::string::npos && lower.find("руд") == std::string::npos)) {
        candidates = { "icons/metal_fragments", "icons/metal_refined", "minecraft:icons/metal_fragments" };
    } else if (lower.find("руд") != std::string::npos || lower.find("ore") != std::string::npos) {
        if (lower.find("сер") != std::string::npos || lower.find("sulfur") != std::string::npos) {
            candidates = { "icons/sulfur_ore", "minecraft:icons/sulfur_ore" };
        } else if (lower.find("мвк") != std::string::npos || lower.find("hq") != std::string::npos) {
            candidates = { "icons/hq_metal_ore", "minecraft:icons/hq_metal_ore" };
        } else {
            candidates = { "icons/metal_ore", "minecraft:icons/metal_ore", "icons/stones" };
        }
    } else if (lower.find("сер") != std::string::npos || lower.find("sulfur") != std::string::npos) {
        candidates = { "icons/sulfur", "icons/sulfur_ore", "minecraft:icons/sulfur" };
    } else if (lower.find("порох") != std::string::npos || lower.find("gunpowder") != std::string::npos) {
        candidates = { "icons/gunpowder", "minecraft:icons/gunpowder" };
    } else if (lower.find("топлив") != std::string::npos || lower.find("тнк") != std::string::npos || lower.find("fuel") != std::string::npos) {
        candidates = { "icons/lowgradefuel", "minecraft:icons/lowgradefuel" };
    } else if (lower.find("шприц") != std::string::npos || lower.find("syringe") != std::string::npos) {
        candidates = { "icons/syringe_medical", "minecraft:icons/syringe_medical" };
    } else if (lower.find("бинт") != std::string::npos || lower.find("bandage") != std::string::npos) {
        candidates = { "icons/bandage", "minecraft:icons/bandage" };
    } else if (lower.find("веревк") != std::string::npos || lower.find("rope") != std::string::npos) {
        candidates = { "icons/rope", "minecraft:icons/rope" };
    } else if (lower.find("шестер") != std::string::npos || lower.find("gear") != std::string::npos) {
        candidates = { "icons/gears", "minecraft:icons/gears" };
    } else if (lower.find("труб") != std::string::npos || lower.find("pipe") != std::string::npos) {
        candidates = { "icons/metalpipe", "minecraft:icons/metalpipe" };
    } else if (lower.find("пружин") != std::string::npos || lower.find("spring") != std::string::npos) {
        candidates = { "icons/metalspring", "minecraft:icons/metalspring" };
    } else if (lower.find("лист") != std::string::npos || lower.find("sheet") != std::string::npos) {
        candidates = { "icons/sheetmetal", "minecraft:icons/sheetmetal" };
    } else if (lower.find("знак") != std::string::npos || lower.find("sign") != std::string::npos) {
        candidates = { "icons/roadsigns", "minecraft:icons/roadsigns" };
    } else if (lower.find("кала") != std::string::npos || lower.find("ak") != std::string::npos) {
        candidates = { "icons/rifle_ak", "minecraft:icons/rifle_ak" };
    } else if (lower.find("болт") != std::string::npos || lower.find("bolt") != std::string::npos) {
        candidates = { "icons/rifle_bolt", "minecraft:icons/rifle_bolt" };
    } else if (lower.find("бердан") != std::string::npos || lower.find("полуавтомат") != std::string::npos || lower.find("semi") != std::string::npos) {
        candidates = { "icons/rifle_semiauto", "minecraft:icons/rifle_semiauto" };
    } else if (lower.find("томпсон") != std::string::npos || lower.find("thompson") != std::string::npos) {
        candidates = { "icons/smg_thompson", "minecraft:icons/smg_thompson" };
    } else if (lower.find("смг") != std::string::npos || lower.find("smg") != std::string::npos) {
        candidates = { "icons/smg_2", "icons/smg_mp5", "minecraft:icons/smg_2" };
    } else if (lower.find("арбалет") != std::string::npos || lower.find("crossbow") != std::string::npos) {
        candidates = { "icons/crossbow", "minecraft:icons/crossbow" };
    } else if (lower.find("лук") != std::string::npos || lower.find("bow") != std::string::npos || lower.find("hunting") != std::string::npos || itemId == 1396) {
        candidates = { "icons/bow_hunting", "icons/bow_compound", "minecraft:icons/bow_hunting", "bow_hunting", "bow" };
    } else if (lower.find("двуствол") != std::string::npos || lower.find("shotgun") != std::string::npos || lower.find("дроб") != std::string::npos) {
        candidates = { "icons/shotgun_double", "icons/shotgun_pump", "icons/shotgun_waterpipe", "minecraft:icons/shotgun_double" };
    } else if (lower.find("револьвер") != std::string::npos || lower.find("revolver") != std::string::npos) {
        candidates = { "icons/pistol_revolver", "icons/pistol_python", "minecraft:icons/pistol_revolver" };
    } else if (lower.find("пистолет") != std::string::npos || lower.find("pistol") != std::string::npos) {
        candidates = { "icons/pistol_semiauto", "icons/pistol_revolver", "icons/pistol_eoka", "minecraft:icons/pistol_semiauto" };
    } else if (lower.find("хазмат") != std::string::npos || lower.find("hazmat") != std::string::npos) {
        candidates = { "icons/hazmatsuit", "minecraft:icons/hazmatsuit" };
    } else if (lower.find("маск") != std::string::npos || lower.find("mask") != std::string::npos) {
        candidates = { "icons/metal_facemask", "minecraft:icons/metal_facemask" };
    } else if (lower.find("нагрудник") != std::string::npos || lower.find("chest") != std::string::npos || lower.find("torso") != std::string::npos) {
        candidates = { "icons/metal_plate_torso", "icons/roadsign_jacket", "minecraft:icons/metal_plate_torso" };
    } else if (lower.find("черник") != std::string::npos || lower.find("ягод") != std::string::npos || lower.find("berry") != std::string::npos) {
        candidates = { "icons/blue_berry", "minecraft:icons/blue_berry" };
    } else if (lower.find("гриб") != std::string::npos || lower.find("mushroom") != std::string::npos) {
        candidates = { "icons/mushroom", "minecraft:icons/mushroom" };
    } else if (lower.find("тыкв") != std::string::npos || lower.find("pumpkin") != std::string::npos || itemId == 86 || itemId == 91 || itemId == 104) {
        candidates = { "icons/pumpkin", "minecraft:icons/pumpkin", "pumpkin", "minecraft:blocks/pumpkin_face_off", "blocks/pumpkin_face_off" };
    } else if (lower.find("картоф") != std::string::npos || lower.find("картош") != std::string::npos || lower.find("potato") != std::string::npos || itemId == 392 || itemId == 393) {
        candidates = { "icons/potato", "icons/potato_baked", "potato", "minecraft:icons/potato", "minecraft:items/potato" };
    } else if (lower.find("хлеб") != std::string::npos || lower.find("bread") != std::string::npos || itemId == 297) {
        candidates = { "icons/bread", "minecraft:icons/bread", "bread", "minecraft:items/bread" };
    } else if (lower.find("свинин") != std::string::npos || lower.find("pork") != std::string::npos || itemId == 319 || itemId == 320) {
        candidates = { "icons/meat_pork", "icons/meat_boar", "meat_pork", "meat_boar", "minecraft:icons/meat_pork", "minecraft:items/porkchop_raw", "minecraft:items/porkchop_cooked" };
    } else if (lower.find("мясо") != std::string::npos || lower.find("meat") != std::string::npos || lower.find("beef") != std::string::npos || lower.find("стейк") != std::string::npos || lower.find("steak") != std::string::npos || itemId == 363 || itemId == 364 || itemId == 365 || itemId == 366) {
        candidates = { "item/rustme/icons/meat_raw", "rustme/icons/meat_raw", "icons/meat_raw", "icons/meat_cooked", "item/rustme/icons/meat_cooked", "rustme/icons/meat_cooked", "meat_raw", "meat_cooked", "minecraft:icons/meat_raw", "minecraft:items/beef_raw", "minecraft:items/beef_cooked", "icons/meat_boar", "icons/meat_pork", "icons/meat_bear" };
    } else if (lower.find("бревн") != std::string::npos || lower.find("log") != std::string::npos || lower.find("дерев") != std::string::npos || lower.find("wood") != std::string::npos) {
        candidates = { "item/rustme/icons/wood", "rustme/icons/wood", "icons/wood", "item/rustme/icons/wood_log", "rustme/icons/wood_log", "icons/wood_log", "icons/log", "wood", "minecraft:icons/wood", "minecraft:blocks/log_oak", "blocks/log_oak" };
    } else if (lower.find("боб") != std::string::npos || lower.find("консерв") != std::string::npos || lower.find("beans") != std::string::npos) {
        candidates = { "icons/can_beans", "minecraft:icons/can_beans" };
    } else if (lower.find("ракетница") != std::string::npos || lower.find("rocket_launcher") != std::string::npos) {
        candidates = { "icons/rocket_launcher", "minecraft:icons/rocket_launcher" };
    } else if (lower.find("ракет") != std::string::npos || lower.find("rocket") != std::string::npos) {
        candidates = { "icons/ammo_rocket_basic", "icons/ammo_rocket_hv", "minecraft:icons/ammo_rocket_basic" };
    } else if (lower.find("патрон") != std::string::npos || lower.find("ammo") != std::string::npos) {
        candidates = { "icons/ammo_rifle", "icons/ammo_pistol", "icons/ammo_shotgun" };
    } else if (lower.find("c4") != std::string::npos || lower.find("сишк") != std::string::npos || lower.find("взрывчат") != std::string::npos) {
        candidates = { "icons/explosive_timed", "icons/explosive_satchel", "icons/explosives" };
    } else if (lower.find("сачел") != std::string::npos || lower.find("satchel") != std::string::npos) {
        candidates = { "icons/explosive_satchel", "icons/explosive_timed" };
    } else if (lower.find("печь") != std::string::npos || lower.find("furnace") != std::string::npos) {
        candidates = { "icons/furnace", "icons/furnace_large", "minecraft:icons/furnace" };
    } else if (lower.find("костер") != std::string::npos || lower.find("campfire") != std::string::npos) {
        candidates = { "icons/campfire", "minecraft:icons/campfire" };
    } else if (lower.find("ящик") != std::string::npos || lower.find("сундук") != std::string::npos || lower.find("box") != std::string::npos) {
        candidates = { "icons/box_wooden_large", "icons/box_wooden", "minecraft:icons/box_wooden_large" };
    } else if (lower.find("спальник") != std::string::npos || lower.find("sleepingbag") != std::string::npos) {
        candidates = { "icons/sleepingbag", "minecraft:icons/sleepingbag" };
    } else if (lower.find("верстак") != std::string::npos || lower.find("workbench") != std::string::npos) {
        candidates = { "icons/workbench1", "icons/workbench2", "icons/workbench3" };
    } else if (lower.find("карьер") != std::string::npos || lower.find("quarry") != std::string::npos) {
        candidates = { "icons/mining_quarry", "icons/quarry" };
    } else if (lower.find("факел") != std::string::npos || lower.find("torch") != std::string::npos) {
        candidates = { "icons/torch", "minecraft:icons/torch" };
    } else if (lower.find("замок") != std::string::npos || lower.find("lock") != std::string::npos) {
        candidates = { "icons/lock_code", "icons/lock_key" };
    } else if (lower.find("дверь") != std::string::npos || lower.find("door") != std::string::npos) {
        candidates = { "icons/door_hinged_wood", "icons/door_hinged_metal" };
    }

    bool foundRP = false;
    // 1. Проверяем прямое попадание в candidates (быстрый O(1) хэш-поиск в Minecraft TextureMap)
    for (const char* cnd : candidates) {
        if (QuerySpriteByName(env, mc, cnd, minU, maxU, minV, maxV)) {
            foundRP = true;
            break;
        }
    }

    // 2. Если прямое имя не сработало — проверяем кэш зарегистрированных спрайтов атласа
    if (!foundRP && !candidates.empty()) {
        RefreshResourcePackSpriteCache(env, mc);
        if (!s_resourcePackSpriteNames.empty()) {
            for (const char* cnd : candidates) {
                const char* slash = strrchr(cnd, '/');
                const char* key = slash ? slash + 1 : cnd;
                size_t kLen = strlen(key);
                if (kLen < 3) continue;
                for (const auto& sp : s_resourcePackSpriteNames) {
                    if (sp.lower.find(key) != std::string::npos) {
                        if (QuerySpriteByName(env, mc, sp.name.c_str(), minU, maxU, minV, maxV)) {
                            foundRP = true;
                            break;
                        }
                    }
                }
                if (foundRP) break;
            }
        }
    }

    if (foundRP) {
        CachedSpriteInfo csi;
        csi.found = true;
        csi.minU = minU; csi.maxU = maxU;
        csi.minV = minV; csi.maxV = maxV;
        s_resourcePackCache[rKey] = csi;
    }

    return foundRP;
}

// ── Извлечение спрайта предмета из модели рендера Minecraft ──────────────────
static bool QueryItemSpriteUV(JNIEnv* env, jobject mc, jobject itemStack, float& minU, float& maxU, float& minV, float& maxV, std::string* outSpriteName = nullptr) {
    if (!env || !mc || !itemStack) return false;
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject ri = GetMCRenderItem(env, mc);
    if (!ri || env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    jclass cRI = env->GetObjectClass(ri);
    jobject model = nullptr;

    jclass cMC = env->GetObjectClass(mc);
    jfieldID fWorld = env->GetFieldID(cMC, "f", "Lbsb;");
    if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "world", "Lnet/minecraft/client/multiplayer/WorldClient;"); }
    jobject world = fWorld ? env->GetObjectField(mc, fWorld) : nullptr;

    jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
    if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
    jobject player = fPlayer ? env->GetObjectField(mc, fPlayer) : nullptr;
    env->DeleteLocalRef(cMC);

    // 1. RenderItem.getItemModelWithOverrides (OptiFine CIT для точных иконок из ресурс-пака)
    jmethodID mGetModelOverrides = env->GetMethodID(cRI, "a", "(Laip;Lamu;Lvp;)Lcfy;");
    if (!mGetModelOverrides && env->ExceptionCheck()) {
        env->ExceptionClear();
        mGetModelOverrides = env->GetMethodID(cRI, "getItemModelWithOverrides", "(Lnet/minecraft/item/ItemStack;Lnet/minecraft/world/World;Lnet/minecraft/entity/EntityLivingBase;)Lnet/minecraft/client/renderer/block/model/IBakedModel;");
    }
    if (!mGetModelOverrides && env->ExceptionCheck()) {
        env->ExceptionClear();
        mGetModelOverrides = env->GetMethodID(cRI, "func_184393_a", "(Lnet/minecraft/item/ItemStack;Lnet/minecraft/world/World;Lnet/minecraft/entity/EntityLivingBase;)Lnet/minecraft/client/renderer/block/model/IBakedModel;");
    }

    if (mGetModelOverrides) {
        model = env->CallObjectMethod(ri, mGetModelOverrides, itemStack, world, player);
        if (env->ExceptionCheck()) { env->ExceptionClear(); model = nullptr; }
    }

    if (player) env->DeleteLocalRef(player);
    if (world) env->DeleteLocalRef(world);

    // 2. Fallback через ItemModelMesher
    if (!model) {
        jmethodID mGetMesher = env->GetMethodID(cRI, "a", "()Lbuv;");
        if (!mGetMesher && env->ExceptionCheck()) { env->ExceptionClear(); mGetMesher = env->GetMethodID(cRI, "a", "()Lbva;"); }
        if (!mGetMesher && env->ExceptionCheck()) { env->ExceptionClear(); mGetMesher = env->GetMethodID(cRI, "getItemModelMesher", "()Lnet/minecraft/client/renderer/ItemModelMesher;"); }
        if (!mGetMesher && env->ExceptionCheck()) { env->ExceptionClear(); mGetMesher = env->GetMethodID(cRI, "func_175037_a", "()Lnet/minecraft/client/renderer/ItemModelMesher;"); }
        if (mGetMesher) {
            jobject mesher = env->CallObjectMethod(ri, mGetMesher);
            if (mesher && !env->ExceptionCheck()) {
                jclass cMesher = env->GetObjectClass(mesher);
                jmethodID mGetModel = env->GetMethodID(cMesher, "a", "(Laip;)Lcfy;");
                if (mGetModel) {
                    model = env->CallObjectMethod(mesher, mGetModel, itemStack);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); model = nullptr; }
                }
                env->DeleteLocalRef(cMesher);
                env->DeleteLocalRef(mesher);
            } else if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(cRI);
    env->DeleteLocalRef(ri);

    if (!model || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }

    bool foundUV = false;
    float bestMinU = 0.f, bestMaxU = 0.f, bestMinV = 0.f, bestMaxV = 0.f;
    std::string foundName = "";

    jclass cModel = env->GetObjectClass(model);

    // СПОСОБ 1 (ГЛАВНЫЙ): model.getParticleTexture() (cfy.d() / func_177554_e)
    // В OptiFine и Minecraft 1.12.2 это возвращает TextureAtlasSprite для ВСЕХ моделей предметов,
    // включая кастомные CIT 2D/3D предметы из Ресурс-Пака!
    jmethodID mGetSprite = env->GetMethodID(cModel, "d", "()Lcdq;");
    if (!mGetSprite && env->ExceptionCheck()) { env->ExceptionClear(); mGetSprite = env->GetMethodID(cModel, "d", "()Lcdt;"); }
    if (!mGetSprite && env->ExceptionCheck()) { env->ExceptionClear(); mGetSprite = env->GetMethodID(cModel, "getParticleTexture", "()Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;"); }
    if (!mGetSprite && env->ExceptionCheck()) { env->ExceptionClear(); mGetSprite = env->GetMethodID(cModel, "func_177554_e", "()Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;"); }

    if (mGetSprite) {
        jobject pSprite = env->CallObjectMethod(model, mGetSprite);
        if (pSprite && !env->ExceptionCheck()) {
            jclass cSp = env->GetObjectClass(pSprite);
            jmethodID mSpN = env->GetMethodID(cSp, "i", "()Ljava/lang/String;");
            if (!mSpN && env->ExceptionCheck()) { env->ExceptionClear(); mSpN = env->GetMethodID(cSp, "getIconName", "()Ljava/lang/String;"); }
            if (mSpN) {
                auto jSN = reinterpret_cast<jstring>(env->CallObjectMethod(pSprite, mSpN));
                if (jSN && !env->ExceptionCheck()) {
                    const char* cs = env->GetStringUTFChars(jSN, nullptr);
                    if (cs) { foundName = cs; env->ReleaseStringUTFChars(jSN, cs); }
                    env->DeleteLocalRef(jSN);
                } else env->ExceptionClear();
            }

            if (foundName != "missingno") {
                jmethodID mU1 = env->GetMethodID(cSp, "e", "()F");
                if (!mU1 && env->ExceptionCheck()) { env->ExceptionClear(); mU1 = env->GetMethodID(cSp, "getMinU", "()F"); }
                jmethodID mU2 = env->GetMethodID(cSp, "f", "()F");
                if (!mU2 && env->ExceptionCheck()) { env->ExceptionClear(); mU2 = env->GetMethodID(cSp, "getMaxU", "()F"); }
                jmethodID mV1 = env->GetMethodID(cSp, "g", "()F");
                if (!mV1 && env->ExceptionCheck()) { env->ExceptionClear(); mV1 = env->GetMethodID(cSp, "getMinV", "()F"); }
                jmethodID mV2 = env->GetMethodID(cSp, "h", "()F");
                if (!mV2 && env->ExceptionCheck()) { env->ExceptionClear(); mV2 = env->GetMethodID(cSp, "getMaxV", "()F"); }

                if (mU1 && mU2 && mV1 && mV2) {
                    float u1 = env->CallFloatMethod(pSprite, mU1);
                    float u2 = env->CallFloatMethod(pSprite, mU2);
                    float v1 = env->CallFloatMethod(pSprite, mV1);
                    float v2 = env->CallFloatMethod(pSprite, mV2);
                    if (u2 > u1 && v2 > v1) {
                        bestMinU = u1; bestMaxU = u2;
                        bestMinV = v1; bestMaxV = v2;
                        foundUV = true;
                    }
                }
            }
            env->DeleteLocalRef(cSp);
            env->DeleteLocalRef(pSprite);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // СПОСОБ 2 (Fallback): Если getParticleTexture() не найден или missingno, проверяем квады модели
    // ВАЖНО: Ищем ТОЛЬКО полный TextureAtlasSprite из quad.getSprite() (bvp.a() в 1.12.2).
    // Чтение сырых UV из int[] vertexData строго ЗАПРЕЩЕНО, т.к. у 3D моделей (шлемы, броня)
    // оно берет полигон одной грани куба и режет иконку на уродливые полосы (как в HUD/ESP)!
    if (!foundUV) {
        jmethodID mGetQuads = env->GetMethodID(cModel, "a", "(Lawt;Lfa;J)Ljava/util/List;");
        if (!mGetQuads && env->ExceptionCheck()) {
            env->ExceptionClear();
            mGetQuads = env->GetMethodID(cModel, "getQuads", "(Lnet/minecraft/block/state/IBlockState;Lnet/minecraft/util/EnumFacing;J)Ljava/util/List;");
        }
        if (!mGetQuads && env->ExceptionCheck()) {
            env->ExceptionClear();
            mGetQuads = env->GetMethodID(cModel, "func_188616_a", "(Lnet/minecraft/block/state/IBlockState;Lnet/minecraft/util/EnumFacing;J)Ljava/util/List;");
        }

        auto InspectQuadList = [&](jobject qList) -> bool {
            if (!qList || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
            jclass cList = env->GetObjectClass(qList);
            jmethodID mSize = env->GetMethodID(cList, "size", "()I");
            jmethodID mGet = env->GetMethodID(cList, "get", "(I)Ljava/lang/Object;");
            int qCount = (mSize && mGet) ? env->CallIntMethod(qList, mSize) : 0;
            if (env->ExceptionCheck()) { env->ExceptionClear(); qCount = 0; }

            for (int qi = 0; qi < qCount; ++qi) {
                jobject quad = env->CallObjectMethod(qList, mGet, qi);
                if (!quad || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                jclass cQuad = env->GetObjectClass(quad);

                // Получаем полный TextureAtlasSprite из quad (bvp.a() в 1.12.2)
                jobject qSprite = nullptr;
                const char* spriteMethods[] = { "a", "d", "getSprite", "func_187508_a", "e" };
                const char* spriteSigs[] = { "()Lcdq;", "()Lcdt;", "()Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;" };
                for (const char* sm : spriteMethods) {
                    for (const char* ss : spriteSigs) {
                        jmethodID mQS = env->GetMethodID(cQuad, sm, ss);
                        if (mQS) {
                            qSprite = env->CallObjectMethod(quad, mQS);
                            if (env->ExceptionCheck()) { env->ExceptionClear(); qSprite = nullptr; }
                            if (qSprite) break;
                        } else env->ExceptionClear();
                    }
                    if (qSprite) break;
                }

                if (qSprite) {
                    jclass cSp = env->GetObjectClass(qSprite);
                    jmethodID mSpN = env->GetMethodID(cSp, "i", "()Ljava/lang/String;");
                    if (!mSpN && env->ExceptionCheck()) { env->ExceptionClear(); mSpN = env->GetMethodID(cSp, "getIconName", "()Ljava/lang/String;"); }
                    if (mSpN) {
                        auto jSN = reinterpret_cast<jstring>(env->CallObjectMethod(qSprite, mSpN));
                        if (jSN && !env->ExceptionCheck()) {
                            const char* cs = env->GetStringUTFChars(jSN, nullptr);
                            if (cs) { foundName = cs; env->ReleaseStringUTFChars(jSN, cs); }
                            env->DeleteLocalRef(jSN);
                        } else env->ExceptionClear();
                    }
                    if (foundName != "missingno") {
                        jmethodID mU1 = env->GetMethodID(cSp, "e", "()F");
                        if (!mU1) { env->ExceptionClear(); mU1 = env->GetMethodID(cSp, "getMinU", "()F"); }
                        jmethodID mU2 = env->GetMethodID(cSp, "f", "()F");
                        if (!mU2) { env->ExceptionClear(); mU2 = env->GetMethodID(cSp, "getMaxU", "()F"); }
                        jmethodID mV1 = env->GetMethodID(cSp, "g", "()F");
                        if (!mV1) { env->ExceptionClear(); mV1 = env->GetMethodID(cSp, "getMinV", "()F"); }
                        jmethodID mV2 = env->GetMethodID(cSp, "h", "()F");
                        if (!mV2) { env->ExceptionClear(); mV2 = env->GetMethodID(cSp, "getMaxV", "()F"); }
                        if (mU1 && mU2 && mV1 && mV2) {
                            float u1 = env->CallFloatMethod(qSprite, mU1);
                            float u2 = env->CallFloatMethod(qSprite, mU2);
                            float v1 = env->CallFloatMethod(qSprite, mV1);
                            float v2 = env->CallFloatMethod(qSprite, mV2);
                            if (u2 > u1 && v2 > v1) {
                                bestMinU = u1; bestMaxU = u2;
                                bestMinV = v1; bestMaxV = v2;
                                foundUV = true;
                            }
                        }
                    }
                    env->DeleteLocalRef(cSp);
                    env->DeleteLocalRef(qSprite);
                }

                env->DeleteLocalRef(cQuad);
                env->DeleteLocalRef(quad);
                if (foundUV) break;
            }

            env->DeleteLocalRef(cList);
            env->DeleteLocalRef(qList);
            return foundUV;
        };

        if (mGetQuads) {
            jobject qList = env->CallObjectMethod(model, mGetQuads, nullptr, nullptr, (jlong)0);
            InspectQuadList(qList);
        }

        if (!foundUV && mGetQuads) {
            jobject cl = GetMinecraftClassLoader(env);
            jclass cFacing = FindMCClass(env, cl, "fa");
            if (!cFacing) cFacing = FindMCClass(env, cl, "net/minecraft/util/EnumFacing");
            if (cFacing) {
                // 1. Метод EnumFacing.values()
                jmethodID mValues = env->GetStaticMethodID(cFacing, "values", "()[Lfa;");
                if (!mValues) { env->ExceptionClear(); mValues = env->GetStaticMethodID(cFacing, "values", "()[Lnet/minecraft/util/EnumFacing;"); }
                jobjectArray arrF = nullptr;
                if (mValues) {
                    arrF = reinterpret_cast<jobjectArray>(env->CallStaticObjectMethod(cFacing, mValues));
                    if (env->ExceptionCheck()) { env->ExceptionClear(); arrF = nullptr; }
                }

                if (arrF) {
                    jsize fCount = env->GetArrayLength(arrF);
                    for (jsize fi = 0; fi < fCount && !foundUV; ++fi) {
                        jobject facingObj = env->GetObjectArrayElement(arrF, fi);
                        if (facingObj) {
                            jobject fList = env->CallObjectMethod(model, mGetQuads, nullptr, facingObj, (jlong)0);
                            InspectQuadList(fList);
                            env->DeleteLocalRef(facingObj);
                        }
                    }
                    env->DeleteLocalRef(arrF);
                } else {
                    // Fallback: статические поля 6 направлений EnumFacing (a..f = DOWN, UP, NORTH, SOUTH, WEST, EAST)
                    const char* fNames[] = { "c", "d", "a", "b", "e", "f", "NORTH", "SOUTH", "DOWN", "UP", "WEST", "EAST" };
                    for (const char* fn : fNames) {
                        jfieldID fF = env->GetStaticFieldID(cFacing, fn, "Lfa;");
                        if (!fF) { env->ExceptionClear(); fF = env->GetStaticFieldID(cFacing, fn, "Lnet/minecraft/util/EnumFacing;"); }
                        if (fF) {
                            jobject facingObj = env->GetStaticObjectField(cFacing, fF);
                            if (facingObj && !env->ExceptionCheck()) {
                                jobject fList = env->CallObjectMethod(model, mGetQuads, nullptr, facingObj, (jlong)0);
                                InspectQuadList(fList);
                                env->DeleteLocalRef(facingObj);
                                if (foundUV) break;
                            } else env->ExceptionClear();
                        } else env->ExceptionClear();
                    }
                }
                env->DeleteLocalRef(cFacing);
            }
            if (cl) env->DeleteLocalRef(cl);
        }
    }

    env->DeleteLocalRef(cModel);
    env->DeleteLocalRef(model);

    if (foundUV) {
        minU = bestMinU; maxU = bestMaxU;
        minV = bestMinV; maxV = bestMaxV;
        if (outSpriteName && !foundName.empty()) *outSpriteName = foundName;
        return true;
    }
    return false;
}

static void ParseItemStackHelper(JNIEnv* env, jobject mc, jobject itemStack, ArmorItem& out) {
    if (!itemStack || env->ExceptionCheck()) {
        env->ExceptionClear();
        out.isEmpty = true;
        out.name = "";
        out.itemId = 0;
        out.enchantments.clear();
        return;
    }

    if (env->PushLocalFrame(64) != 0) {
        out.isEmpty = true;
        return;
    }

    jclass cIS = env->GetObjectClass(itemStack);

    // aip.b() -> boolean isEmpty
    jmethodID mEmpty = env->GetMethodID(cIS, "b", "()Z");
    if (!mEmpty) {
        env->ExceptionClear();
        mEmpty = env->GetMethodID(cIS, "isEmpty", "()Z");
    }
    if (!mEmpty) {
        env->ExceptionClear();
        mEmpty = env->GetMethodID(cIS, "func_190926_b", "()Z");
    }

    if (mEmpty && env->CallBooleanMethod(itemStack, mEmpty)) {
        out.isEmpty = true;
        out.name = "";
        out.itemId = 0;
        out.enchantments.clear();
        env->PopLocalFrame(nullptr);
        return;
    }

    out.isEmpty = false;
    out.enchantments.clear();

    // aip.E() -> int getCount
    jmethodID mCount = env->GetMethodID(cIS, "E", "()I");
    if (!mCount) { env->ExceptionClear(); mCount = env->GetMethodID(cIS, "getCount", "()I"); }
    if (!mCount) { env->ExceptionClear(); mCount = env->GetMethodID(cIS, "func_190916_E", "()I"); }
    out.count = mCount ? env->CallIntMethod(itemStack, mCount) : 1;
    if (out.count < 1) out.count = 1;

    // aip.r() -> String getDisplayName
    jmethodID mName = env->GetMethodID(cIS, "r", "()Ljava/lang/String;");
    if (!mName) {
        env->ExceptionClear();
        mName = env->GetMethodID(cIS, "getDisplayName", "()Ljava/lang/String;");
    }
    if (!mName) {
        env->ExceptionClear();
        mName = env->GetMethodID(cIS, "func_82833_r", "()Ljava/lang/String;");
    }

    if (mName) {
        auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(itemStack, mName));
        if (jStr && !env->ExceptionCheck()) {
            const char* chars = env->GetStringUTFChars(jStr, nullptr);
            if (chars) {
                out.name = StripColorCodes(chars);
                env->ReleaseStringUTFChars(jStr, chars);
            }
            env->DeleteLocalRef(jStr);
        } else {
            env->ExceptionClear();
        }
    }

    // aip.c() -> ain (Item) -> ain.a(ain) -> int ID
    jmethodID mItem = env->GetMethodID(cIS, "c", "()Lain;");
    if (!mItem) {
        env->ExceptionClear();
        mItem = env->GetMethodID(cIS, "getItem", "()Lnet/minecraft/item/Item;");
    }
    if (!mItem) {
        env->ExceptionClear();
        mItem = env->GetMethodID(cIS, "func_77973_b", "()Lnet/minecraft/item/Item;");
    }

    jobject itemObj = nullptr;
    if (mItem) {
        itemObj = env->CallObjectMethod(itemStack, mItem);
        if (itemObj && !env->ExceptionCheck()) {
            jobject cl = GetMinecraftClassLoader(env);
            jclass cItemBase = FindMCClass(env, cl, "ain");
            if (!cItemBase) cItemBase = FindMCClass(env, cl, "net/minecraft/item/Item");
            if (cItemBase) {
                jmethodID mGetId = env->GetStaticMethodID(cItemBase, "a", "(Lain;)I");
                if (!mGetId) {
                    env->ExceptionClear();
                    mGetId = env->GetStaticMethodID(cItemBase, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
                }
                if (mGetId) {
                    out.itemId = env->CallStaticIntMethod(cItemBase, mGetId, itemObj);
                }
                env->DeleteLocalRef(cItemBase);
            }
        } else {
            env->ExceptionClear();
            itemObj = nullptr;
        }
    }

    // Сохраняем fingerprint NBT: CIT/серверные модели часто различаются
    // только тегами предмета при совпадающих ID, имени и damage.
    std::string itemNbtKey;
    // Custom ID check from NBT: stack.getTagCompound().getInteger("CustomID")
    jmethodID mGetTag = env->GetMethodID(cIS, "p", "()Lfy;");
    if (!mGetTag) { env->ExceptionClear(); mGetTag = env->GetMethodID(cIS, "getTagCompound", "()Lnet/minecraft/nbt/NBTTagCompound;"); }
    if (!mGetTag) { env->ExceptionClear(); mGetTag = env->GetMethodID(cIS, "func_77978_p", "()Lnet/minecraft/nbt/NBTTagCompound;"); }
    if (mGetTag) {
        jobject tagComp = env->CallObjectMethod(itemStack, mGetTag);
        if (tagComp && !env->ExceptionCheck()) {
            jclass cComp = env->GetObjectClass(tagComp);
            jmethodID mTagToString = env->GetMethodID(cComp, "toString", "()Ljava/lang/String;");
            if (mTagToString) {
                auto jTagText = reinterpret_cast<jstring>(env->CallObjectMethod(tagComp, mTagToString));
                if (jTagText && !env->ExceptionCheck()) {
                    const char* tagChars = env->GetStringUTFChars(jTagText, nullptr);
                    if (tagChars) {
                        itemNbtKey.assign(tagChars);
                        env->ReleaseStringUTFChars(jTagText, tagChars);
                    }
                    env->DeleteLocalRef(jTagText);
                } else if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            jmethodID mGetInt = env->GetMethodID(cComp, "h", "(Ljava/lang/String;)I");
            if (!mGetInt) { env->ExceptionClear(); mGetInt = env->GetMethodID(cComp, "getInteger", "(Ljava/lang/String;)I"); }
            if (!mGetInt) { env->ExceptionClear(); mGetInt = env->GetMethodID(cComp, "func_74762_e", "(Ljava/lang/String;)I"); }
            if (mGetInt) {
                jstring jCustom = env->NewStringUTF("CustomID");
                int customId = env->CallIntMethod(tagComp, mGetInt, jCustom);
                env->DeleteLocalRef(jCustom);
                if (customId <= 0) {
                    jstring jCustom2 = env->NewStringUTF("customId");
                    customId = env->CallIntMethod(tagComp, mGetInt, jCustom2);
                    env->DeleteLocalRef(jCustom2);
                }
                if (customId <= 0) {
                    jstring jCustom3 = env->NewStringUTF("ItemId");
                    customId = env->CallIntMethod(tagComp, mGetInt, jCustom3);
                    env->DeleteLocalRef(jCustom3);
                }
                if (customId > 0) out.itemId = customId;
            }
            env->DeleteLocalRef(cComp);
            env->DeleteLocalRef(tagComp);
        } else env->ExceptionClear();
    }

    // ── ОБХОД ItemESP / NameTags для предметов с анти-чит маскировкой ("sosihuy") ───
    std::string lowerCheck = out.name;
    for (char& ch : lowerCheck) ch = static_cast<char>(tolower(ch));
    bool isMasked = lowerCheck.empty() || lowerCheck.find("sosi") != std::string::npos || lowerCheck.find("huy") != std::string::npos;

    if (isMasked) {
        // 1. Проверяем кастомную базу предметов сервера (Rust CustomID)
        auto itCust = CUSTOM_ITEM_IDS.find(out.itemId);
        if (itCust != CUSTOM_ITEM_IDS.end()) {
            out.name = itCust->second;
            isMasked = false;
        }

        // 2. Вызываем Item.getItemStackDisplayName(stack) (ain.a(aip)) — возвращает подлинное имя без кастомного тега
        if (isMasked && itemObj) {
            jclass cItem2 = env->GetObjectClass(itemObj);
            jmethodID mDisp = env->GetMethodID(cItem2, "a", "(Laip;)Ljava/lang/String;");
            if (!mDisp) { env->ExceptionClear(); mDisp = env->GetMethodID(cItem2, "getItemStackDisplayName", "(Lnet/minecraft/item/ItemStack;)Ljava/lang/String;"); }
            if (!mDisp) { env->ExceptionClear(); mDisp = env->GetMethodID(cItem2, "func_77653_i", "(Lnet/minecraft/item/ItemStack;)Ljava/lang/String;"); }
            if (mDisp) {
                auto jD = reinterpret_cast<jstring>(env->CallObjectMethod(itemObj, mDisp, itemStack));
                if (jD && !env->ExceptionCheck()) {
                    const char* dChars = env->GetStringUTFChars(jD, nullptr);
                    if (dChars && dChars[0] != '\0') {
                        std::string dStr = dChars;
                        std::string lowerD = dStr;
                        for (char& c : lowerD) c = static_cast<char>(tolower(c));
                        if (lowerD.find("sosi") == std::string::npos && lowerD.find("huy") == std::string::npos) {
                            out.name = dStr;
                            isMasked = false;
                        }
                        env->ReleaseStringUTFChars(jD, dChars);
                    }
                    env->DeleteLocalRef(jD);
                } else env->ExceptionClear();
            }

            // 3. Если всё еще замаскировано — читаем unlocalized name (ain.a() / func_77658_a)
            if (isMasked) {
                jmethodID mUnloc = env->GetMethodID(cItem2, "a", "()Ljava/lang/String;");
                if (!mUnloc) { env->ExceptionClear(); mUnloc = env->GetMethodID(cItem2, "getUnlocalizedName", "()Ljava/lang/String;"); }
                if (!mUnloc) { env->ExceptionClear(); mUnloc = env->GetMethodID(cItem2, "func_77658_a", "()Ljava/lang/String;"); }
                if (mUnloc) {
                    auto jU = reinterpret_cast<jstring>(env->CallObjectMethod(itemObj, mUnloc));
                    if (jU && !env->ExceptionCheck()) {
                        const char* uChars = env->GetStringUTFChars(jU, nullptr);
                        if (uChars && uChars[0] != '\0') {
                            std::string uStr = uChars;
                            if (uStr.rfind("item.", 0) == 0) uStr = uStr.substr(5);
                            else if (uStr.rfind("tile.", 0) == 0) uStr = uStr.substr(5);
                            if (!uStr.empty()) {
                                uStr[0] = static_cast<char>(toupper(uStr[0]));
                                out.name = uStr;
                            }
                            env->ReleaseStringUTFChars(jU, uChars);
                        }
                        env->DeleteLocalRef(jU);
                    } else env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(cItem2);
        }
    }
    if (itemObj) env->DeleteLocalRef(itemObj);

    // Очистка от цветовых кодов и назначение человекочитаемого имени для брони, если имя пустое / замаскировано
    out.name = StripColorCodes(out.name);
    if (out.name.empty() || out.name == "§f") {
        switch (out.itemId) {
            case 298: out.name = "Кожаный шлем"; break;
            case 299: out.name = "Кожаная куртка"; break;
            case 300: out.name = "Кожаные штаны"; break;
            case 301: out.name = "Кожаные ботинки"; break;
            case 302: out.name = "Кольчужный шлем"; break;
            case 303: out.name = "Кольчужная куртка"; break;
            case 304: out.name = "Кольчужные штаны"; break;
            case 305: out.name = "Кольчужные ботинки"; break;
            case 306: out.name = "Железный шлем"; break;
            case 307: out.name = "Железный нагрудник"; break;
            case 308: out.name = "Железные поножи"; break;
            case 309: out.name = "Железные ботинки"; break;
            case 310: out.name = "Алмазный шлем"; break;
            case 311: out.name = "Алмазный нагрудник"; break;
            case 312: out.name = "Алмазные поножи"; break;
            case 313: out.name = "Алмазные ботинки"; break;
            case 314: out.name = "Золотой шлем"; break;
            case 315: out.name = "Золотой нагрудник"; break;
            case 316: out.name = "Золотые поножи"; break;
            case 317: out.name = "Золотые ботинки"; break;
            case 276: out.name = "Алмазный меч"; break;
            case 278: out.name = "Алмазная кирка"; break;
            case 279: out.name = "Алмазный топор"; break;
            case 267: out.name = "Железный меч"; break;
            case 257: out.name = "Железная кирка"; break;
            case 258: out.name = "Железный топор"; break;
            case 272: out.name = "Каменный меч"; break;
            case 274: out.name = "Каменная кирка"; break;
            case 275: out.name = "Каменный топор"; break;
            case 261: out.name = "Лук"; break;
            case 262: out.name = "Стрелы"; break;
            case 392: out.name = "Картофель"; break;
            case 393: out.name = "Печеный картофель"; break;
            case 297: out.name = "Хлеб"; break;
            case 260: out.name = "Яблоко"; break;
            case 322: out.name = "Золотое яблоко"; break;
            case 391: out.name = "Морковь"; break;
            case 86:  out.name = "Тыква"; break;
            case 364: out.name = "Стейк"; break;
            case 363: out.name = "Сырая говядина"; break;
            case 320: out.name = "Жареная свинина"; break;
            case 319: out.name = "Сырая свинина"; break;
        }
    }

    // aip.j() -> current damage, aip.k() -> max damage
    jmethodID mCurD = env->GetMethodID(cIS, "j", "()I");
    if (!mCurD) { env->ExceptionClear(); mCurD = env->GetMethodID(cIS, "getItemDamage", "()I"); }
    if (!mCurD) { env->ExceptionClear(); mCurD = env->GetMethodID(cIS, "func_77952_i", "()I"); }

    jmethodID mMaxD = env->GetMethodID(cIS, "k", "()I");
    if (!mMaxD) { env->ExceptionClear(); mMaxD = env->GetMethodID(cIS, "getMaxDamage", "()I"); }
    if (!mMaxD) { env->ExceptionClear(); mMaxD = env->GetMethodID(cIS, "func_77958_k", "()I"); }

    if (mMaxD && mCurD) {
        out.curDamage = env->CallIntMethod(itemStack, mCurD);
        out.maxDamage = env->CallIntMethod(itemStack, mMaxD);
        if (out.maxDamage > 0) {
            out.durability = 1.0f - (static_cast<float>(out.curDamage) / static_cast<float>(out.maxDamage));
            if (out.durability < 0.0f) out.durability = 0.0f;
        } else {
            out.durability = 1.0f;
        }
    } else {
        out.durability = 1.0f;
    }

    // Enchantments list from stack.getEnchantmentTagList() (q() / func_77986_q)
    jmethodID mGetEnchList = env->GetMethodID(cIS, "q", "()Lfi;");
    if (!mGetEnchList) { env->ExceptionClear(); mGetEnchList = env->GetMethodID(cIS, "getEnchantmentTagList", "()Lnet/minecraft/nbt/NBTTagList;"); }
    if (!mGetEnchList) { env->ExceptionClear(); mGetEnchList = env->GetMethodID(cIS, "func_77986_q", "()Lnet/minecraft/nbt/NBTTagList;"); }
    if (mGetEnchList) {
        jobject enchList = env->CallObjectMethod(itemStack, mGetEnchList);
        if (enchList && !env->ExceptionCheck()) {
            jclass cEnchList = env->GetObjectClass(enchList);
            jmethodID mTagCount = env->GetMethodID(cEnchList, "c", "()I");
            if (!mTagCount) { env->ExceptionClear(); mTagCount = env->GetMethodID(cEnchList, "tagCount", "()I"); }
            if (!mTagCount) { env->ExceptionClear(); mTagCount = env->GetMethodID(cEnchList, "func_74745_c", "()I"); }

            jmethodID mGetCompound = env->GetMethodID(cEnchList, "b", "(I)Lfy;");
            if (!mGetCompound) { env->ExceptionClear(); mGetCompound = env->GetMethodID(cEnchList, "getCompoundTagAt", "(I)Lnet/minecraft/nbt/NBTTagCompound;"); }
            if (!mGetCompound) { env->ExceptionClear(); mGetCompound = env->GetMethodID(cEnchList, "func_150305_b", "(I)Lnet/minecraft/nbt/NBTTagCompound;"); }

            int count = (mTagCount && mGetCompound) ? env->CallIntMethod(enchList, mTagCount) : 0;
            for (int i = 0; i < count; ++i) {
                jobject enchComp = env->CallObjectMethod(enchList, mGetCompound, i);
                if (enchComp && !env->ExceptionCheck()) {
                    jclass cEC = env->GetObjectClass(enchComp);
                    jmethodID mGetShort = env->GetMethodID(cEC, "e", "(Ljava/lang/String;)S");
                    if (!mGetShort) { env->ExceptionClear(); mGetShort = env->GetMethodID(cEC, "getShort", "(Ljava/lang/String;)S"); }
                    if (!mGetShort) { env->ExceptionClear(); mGetShort = env->GetMethodID(cEC, "func_74765_d", "(Ljava/lang/String;)S"); }
                    if (mGetShort) {
                        jstring jId = env->NewStringUTF("id");
                        jstring jLvl = env->NewStringUTF("lvl");
                        short id = env->CallShortMethod(enchComp, mGetShort, jId);
                        short lvl = env->CallShortMethod(enchComp, mGetShort, jLvl);
                        env->DeleteLocalRef(jId);
                        env->DeleteLocalRef(jLvl);

                        char eBuf[32] = "";
                        switch (id) {
                            case 0:  snprintf(eBuf, sizeof(eBuf), "Pro%d", lvl); break;
                            case 1:  snprintf(eBuf, sizeof(eBuf), "Firp%d", lvl); break;
                            case 2:  snprintf(eBuf, sizeof(eBuf), "Fea%d", lvl); break;
                            case 3:  snprintf(eBuf, sizeof(eBuf), "Bla%d", lvl); break;
                            case 7:  snprintf(eBuf, sizeof(eBuf), "Thr%d", lvl); break;
                            case 8:  snprintf(eBuf, sizeof(eBuf), "Dep%d", lvl); break;
                            case 10: snprintf(eBuf, sizeof(eBuf), "Bindi"); break;
                            case 16: snprintf(eBuf, sizeof(eBuf), "Shr%d", lvl); break;
                            case 19: snprintf(eBuf, sizeof(eBuf), "Kno%d", lvl); break;
                            case 20: snprintf(eBuf, sizeof(eBuf), "Fir%d", lvl); break;
                            case 21: snprintf(eBuf, sizeof(eBuf), "Loot%d", lvl); break;
                            case 22: snprintf(eBuf, sizeof(eBuf), "Swe%d", lvl); break;
                            case 32: snprintf(eBuf, sizeof(eBuf), "Eff%d", lvl); break;
                            case 33: snprintf(eBuf, sizeof(eBuf), "Sil%d", lvl); break;
                            case 34: snprintf(eBuf, sizeof(eBuf), "Unb%d", lvl); break;
                            case 35: snprintf(eBuf, sizeof(eBuf), "For%d", lvl); break;
                            case 48: snprintf(eBuf, sizeof(eBuf), "Pow%d", lvl); break;
                            case 49: snprintf(eBuf, sizeof(eBuf), "Pun%d", lvl); break;
                            case 50: snprintf(eBuf, sizeof(eBuf), "Fla%d", lvl); break;
                            case 51: snprintf(eBuf, sizeof(eBuf), "Inf%d", lvl); break;
                            case 70: snprintf(eBuf, sizeof(eBuf), "Men"); break;
                            case 71: snprintf(eBuf, sizeof(eBuf), "Vanish"); break;
                        }
                        if (eBuf[0] != '\0') {
                            out.enchantments.push_back(eBuf);
                        }
                    }
                    env->DeleteLocalRef(cEC);
                    env->DeleteLocalRef(enchComp);
                } else env->ExceptionClear();
            }
            env->DeleteLocalRef(cEnchList);
            env->DeleteLocalRef(enchList);
        } else env->ExceptionClear();
    }

    struct CachedItemInfo {
        bool hasIcon = false;
        float minU = 0.f, maxU = 0.f, minV = 0.f, maxV = 0.f;
        std::string finalName;
    };
    static std::unordered_map<std::string, CachedItemInfo> s_itemParsedCache;

    // Две серверные вещи могут быть одним и тем же ванильным предметом с
    // одинаковым отображаемым именем, но разным CustomID/NBT и разными
    // текстурами модели (в частности, серверные скины). Не переиспользуем UV
    // между такими стаками: добавляем метаданные предмета в ключ кэша.
    std::string cacheKey = std::to_string(s_cachedAtlasTextureId) + ":" +
        std::to_string(out.itemId) + ":" + std::to_string(out.curDamage) + ":" +
        std::to_string(out.maxDamage) + ":" + out.name + ":" + itemNbtKey;
    auto itCache = s_itemParsedCache.find(cacheKey);
    if (itCache != s_itemParsedCache.end()) {
        out.hasIcon = itCache->second.hasIcon;
        out.minU = itCache->second.minU;
        out.maxU = itCache->second.maxU;
        out.minV = itCache->second.minV;
        out.maxV = itCache->second.maxV;
        if (!itCache->second.finalName.empty()) {
            out.name = itCache->second.finalName;
        }
        env->PopLocalFrame(nullptr);
        return;
    }

    // Запрашиваем UV координаты спрайта предмета из атласа Minecraft напрямую через модель рендера
    if (mc) {
        std::string spFound;
        float vanillaMinU = 0.f, vanillaMaxU = 0.f, vanillaMinV = 0.f, vanillaMaxV = 0.f;
        bool vanillaOk = QueryItemSpriteUV(env, mc, itemStack, vanillaMinU, vanillaMaxU, vanillaMinV, vanillaMaxV, &spFound);

        std::string spLower = CleanAndLower(spFound);

        // Определяем подлинное имя предмета по найденному спрайту модели, если имя замаскировано ("sosihuy" или "hatchetIron")
        std::string effectiveName = out.name;
        std::string rawLowerName = CleanAndLower(out.name);
        bool needsTrueName = effectiveName.empty() || isMasked ||
                             rawLowerName.find("sosi") != std::string::npos ||
                             rawLowerName.find("huy") != std::string::npos ||
                             rawLowerName.find("hatchetiron") != std::string::npos;

        if (needsTrueName && !spLower.empty() && spLower != "missingno") {
            if (spLower.find("bone_club") != std::string::npos || spLower.find("club") != std::string::npos) effectiveName = "Bone Club";
            else if (spLower.find("knife_bone") != std::string::npos || spLower.find("bone_knife") != std::string::npos) effectiveName = "Bone Knife";
            else if (spLower.find("bone") != std::string::npos) effectiveName = "Bone";
            else if (spLower.find("wood") != std::string::npos || spLower.find("log") != std::string::npos) effectiveName = "Wood";
            else if (spLower.find("rock") != std::string::npos || spLower.find("stone") != std::string::npos) effectiveName = "Stone";
            else if (spLower.find("cloth") != std::string::npos) effectiveName = "Cloth";
            else if (spLower.find("meat") != std::string::npos) effectiveName = "Meat";
            else if (spLower.find("fat") != std::string::npos) effectiveName = "Fat";
            else if (spLower.find("headwrap") != std::string::npos) effectiveName = "Headwrap";
            else if (spLower.find("trousers") != std::string::npos || spLower.find("pants") != std::string::npos) effectiveName = "Pants";
            else if (spLower.find("shirt") != std::string::npos || spLower.find("jacket") != std::string::npos) effectiveName = "Shirt";
            else if (spLower.find("shoes") != std::string::npos || spLower.find("boots") != std::string::npos) effectiveName = "Shoes";
            else if (spLower.find("apple") != std::string::npos) effectiveName = "Apple";
            else if (spLower.find("berry") != std::string::npos) effectiveName = "Berry";
            else if (spLower.find("sulfur") != std::string::npos) effectiveName = "Sulfur";
            else if (spLower.find("metal") != std::string::npos) effectiveName = "Metal";
            else if (spLower.find("bow") != std::string::npos || spLower.find("hunting") != std::string::npos) effectiveName = "Bow";
            else if (spLower.find("spear") != std::string::npos) effectiveName = "Spear";
            else if (spLower.find("hatchet") != std::string::npos || spLower.find("axe") != std::string::npos) effectiveName = "Hatchet";
            else if (spLower.find("pickaxe") != std::string::npos) effectiveName = "Pickaxe";
            else if (spLower.find("sword") != std::string::npos || spLower.find("machete") != std::string::npos) effectiveName = "Sword";
            else if (spLower.find("gun") != std::string::npos || spLower.find("rifle") != std::string::npos || spLower.find("ak") != std::string::npos) effectiveName = "Rifle";

            auto itCustId = CUSTOM_ITEM_IDS.find(out.itemId);
            if (itCustId != CUSTOM_ITEM_IDS.end()) {
                effectiveName = itCustId->second;
            }

            if (!effectiveName.empty()) {
                out.name = effectiveName;
            }
        }

        // ── ОПРЕДЕЛЕНИЕ ИКОНКИ: РЕСУРС-ПАК ИЛИ ВАНИЛЛА ─────────────────────────────
        // 1. Live модель рендера Minecraft (QueryItemSpriteUV) - ПЕРВИЧНЫЙ источник!
        // В Minecraft (включая OptiFine CIT и любые текстур-паки) RenderItem возвращает:
        // - Модель и текстуру из РП (если применен ресурс-пак)
        // - Модель и текстуру ваниллы (если ресурс-пак выключен или не заменяет этот предмет)
        bool iconResolved = false;
        float rU1 = 0.f, rU2 = 0.f, rV1 = 0.f, rV2 = 0.f;

        // Серверы Rust часто передают кастомный предмет как обычную ванильную
        // основу (например diamond_pickaxe) с CustomID. В таком случае UV
        // модели указывает на основу, а не на иконку ресурспака.
        const bool isServerCustomItem = CUSTOM_ITEM_IDS.find(out.itemId) != CUSTOM_ITEM_IDS.end();
        if (isServerCustomItem && QueryResourcePackSprite(env, mc, out.itemId, out.name, rU1, rU2, rV1, rV2)) {
            out.minU = rU1; out.maxU = rU2;
            out.minV = rV1; out.maxV = rV2;
            out.hasIcon = true;
            iconResolved = true;
        }

        if (!iconResolved && vanillaOk && (vanillaMaxU > vanillaMinU) && (vanillaMaxV > vanillaMinV) && spLower != "missingno") {
            out.minU = vanillaMinU; out.maxU = vanillaMaxU;
            out.minV = vanillaMinV; out.maxV = vanillaMaxV;
            out.hasIcon = true;
            iconResolved = true;
        }

        // 2. Фоллбэк: поиск кастомной текстуры в ресурс-паке по имени
        if (!iconResolved) {
            if (QueryResourcePackSprite(env, mc, out.itemId, out.name, rU1, rU2, rV1, rV2)) {
                out.minU = rU1; out.maxU = rU2;
                out.minV = rV1; out.maxV = rV2;
                out.hasIcon = true;
                iconResolved = true;
            }
        }

        // 3. Фоллбэк: ищем в атласе по имени ванильного предмета или itemId (картофель, хлеб, броня, инструменты и т.д.)
        if (!iconResolved) {
            const char* namedSprite = GetGeneralItemSpriteName(out.itemId, out.name);
            if (!namedSprite || namedSprite[0] == '\0') {
                namedSprite = GetVanillaItemSpriteNameById(out.itemId);
            }
            if (namedSprite && namedSprite[0] != '\0') {
                if (QuerySpriteByName(env, mc, namedSprite, rU1, rU2, rV1, rV2)) {
                    out.minU = rU1; out.maxU = rU2;
                    out.minV = rV1; out.maxV = rV2;
                    out.hasIcon = true;
                    iconResolved = true;
                }
            }
        }

        if (!iconResolved) {
            out.hasIcon = false;
        }
    } else {
        out.hasIcon = false;
    }

    if (out.hasIcon) {
        CachedItemInfo ci;
        ci.hasIcon = true;
        ci.minU = out.minU; ci.maxU = out.maxU;
        ci.minV = out.minV; ci.maxV = out.maxV;
        ci.finalName = out.name;
        s_itemParsedCache[cacheKey] = ci;
    }

    env->PopLocalFrame(nullptr);
}

void JVMBridge::QueryPlayerEquipment(JNIEnv* env) {
    jobject mc = GetMinecraftInstance(env);
    if (!mc) return;

    jclass cMC = env->GetObjectClass(mc);

    // bib.h -> bud (EntityPlayerSP)
    jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
    if (!fPlayer) {
        env->ExceptionClear();
        fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    }

    if (!fPlayer) {
        env->ExceptionClear();
        env->DeleteLocalRef(cMC);
        env->DeleteLocalRef(mc);
        for (int i = 0; i < 5; ++i) m_data.equipment[i].isEmpty = true;
        return;
    }

    jobject player = env->GetObjectField(mc, fPlayer);
    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);

    if (!player || env->ExceptionCheck()) {
        env->ExceptionClear();
        for (int i = 0; i < 5; ++i) m_data.equipment[i].isEmpty = true;
        for (int i = 0; i < 9; ++i) m_data.hotbar[i].isEmpty = true;
        m_data.offHandItem.isEmpty = true;
        return;
    }

    jclass cPlayer = env->GetObjectClass(player);

    // Опыт игрока (aed.bB float experience, aed.bD int experienceLevel)
    jfieldID fExp = env->GetFieldID(cPlayer, "bB", "F");
    if (!fExp) { env->ExceptionClear(); fExp = env->GetFieldID(cPlayer, "experience", "F"); }
    if (fExp) m_data.experience = env->GetFloatField(player, fExp);

    jfieldID fExpLvl = env->GetFieldID(cPlayer, "bD", "I");
    if (!fExpLvl) { env->ExceptionClear(); fExpLvl = env->GetFieldID(cPlayer, "experienceLevel", "I"); }
    if (fExpLvl) m_data.experienceLevel = env->GetIntField(player, fExpLvl);

    // Ищем inventory (aed.bv Laec;) через обход иерархии суперклассов bud -> bua -> aed
    jfieldID fInv = nullptr;
    jclass curCls = cPlayer;
    while (curCls && !fInv) {
        fInv = env->GetFieldID(curCls, "bv", "Laec;");
        if (!fInv) {
            env->ExceptionClear();
            fInv = env->GetFieldID(curCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
        }
        if (!fInv) {
            env->ExceptionClear();
            jclass superCls = env->GetSuperclass(curCls);
            if (curCls != cPlayer) env->DeleteLocalRef(curCls);
            curCls = superCls;
        }
    }
    if (curCls && curCls != cPlayer) env->DeleteLocalRef(curCls);

    if (!fInv) {
        env->ExceptionClear();
        env->DeleteLocalRef(cPlayer);
        env->DeleteLocalRef(player);
        for (int i = 0; i < 5; ++i) m_data.equipment[i].isEmpty = true;
        for (int i = 0; i < 9; ++i) m_data.hotbar[i].isEmpty = true;
        m_data.offHandItem.isEmpty = true;
        return;
    }

    jobject inv = env->GetObjectField(player, fInv);
    env->DeleteLocalRef(cPlayer);
    env->DeleteLocalRef(player);

    if (!inv || env->ExceptionCheck()) {
        env->ExceptionClear();
        for (int i = 0; i < 5; ++i) m_data.equipment[i].isEmpty = true;
        for (int i = 0; i < 9; ++i) m_data.hotbar[i].isEmpty = true;
        m_data.offHandItem.isEmpty = true;
        return;
    }

    jclass cInv = env->GetObjectClass(inv);

    // aec.b -> fi<aip> (armorInventory: 0=boots, 1=legs, 2=chest, 3=helmet)
    jfieldID fArmor = env->GetFieldID(cInv, "b", "Lfi;");
    if (!fArmor) {
        env->ExceptionClear();
        fArmor = env->GetFieldID(cInv, "armorInventory", "Lnet/minecraft/util/NonNullList;");
    }

    // Выбранный слот хотбара (currentItem / field_70461_c / d: 0..8)
    jfieldID fCur = env->GetFieldID(cInv, "d", "I");
    if (!fCur) { env->ExceptionClear(); fCur = env->GetFieldID(cInv, "currentItem", "I"); }
    if (!fCur) { env->ExceptionClear(); fCur = env->GetFieldID(cInv, "field_70461_c", "I"); }
    if (fCur) {
        m_data.currentItem = env->GetIntField(inv, fCur);
    }

    // aec.i() -> aip (getCurrentItem() - предмет в руке)
    jmethodID mHand = env->GetMethodID(cInv, "i", "()Laip;");
    if (!mHand) {
        env->ExceptionClear();
        mHand = env->GetMethodID(cInv, "getCurrentItem", "()Lnet/minecraft/item/ItemStack;");
    }

    jclass cList = env->FindClass("java/util/List");
    jmethodID mGetItem = cList ? env->GetMethodID(cList, "get", "(I)Ljava/lang/Object;") : nullptr;

    jobject mcInst = GetMinecraftInstance(env);

    // Броня: 0=Шлем, 1=Нагрудник, 2=Поножи, 3=Ботинки
    if (fArmor && mGetItem) {
        jobject armorList = env->GetObjectField(inv, fArmor);
        if (armorList && !env->ExceptionCheck()) {
            for (int i = 0; i < 4; ++i) {
                int armorIdx = 3 - i; // 3=шлем, 2=нагрудник, 1=поножи, 0=ботинки
                jobject is = env->CallObjectMethod(armorList, mGetItem, armorIdx);
                ParseItemStackHelper(env, mcInst, is, m_data.equipment[i]);
                if (is) env->DeleteLocalRef(is);

                // Запасной вариант для надетой брони (если иконка не определилась или слетела)
                if (!m_data.equipment[i].isEmpty && !m_data.equipment[i].hasIcon) {
                    const char* fbSprite = nullptr;
                    if (i == 0) fbSprite = "minecraft:items/leather_helmet";
                    else if (i == 1) fbSprite = "minecraft:items/leather_chestplate";
                    else if (i == 2) fbSprite = "minecraft:items/leather_leggings";
                    else if (i == 3) fbSprite = "minecraft:items/leather_boots";
                    if (fbSprite) {
                        m_data.equipment[i].hasIcon = QuerySpriteByName(env, mcInst, fbSprite,
                            m_data.equipment[i].minU, m_data.equipment[i].maxU,
                            m_data.equipment[i].minV, m_data.equipment[i].maxV);
                    }
                }
            }
            env->DeleteLocalRef(armorList);
        } else {
            env->ExceptionClear();
            for (int i = 0; i < 4; ++i) m_data.equipment[i].isEmpty = true;
        }
    } else {
        for (int i = 0; i < 4; ++i) m_data.equipment[i].isEmpty = true;
    }

    // Предмет в руке: equipment[4]
    if (mHand) {
        jobject handItem = env->CallObjectMethod(inv, mHand);
        ParseItemStackHelper(env, mcInst, handItem, m_data.equipment[4]);
        if (handItem) env->DeleteLocalRef(handItem);
    } else {
        m_data.equipment[4].isEmpty = true;
    }

    // 9 слотов хотбара + 27 слотов инвентаря: aec.a (fi<aip> mainInventory, слоты 0..35)
    jfieldID fMain = env->GetFieldID(cInv, "a", "Lfi;");
    if (!fMain) { env->ExceptionClear(); fMain = env->GetFieldID(cInv, "mainInventory", "Lnet/minecraft/util/NonNullList;"); }
    if (fMain && mGetItem) {
        jobject mainList = env->GetObjectField(inv, fMain);
        if (mainList && !env->ExceptionCheck()) {
            for (int i = 0; i < 9; ++i) {
                jobject is = env->CallObjectMethod(mainList, mGetItem, i);
                ParseItemStackHelper(env, mcInst, is, m_data.hotbar[i]);
                if (is) env->DeleteLocalRef(is);
            }
            for (int i = 0; i < 27; ++i) {
                jobject is = env->CallObjectMethod(mainList, mGetItem, 9 + i);
                ParseItemStackHelper(env, mcInst, is, m_data.mainInventory[i]);
                if (is) env->DeleteLocalRef(is);
            }
            env->DeleteLocalRef(mainList);
        } else {
            env->ExceptionClear();
            for (int i = 0; i < 9; ++i) m_data.hotbar[i].isEmpty = true;
            for (int i = 0; i < 27; ++i) m_data.mainInventory[i].isEmpty = true;
        }
    } else {
        for (int i = 0; i < 9; ++i) m_data.hotbar[i].isEmpty = true;
        for (int i = 0; i < 27; ++i) m_data.mainInventory[i].isEmpty = true;
    }

    // Предмет во второй руке: aec.c (fi<aip> offHandInventory, слот 0)
    jfieldID fOff = env->GetFieldID(cInv, "c", "Lfi;");
    if (!fOff) { env->ExceptionClear(); fOff = env->GetFieldID(cInv, "offHandInventory", "Lnet/minecraft/util/NonNullList;"); }
    if (fOff && mGetItem) {
        jobject offList = env->GetObjectField(inv, fOff);
        if (offList && !env->ExceptionCheck()) {
            jobject is = env->CallObjectMethod(offList, mGetItem, 0);
            ParseItemStackHelper(env, mcInst, is, m_data.offHandItem);
            if (is) env->DeleteLocalRef(is);
            env->DeleteLocalRef(offList);
        } else {
            env->ExceptionClear();
            m_data.offHandItem.isEmpty = true;
        }
    } else {
        m_data.offHandItem.isEmpty = true;
    }

    if (mcInst) env->DeleteLocalRef(mcInst);

    if (cList) env->DeleteLocalRef(cList);
    env->DeleteLocalRef(cInv);
    env->DeleteLocalRef(inv);
}

void JVMBridge::ClearExceptions() {
    JNIEnv* env = GetJNIEnv();
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void JVMBridge::Tick() {
    if (!m_ok) return;

    JNIEnv* env = GetJNIEnv();
    if (!env) return;

    if (env->ExceptionCheck()) env->ExceptionClear();

    if (!m_refsCached) {
        CacheRefs(env);
    }

    // Проверяем, находится ли игрок в мире (world != null && player != null)
    jobject mc = GetMinecraftInstance(env);
    bool inWorld = false;
    if (mc) {
        jclass cMC = env->GetObjectClass(mc);
        jfieldID fWorld = env->GetFieldID(cMC, "f", "Lbsb;");
        if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "world", "Lnet/minecraft/client/multiplayer/WorldClient;"); }
        if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;"); }

        jfieldID fPl = env->GetFieldID(cMC, "h", "Lbud;");
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }

        jobject wObj = fWorld ? env->GetObjectField(mc, fWorld) : nullptr;
        jobject pObj = fPl ? env->GetObjectField(mc, fPl) : nullptr;

        if (wObj && pObj) {
            inWorld = true;
        }

        if (wObj) env->DeleteLocalRef(wObj);
        if (pObj) env->DeleteLocalRef(pObj);
        env->DeleteLocalRef(cMC);
        env->DeleteLocalRef(mc);
    }

    m_data.isInWorld = inWorld;
    if (!inWorld) {
        m_data.players.clear();
        m_data.droppedItems.clear();
        m_data.isChatOpen = false;
        m_data.tabPlayers.clear();
        m_offlineTabNames.clear();
        m_knownPlayerNamesByEntId.clear();
        m_knownPlayerNamesByUUID.clear();
        m_playerTrackers.clear();
        m_lastTabParseTime = 0;
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }

    DWORD nowMs = GetTickCount();

    // 1. Обновляем матрицы рендера камеры каждый кадр для WorldToScreen (сверхбыстрый прямой доступ)
    UpdateRenderMatrices(env);

    // 2. Сканирование сущностей (игроки, предметы на полу) — опрашиваем с частотой 20–25 Гц (каждые 45 мс)
    // Строго по таймеру, чтобы не вызывать тысячи JNI вызовов за кадр при пустом списке игроков
    static DWORD s_lastEntityScanTime = 0;
    if (nowMs - s_lastEntityScanTime >= 45) {
        s_lastEntityScanTime = nowMs;
        QueryWorldEntities(env);
    }

    // 3. Экипировка игрока — опрашиваем с частотой ~7 Гц (каждые 150 мс)
    static DWORD s_lastEquipScanTime = 0;
    if (nowMs - s_lastEquipScanTime >= 150) {
        s_lastEquipScanTime = nowMs;
        QueryPlayerEquipment(env);
    }

    // 4. GUI Scale, ChatOpen, TitleActive и FPS — опрашиваем раз в 50 мс
    static DWORD s_lastSettingsTime = 0;
    if (nowMs - s_lastSettingsTime >= 50) {
        s_lastSettingsTime = nowMs;
        m_data.fps = QueryFPS(env);
        m_data.isChatOpen = QueryIsChatOpen(env);
        m_data.isTitleActive = QueryIsTitleActive(env);
        m_data.guiScale = QueryGuiScale(env);
    }

    // 5. RustEx Bypass: обновляем таблицу offline UUID -> реальный ник каждые 500мс
    if (m_offlineTabNames.empty() || (nowMs - m_lastTabParseTime >= 500)) {
        m_lastTabParseTime = nowMs;
        UpdateOfflineTabNames(env);
    }

    // 6. Забираем результаты BlockESP из всех параллельных потоков воркеров
    if (m_blockEspMutex.try_lock()) {
        m_data.espBlocks.clear();
        for (unsigned int i = 0; i < m_numWorkers && i < 16; ++i) {
            m_data.espBlocks.insert(m_data.espBlocks.end(), m_workerBlocks[i].begin(), m_workerBlocks[i].end());
        }
        m_blockEspMutex.unlock();
    }

    // 7. Никнейм и Ping — опрашиваем каждые 300мс
    static DWORD s_lastPingTime = 0;
    if (m_data.username.empty() || m_data.username == "Unknown" || (nowMs - s_lastPingTime >= 300)) {
        s_lastPingTime = nowMs;
        std::string uname = QueryUsername(env);
        if (!uname.empty() && uname != "Unknown") {
            m_data.username = uname;
        }
        m_data.ping = QueryPing(env);
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

// ── Обновление матриц камеры Minecraft для WorldToScreen ─────────────────────
static float s_lastEyeHeight = 1.62f;

void JVMBridge::UpdateRenderMatrices(JNIEnv* env) {
    if (!env) env = GetJNIEnv();
    if (!env || !m_ok || !m_cMC) return;
    if (env->ExceptionCheck()) env->ExceptionClear();

    // Мгновенное определение наличия открытого экрана (инвентарь, сундук с картой, меню) каждый кадр
    static jfieldID s_fCurrentScreen = nullptr;
    static bool s_screenResolved = false;
    jobject mcInst = GetMinecraftInstance(env);
    if (mcInst) {
        if (!s_screenResolved) {
            s_screenResolved = true;
            jclass cMC = env->GetObjectClass(mcInst);
            if (cMC) {
                const char* screenFields[] = { "m", "currentScreen", "field_71462_r" };
                for (const char* sf : screenFields) {
                    s_fCurrentScreen = env->GetFieldID(cMC, sf, "Lblk;");
                    if (!s_fCurrentScreen) { env->ExceptionClear(); s_fCurrentScreen = env->GetFieldID(cMC, sf, "Lnet/minecraft/client/gui/GuiScreen;"); }
                    if (s_fCurrentScreen) break;
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(cMC);
            }
        }
        if (s_fCurrentScreen) {
            jobject scr = env->GetObjectField(mcInst, s_fCurrentScreen);
            m_data.hasScreen = (scr != nullptr);
            if (scr) env->DeleteLocalRef(scr);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        env->DeleteLocalRef(mcInst);
    }

    static jclass s_cBHV = nullptr;
    static jfieldID s_fMV = nullptr;
    static jfieldID s_fProj = nullptr;
    static jfieldID s_fVP = nullptr;
    static bool s_bhvResolved = false;

    if (!s_bhvResolved) {
        s_bhvResolved = true;
        jobject cl = GetMinecraftClassLoader(env);
        jclass cBHV = FindMCClass(env, cl, "bhv");
        if (!cBHV) cBHV = FindMCClass(env, cl, "net/minecraft/client/renderer/ActiveRenderInfo");
        if (cBHV) {
            s_cBHV = reinterpret_cast<jclass>(env->NewGlobalRef(cBHV));
            s_fMV = env->GetStaticFieldID(cBHV, "b", "Ljava/nio/FloatBuffer;");
            if (!s_fMV) { env->ExceptionClear(); s_fMV = env->GetStaticFieldID(cBHV, "MODELVIEW", "Ljava/nio/FloatBuffer;"); }
            if (!s_fMV) { env->ExceptionClear(); s_fMV = env->GetStaticFieldID(cBHV, "field_178812_b", "Ljava/nio/FloatBuffer;"); }

            s_fProj = env->GetStaticFieldID(cBHV, "c", "Ljava/nio/FloatBuffer;");
            if (!s_fProj) { env->ExceptionClear(); s_fProj = env->GetStaticFieldID(cBHV, "PROJECTION", "Ljava/nio/FloatBuffer;"); }
            if (!s_fProj) { env->ExceptionClear(); s_fProj = env->GetStaticFieldID(cBHV, "field_178813_c", "Ljava/nio/FloatBuffer;"); }

            s_fVP = env->GetStaticFieldID(cBHV, "a", "Ljava/nio/IntBuffer;");
            if (!s_fVP) { env->ExceptionClear(); s_fVP = env->GetStaticFieldID(cBHV, "VIEWPORT", "Ljava/nio/IntBuffer;"); }
            if (!s_fVP) { env->ExceptionClear(); s_fVP = env->GetStaticFieldID(cBHV, "field_178814_a", "Ljava/nio/IntBuffer;"); }
            env->DeleteLocalRef(cBHV);
        }
    }

    if (s_cBHV && s_fMV && s_fProj && s_fVP) {
        jobject bufMV = env->GetStaticObjectField(s_cBHV, s_fMV);
        jobject bufProj = env->GetStaticObjectField(s_cBHV, s_fProj);
        jobject bufVP = env->GetStaticObjectField(s_cBHV, s_fVP);

        if (bufMV && bufProj && bufVP) {
            float* pMV = reinterpret_cast<float*>(env->GetDirectBufferAddress(bufMV));
            float* pProj = reinterpret_cast<float*>(env->GetDirectBufferAddress(bufProj));
            int* pVP = reinterpret_cast<int*>(env->GetDirectBufferAddress(bufVP));

            if (pMV && pProj && pVP) {
                memcpy(m_modelView, pMV, 16 * sizeof(float));
                memcpy(m_projection, pProj, 16 * sizeof(float));
                memcpy(m_viewport, pVP, 4 * sizeof(int));
                m_hasMatrices = true;
            } else {
                jclass cFB = env->GetObjectClass(bufMV);
                jmethodID mGetF = env->GetMethodID(cFB, "get", "(I)F");
                jclass cIB = env->GetObjectClass(bufVP);
                jmethodID mGetI = env->GetMethodID(cIB, "get", "(I)I");
                if (mGetF && mGetI) {
                    for (int i = 0; i < 16; ++i) {
                        m_modelView[i] = env->CallFloatMethod(bufMV, mGetF, i);
                        m_projection[i] = env->CallFloatMethod(bufProj, mGetF, i);
                    }
                    for (int i = 0; i < 4; ++i) {
                        m_viewport[i] = env->CallIntMethod(bufVP, mGetI, i);
                    }
                    m_hasMatrices = true;
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(cFB);
                env->DeleteLocalRef(cIB);
            }
        }

        if (bufMV) env->DeleteLocalRef(bufMV);
        if (bufProj) env->DeleteLocalRef(bufProj);
        if (bufVP) env->DeleteLocalRef(bufVP);
    }

    // Позиция камеры: берем точные координаты из RenderManager (bzf в 1.12.2) или ActiveRenderInfo (bhv.e)
    bool gotCam = false;

    static jfieldID s_fRenderManager = nullptr;
    static jmethodID s_mGetRenderManager = nullptr;
    static jfieldID s_fRM_viewerPosX = nullptr;
    static jfieldID s_fRM_viewerPosY = nullptr;
    static jfieldID s_fRM_viewerPosZ = nullptr;
    static jfieldID s_fRM_playerViewX = nullptr;
    static jfieldID s_fRM_playerViewY = nullptr;
    static bool s_rmInit = false;
    double rmViewerPosY = 0.0;

    jobject mc = GetMinecraftInstance(env);
    if (mc && !env->ExceptionCheck()) {
        jclass cMC = env->GetObjectClass(mc);
        if (!s_rmInit) {
            s_rmInit = true;
            jobject cl = GetMinecraftClassLoader(env);
            jclass cRM = FindMCClass(env, cl, "bzf");
            if (!cRM) cRM = FindMCClass(env, cl, "net/minecraft/client/renderer/entity/RenderManager");
            if (cRM) {
                s_fRM_viewerPosX = env->GetFieldID(cRM, "h", "D");
                if (!s_fRM_viewerPosX) { env->ExceptionClear(); s_fRM_viewerPosX = env->GetFieldID(cRM, "viewerPosX", "D"); }
                if (!s_fRM_viewerPosX) { env->ExceptionClear(); s_fRM_viewerPosX = env->GetFieldID(cRM, "field_78725_b", "D"); }

                s_fRM_viewerPosY = env->GetFieldID(cRM, "i", "D");
                if (!s_fRM_viewerPosY) { env->ExceptionClear(); s_fRM_viewerPosY = env->GetFieldID(cRM, "viewerPosY", "D"); }
                if (!s_fRM_viewerPosY) { env->ExceptionClear(); s_fRM_viewerPosY = env->GetFieldID(cRM, "field_78726_c", "D"); }

                s_fRM_viewerPosZ = env->GetFieldID(cRM, "j", "D");
                if (!s_fRM_viewerPosZ) { env->ExceptionClear(); s_fRM_viewerPosZ = env->GetFieldID(cRM, "viewerPosZ", "D"); }
                if (!s_fRM_viewerPosZ) { env->ExceptionClear(); s_fRM_viewerPosZ = env->GetFieldID(cRM, "field_78723_d", "D"); }

                s_fRM_playerViewX = env->GetFieldID(cRM, "e", "F");
                if (!s_fRM_playerViewX) { env->ExceptionClear(); s_fRM_playerViewX = env->GetFieldID(cRM, "playerViewX", "F"); }
                if (!s_fRM_playerViewX) { env->ExceptionClear(); s_fRM_playerViewX = env->GetFieldID(cRM, "field_78732_a", "F"); }

                s_fRM_playerViewY = env->GetFieldID(cRM, "f", "F");
                if (!s_fRM_playerViewY) { env->ExceptionClear(); s_fRM_playerViewY = env->GetFieldID(cRM, "playerViewY", "F"); }
                if (!s_fRM_playerViewY) { env->ExceptionClear(); s_fRM_playerViewY = env->GetFieldID(cRM, "field_78735_i", "F"); }
                env->DeleteLocalRef(cRM);
            }

            s_fRenderManager = env->GetFieldID(cMC, "aa", "Lbzf;");
            if (!s_fRenderManager) { env->ExceptionClear(); s_fRenderManager = env->GetFieldID(cMC, "renderManager", "Lnet/minecraft/client/renderer/entity/RenderManager;"); }
            if (!s_fRenderManager) { env->ExceptionClear(); s_fRenderManager = env->GetFieldID(cMC, "field_175616_W", "Lnet/minecraft/client/renderer/entity/RenderManager;"); }

            s_mGetRenderManager = env->GetMethodID(cMC, "ac", "()Lbzf;");
            if (!s_mGetRenderManager) { env->ExceptionClear(); s_mGetRenderManager = env->GetMethodID(cMC, "getRenderManager", "()Lnet/minecraft/client/renderer/entity/RenderManager;"); }
            if (!s_mGetRenderManager) { env->ExceptionClear(); s_mGetRenderManager = env->GetMethodID(cMC, "func_175598_ae", "()Lnet/minecraft/client/renderer/entity/RenderManager;"); }
        }

        jobject rmObj = nullptr;
        if (s_fRenderManager) {
            rmObj = env->GetObjectField(mc, s_fRenderManager);
            if (env->ExceptionCheck()) { env->ExceptionClear(); rmObj = nullptr; }
        }
        if (!rmObj && s_mGetRenderManager) {
            rmObj = env->CallObjectMethod(mc, s_mGetRenderManager);
            if (env->ExceptionCheck()) { env->ExceptionClear(); rmObj = nullptr; }
        }

        if (rmObj) {
            if (s_fRM_viewerPosX && s_fRM_viewerPosY && s_fRM_viewerPosZ) {
                double rx = env->GetDoubleField(rmObj, s_fRM_viewerPosX);
                double ry = env->GetDoubleField(rmObj, s_fRM_viewerPosY);
                double rz = env->GetDoubleField(rmObj, s_fRM_viewerPosZ);
                if (rx != 0.0 || ry != 0.0 || rz != 0.0) {
                    rmViewerPosY = ry;
                    m_data.camPos.x = rx;
                    m_data.camPos.y = ry; // Матрица OpenGL MODELVIEW в 1.12.2 уже содержит смещение камеры по высоте глаз
                    m_data.camPos.z = rz;
                    m_camPos = m_data.camPos;
                    if (s_fRM_playerViewX && s_fRM_playerViewY) {
                        // В RenderManager 1.12.2: playerViewY = rotationYaw, playerViewX = rotationPitch!
                        m_data.camYaw   = env->GetFloatField(rmObj, s_fRM_playerViewY);
                        m_data.camPitch = env->GetFloatField(rmObj, s_fRM_playerViewX);
                    }
                    gotCam = true;
                }
            }
            env->DeleteLocalRef(rmObj);
        }

        // Fallback №2: читаем ActiveRenderInfo.position (bhv.e) если RenderManager вернул нули
        if (!gotCam && s_cBHV) {
            static jfieldID s_fBHV_e = env->GetStaticFieldID(s_cBHV, "e", "Lbhe;");
            if (!s_fBHV_e) { env->ExceptionClear(); s_fBHV_e = env->GetStaticFieldID(s_cBHV, "position", "Lnet/minecraft/util/math/Vec3d;"); }
            if (!s_fBHV_e) { env->ExceptionClear(); s_fBHV_e = env->GetStaticFieldID(s_cBHV, "field_178811_e", "Lnet/minecraft/util/math/Vec3d;"); }
            if (s_fBHV_e) {
                jobject vecObj = env->GetStaticObjectField(s_cBHV, s_fBHV_e);
                if (vecObj && !env->ExceptionCheck()) {
                    jclass cVec = env->GetObjectClass(vecObj);
                    jfieldID fVX = env->GetFieldID(cVec, "b", "D");
                    if (!fVX) { env->ExceptionClear(); fVX = env->GetFieldID(cVec, "x", "D"); }
                    jfieldID fVY = env->GetFieldID(cVec, "c", "D");
                    if (!fVY) { env->ExceptionClear(); fVY = env->GetFieldID(cVec, "y", "D"); }
                    jfieldID fVZ = env->GetFieldID(cVec, "d", "D");
                    if (!fVZ) { env->ExceptionClear(); fVZ = env->GetFieldID(cVec, "z", "D"); }
                    if (fVX && fVY && fVZ) {
                        double vx = env->GetDoubleField(vecObj, fVX);
                        double vy = env->GetDoubleField(vecObj, fVY);
                        double vz = env->GetDoubleField(vecObj, fVZ);
                        if (vx != 0.0 || vy != 0.0 || vz != 0.0) {
                            m_data.camPos.x = vx;
                            m_data.camPos.y = vy;
                            m_data.camPos.z = vz;
                            m_camPos = m_data.camPos;
                            gotCam = true;
                        }
                    }
                    env->DeleteLocalRef(cVec);
                    env->DeleteLocalRef(vecObj);
                } else env->ExceptionClear();
            }
        }

        // Fallback №3: localPlayer + Timer
        jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        jobject localPlayer = fPlayer ? env->GetObjectField(mc, fPlayer) : nullptr;

        // partialTicks из mc.timer (bih.c в 1.12.2)
        float pt = 1.0f;
        const char* timerFields[] = { "Y", "timer", "field_71428_T", "aj" };
        for (const char* tf : timerFields) {
            jfieldID fTimer = env->GetFieldID(cMC, tf, "Lbih;");
            if (!fTimer) { env->ExceptionClear(); fTimer = env->GetFieldID(cMC, tf, "Lbix;"); }
            if (!fTimer) { env->ExceptionClear(); fTimer = env->GetFieldID(cMC, tf, "Lnet/minecraft/util/Timer;"); }
            if (fTimer) {
                jobject timerObj = env->GetObjectField(mc, fTimer);
                if (timerObj && !env->ExceptionCheck()) {
                    jclass cTimer = env->GetObjectClass(timerObj);
                    jfieldID fPT = env->GetFieldID(cTimer, "c", "F");
                    if (!fPT) { env->ExceptionClear(); fPT = env->GetFieldID(cTimer, "renderPartialTicks", "F"); }
                    if (!fPT) { env->ExceptionClear(); fPT = env->GetFieldID(cTimer, "field_194147_b", "F"); }
                    if (fPT) pt = env->GetFloatField(timerObj, fPT);
                    env->DeleteLocalRef(cTimer);
                    env->DeleteLocalRef(timerObj);
                    break;
                }
                env->ExceptionClear();
            }
        }

        // ── Обновление позиции игрока напрямую из localPlayer и partialTicks ──
        if (localPlayer) {
            jobject cl = GetMinecraftClassLoader(env);
            jclass cEntity = FindMCClass(env, cl, "vg");
            if (!cEntity) cEntity = FindMCClass(env, cl, "net/minecraft/entity/Entity");
            if (cEntity) {
                jfieldID fPosX = env->GetFieldID(cEntity, "p", "D");
                if (!fPosX) { env->ExceptionClear(); fPosX = env->GetFieldID(cEntity, "posX", "D"); }
                if (!fPosX) { env->ExceptionClear(); fPosX = env->GetFieldID(cEntity, "field_70165_t", "D"); }

                jfieldID fPosY = env->GetFieldID(cEntity, "q", "D");
                if (!fPosY) { env->ExceptionClear(); fPosY = env->GetFieldID(cEntity, "posY", "D"); }
                if (!fPosY) { env->ExceptionClear(); fPosY = env->GetFieldID(cEntity, "field_70163_u", "D"); }

                jfieldID fPosZ = env->GetFieldID(cEntity, "r", "D");
                if (!fPosZ) { env->ExceptionClear(); fPosZ = env->GetFieldID(cEntity, "posZ", "D"); }
                if (!fPosZ) { env->ExceptionClear(); fPosZ = env->GetFieldID(cEntity, "field_70161_v", "D"); }

                jfieldID fLastX = env->GetFieldID(cEntity, "m", "D");
                if (!fLastX) { env->ExceptionClear(); fLastX = env->GetFieldID(cEntity, "prevPosX", "D"); }
                if (!fLastX) { env->ExceptionClear(); fLastX = env->GetFieldID(cEntity, "field_70169_q", "D"); }
                if (!fLastX) { env->ExceptionClear(); fLastX = env->GetFieldID(cEntity, "M", "D"); }
                if (!fLastX) { env->ExceptionClear(); fLastX = env->GetFieldID(cEntity, "lastTickPosX", "D"); }
                if (!fLastX) { env->ExceptionClear(); fLastX = env->GetFieldID(cEntity, "field_70142_S", "D"); }

                jfieldID fLastY = env->GetFieldID(cEntity, "n", "D");
                if (!fLastY) { env->ExceptionClear(); fLastY = env->GetFieldID(cEntity, "prevPosY", "D"); }
                if (!fLastY) { env->ExceptionClear(); fLastY = env->GetFieldID(cEntity, "field_70167_r", "D"); }
                if (!fLastY) { env->ExceptionClear(); fLastY = env->GetFieldID(cEntity, "N", "D"); }
                if (!fLastY) { env->ExceptionClear(); fLastY = env->GetFieldID(cEntity, "lastTickPosY", "D"); }
                if (!fLastY) { env->ExceptionClear(); fLastY = env->GetFieldID(cEntity, "field_70137_T", "D"); }

                jfieldID fLastZ = env->GetFieldID(cEntity, "o", "D");
                if (!fLastZ) { env->ExceptionClear(); fLastZ = env->GetFieldID(cEntity, "prevPosZ", "D"); }
                if (!fLastZ) { env->ExceptionClear(); fLastZ = env->GetFieldID(cEntity, "field_70166_s", "D"); }
                if (!fLastZ) { env->ExceptionClear(); fLastZ = env->GetFieldID(cEntity, "O", "D"); }
                if (!fLastZ) { env->ExceptionClear(); fLastZ = env->GetFieldID(cEntity, "lastTickPosZ", "D"); }
                if (!fLastZ) { env->ExceptionClear(); fLastZ = env->GetFieldID(cEntity, "field_70136_U", "D"); }

                jmethodID mEyeH = env->GetMethodID(cEntity, "by", "()F");
                if (!mEyeH) { env->ExceptionClear(); mEyeH = env->GetMethodID(cEntity, "getEyeHeight", "()F"); }
                if (!mEyeH) { env->ExceptionClear(); mEyeH = env->GetMethodID(cEntity, "func_70047_e", "()F"); }

                jfieldID fYaw = env->GetFieldID(cEntity, "v", "F");
                if (!fYaw) { env->ExceptionClear(); fYaw = env->GetFieldID(cEntity, "rotationYaw", "F"); }
                if (!fYaw) { env->ExceptionClear(); fYaw = env->GetFieldID(cEntity, "field_70177_z", "F"); }

                jfieldID fPitch = env->GetFieldID(cEntity, "w", "F");
                if (!fPitch) { env->ExceptionClear(); fPitch = env->GetFieldID(cEntity, "rotationPitch", "F"); }
                if (!fPitch) { env->ExceptionClear(); fPitch = env->GetFieldID(cEntity, "field_70125_A", "F"); }

                jfieldID fPrevYaw = env->GetFieldID(cEntity, "x", "F");
                if (!fPrevYaw) { env->ExceptionClear(); fPrevYaw = env->GetFieldID(cEntity, "prevRotationYaw", "F"); }
                if (!fPrevYaw) { env->ExceptionClear(); fPrevYaw = env->GetFieldID(cEntity, "field_70126_B", "F"); }

                jfieldID fPrevPitch = env->GetFieldID(cEntity, "y", "F");
                if (!fPrevPitch) { env->ExceptionClear(); fPrevPitch = env->GetFieldID(cEntity, "prevRotationPitch", "F"); }
                if (!fPrevPitch) { env->ExceptionClear(); fPrevPitch = env->GetFieldID(cEntity, "field_70127_C", "F"); }

                if (fPosX && fPosY && fPosZ) {
                    double myX = env->GetDoubleField(localPlayer, fPosX);
                    double myY = env->GetDoubleField(localPlayer, fPosY);
                    double myZ = env->GetDoubleField(localPlayer, fPosZ);
                    double myLastX = fLastX ? env->GetDoubleField(localPlayer, fLastX) : myX;
                    double myLastY = fLastY ? env->GetDoubleField(localPlayer, fLastY) : myY;
                    double myLastZ = fLastZ ? env->GetDoubleField(localPlayer, fLastZ) : myZ;

                    double myInterpX = myLastX + (myX - myLastX) * pt;
                    double myInterpY = myLastY + (myY - myLastY) * pt;
                    double myInterpZ = myLastZ + (myZ - myLastZ) * pt;

                    float eyeH = mEyeH ? env->CallFloatMethod(localPlayer, mEyeH) : 1.62f;
                    if (eyeH < 0.1f || eyeH > 3.0f) eyeH = 1.62f;
                    s_lastEyeHeight = eyeH;

                    m_data.playerPos = { myInterpX, myInterpY, myInterpZ };
                    if (!gotCam) {
                        m_data.camPos    = { myInterpX, myInterpY + eyeH, myInterpZ };
                        m_camPos         = m_data.camPos;

                        if (fYaw && fPitch) {
                            float curY = env->GetFloatField(localPlayer, fYaw);
                            float curP = env->GetFloatField(localPlayer, fPitch);
                            float prevY = fPrevYaw ? env->GetFloatField(localPlayer, fPrevYaw) : curY;
                            float prevP = fPrevPitch ? env->GetFloatField(localPlayer, fPrevPitch) : curP;
                            m_data.camYaw   = prevY + (curY - prevY) * pt;
                            m_data.camPitch = prevP + (curP - prevP) * pt;
                        }
                    } else if (rmViewerPosY != 0.0) {
                        m_data.camPos.y = rmViewerPosY; // Позиция RenderManager (без двойного добавления высоты глаз)
                        m_camPos = m_data.camPos;
                    }
                }
                env->DeleteLocalRef(cEntity);
            }
            if (cl) env->DeleteLocalRef(cl);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        if (localPlayer) env->DeleteLocalRef(localPlayer);

        // Читаем thirdPersonView в реальном времени каждый кадр (0=первое лицо, 1=третье лицо сзади, 2=третье лицо спереди)
        static jfieldID s_fSettings = nullptr;
        static jfieldID s_fTP = nullptr;
        if (!s_fSettings) {
            s_fSettings = env->GetFieldID(cMC, "t", "Lbid;");
            if (!s_fSettings) { env->ExceptionClear(); s_fSettings = env->GetFieldID(cMC, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;"); }
        }
        if (s_fSettings) {
            jobject settings = env->GetObjectField(mc, s_fSettings);
            if (settings) {
                if (!s_fTP) {
                    jclass cSet = env->GetObjectClass(settings);
                    s_fTP = env->GetFieldID(cSet, "aw", "I");
                    if (!s_fTP) { env->ExceptionClear(); s_fTP = env->GetFieldID(cSet, "thirdPersonView", "I"); }
                    if (!s_fTP) { env->ExceptionClear(); s_fTP = env->GetFieldID(cSet, "field_74320_O", "I"); }
                    env->DeleteLocalRef(cSet);
                }
                if (s_fTP) {
                    m_data.thirdPersonView = env->GetIntField(settings, s_fTP);
                }
                env->DeleteLocalRef(settings);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        // Запрашиваем ID текстурного атласа блоков и предметов через универсальный GetMCTextureMap
        jobject tm = GetMCTextureMap(env, mc);
        if (tm) {
            jclass cTM = env->GetObjectClass(tm);
            // 1. Метод b()I из cdf (getGlTextureId)
            jmethodID mGetTexId = env->GetMethodID(cTM, "b", "()I");
            if (!mGetTexId && env->ExceptionCheck()) { env->ExceptionClear(); mGetTexId = env->GetMethodID(cTM, "getGlTextureId", "()I"); }
            if (!mGetTexId && env->ExceptionCheck()) { env->ExceptionClear(); mGetTexId = env->GetMethodID(cTM, "func_110552_b", "()I"); }
            if (mGetTexId) {
                m_data.atlasTextureId = (unsigned int)env->CallIntMethod(tm, mGetTexId);
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
            // 2. Если ID все еще 0, проверяем поле a:I в cdf (суперкласс)
            if (m_data.atlasTextureId == 0) {
                jclass cCDF = env->FindClass("cdf");
                if (cCDF) {
                    jfieldID fGlId = env->GetFieldID(cCDF, "a", "I");
                    if (fGlId) {
                        m_data.atlasTextureId = (unsigned int)env->GetIntField(tm, fGlId);
                    }
                    env->DeleteLocalRef(cCDF);
                }
                if (env->ExceptionCheck()) env->ExceptionClear();
            }
            env->DeleteLocalRef(cTM);
            env->DeleteLocalRef(tm);
        }

        // 3. Дополнительный fallback через TextureManager (mc.N()) и cdp.g (LOCATION_BLOCKS_TEXTURE)
        if (m_data.atlasTextureId == 0) {
            jclass cCDP = env->FindClass("cdp");
            if (cCDP) {
                jfieldID fBlocksLoc = env->GetStaticFieldID(cCDP, "g", "Lnf;");
                if (fBlocksLoc) {
                    jobject blocksLoc = env->GetStaticObjectField(cCDP, fBlocksLoc);
                    if (blocksLoc) {
                        jmethodID mN = env->GetMethodID(cMC, "N", "()Lcdr;");
                        if (mN) {
                            jobject texMgr = env->CallObjectMethod(mc, mN);
                            if (texMgr) {
                                jclass cCDR = env->GetObjectClass(texMgr);
                                jmethodID mGetTex = env->GetMethodID(cCDR, "b", "(Lnf;)Lcds;");
                                if (mGetTex) {
                                    jobject texObj = env->CallObjectMethod(texMgr, mGetTex, blocksLoc);
                                    if (texObj) {
                                        jclass cCDS = env->GetObjectClass(texObj);
                                        jmethodID mTexId = env->GetMethodID(cCDS, "b", "()I");
                                        if (mTexId) {
                                            m_data.atlasTextureId = (unsigned int)env->CallIntMethod(texObj, mTexId);
                                        }
                                        env->DeleteLocalRef(cCDS);
                                        env->DeleteLocalRef(texObj);
                                    }
                                }
                                env->DeleteLocalRef(cCDR);
                                env->DeleteLocalRef(texMgr);
                            }
                        }
                        env->DeleteLocalRef(blocksLoc);
                    }
                }
                env->DeleteLocalRef(cCDP);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        InvalidateSpriteCachesForAtlas(m_data.atlasTextureId);



        env->DeleteLocalRef(cMC);
        env->DeleteLocalRef(mc);
    } else env->ExceptionClear();
}

// ── Прямой захват матрицы проекции из OpenGL (Zoom OptiFine + AspectRatio) ──
void JVMBridge::SetCapturedProjection(const float* proj) {
    if (!proj) return;
    memcpy(m_projection, proj, 16 * sizeof(float));
    m_hasMatrices = true;
    m_lastProjCapture = GetTickCount();
}

// ── Запись аппаратных матриц напрямую из OpenGL (хук glClear / glOrtho) ──────
void JVMBridge::SetGLMatrices(const float* mv, const float* proj, const int* vp) {
    if (mv && proj && vp) {
        memcpy(m_modelView, mv, 16 * sizeof(float));
        if (m_lastProjCapture == 0 || (GetTickCount() - m_lastProjCapture > 1000)) {
            memcpy(m_projection, proj, 16 * sizeof(float));
        }
        memcpy(m_viewport, vp, 4 * sizeof(int));
        m_hasMatrices = true;
    }
}

// ── WorldToScreen ────────────────────────────────────────────────────────────
bool JVMBridge::WorldToScreen(const Vec3& worldPos, Vec2& screenOut) const {
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    if (disp.x <= 0 || disp.y <= 0) return false;

    // В виде от 3-го лица спереди отсекаем рендер тегов перед лицом
    if (m_data.thirdPersonView == 2) return false;

    Vec3 eyeCam = m_data.camPos;
    double dx = worldPos.x - eyeCam.x;
    double dy = worldPos.y - eyeCam.y;
    double dz = worldPos.z - eyeCam.z;

    // Способ 1: Прямое аппаратное проецирование через матрицы OpenGL (MODELVIEW + PROJECTION)
    // 100% учитывает поворот камеры, покачивание при ходьбе (view bobbing),
    // FOV и субпиксельную позицию камеры без малейших дёрганий
    if (m_hasMatrices && m_projection[0] > 0.01f && m_projection[5] > 0.01f) {
        const float* M = m_modelView;
        // ActiveRenderInfo captures the camera rotation before RenderGlobal
        // applies the world-space viewer translation. Supply camera-relative
        // coordinates here; using absolute world coordinates makes every
        // marker drift with distance from the world origin.
        double eyeX = dx * M[0] + dy * M[4] + dz * M[8]  + M[12];
        double eyeY = dx * M[1] + dy * M[5] + dz * M[9]  + M[13];
        double eyeZ = dx * M[2] + dy * M[6] + dz * M[10] + M[14];
        double eyeW = dx * M[3] + dy * M[7] + dz * M[11] + M[15];

        const float* P = m_projection;
        double clipX = eyeX * P[0] + eyeY * P[4] + eyeZ * P[8]  + eyeW * P[12];
        double clipY = eyeX * P[1] + eyeY * P[5] + eyeZ * P[9]  + eyeW * P[13];
        double clipW = eyeX * P[3] + eyeY * P[7] + eyeZ * P[11] + eyeW * P[15];

        if (clipW > 0.01) {
            double ndcX = clipX / clipW;
            double ndcY = clipY / clipW;

            if (fabs(ndcX) <= 10.0 && fabs(ndcY) <= 10.0) {
                const double vpX = m_viewport[0];
                const double vpY = m_viewport[1];
                const double vpW = m_viewport[2] > 0 ? m_viewport[2] : disp.x;
                const double vpH = m_viewport[3] > 0 ? m_viewport[3] : disp.y;
                double screenX = vpX + (ndcX + 1.0) * vpW * 0.5;
                double screenY = disp.y - (vpY + (ndcY + 1.0) * vpH * 0.5);

                screenOut.x = static_cast<float>(screenX);
                screenOut.y = static_cast<float>(screenY);
                return true;
            }
        }
        // Матрицы могут относиться к соседнему кадру. Ниже пробуем расчёт
        // по текущим углам камеры, чтобы короткий рассинхрон не гасил NameTags.
    }

    // Способ 2 (Fallback): Тригонометрический расчет по углам Эйлера
    constexpr double kPI = 3.14159265358979323846;
    double yawRad   = m_data.camYaw * (kPI / 180.0);
    double pitchRad = m_data.camPitch * (kPI / 180.0);

    // camPos уже хранится на уровне глаз; повторно добавлять eyeHeight нельзя.
    dx = worldPos.x - eyeCam.x;
    dy = worldPos.y - eyeCam.y;
    dz = worldPos.z - eyeCam.z;

    double cosY = cos(yawRad),   sinY = sin(yawRad);
    double cosP = cos(pitchRad), sinP = sin(pitchRad);

    // Forward, Right, Up вектора камеры Minecraft
    double fx = -sinY * cosP, fy = -sinP, fz =  cosY * cosP;
    double rx = -cosY,        ry = 0.0,   rz = -sinY;
    double ux = -sinY * sinP, uy =  cosP, uz =  cosY * sinP;

    if (m_data.thirdPersonView == 0) {
        double dotH = -dx * sinY + dz * cosY;
        if (dotH <= 0.01) return false;
    } else if (m_data.thirdPersonView == 1) {
        eyeCam.x -= fx * 4.0;
        eyeCam.y -= fy * 4.0;
        eyeCam.z -= fz * 4.0;
        dx = worldPos.x - eyeCam.x;
        dy = worldPos.y - eyeCam.y;
        dz = worldPos.z - eyeCam.z;
    }

    double xCam = dx * rx + dy * ry + dz * rz;
    double yCam = dx * ux + dy * uy + dz * uz;
    double zCam = dx * fx + dy * fy + dz * fz;

    if (zCam <= 0.05) return false;

    double fovY_Rad = (m_data.fov > 10.0f ? m_data.fov : 70.0f) * (kPI / 180.0);
    double tanHalfY = tan(fovY_Rad * 0.5);
    double aspect = static_cast<double>(disp.x) / static_cast<double>(disp.y);
    double tanHalfX = tanHalfY * aspect;

    if (tanHalfX <= 0.001 || tanHalfY <= 0.001) return false;

    double xNorm = xCam / (zCam * tanHalfX);
    double yNorm = yCam / (zCam * tanHalfY);

    if (fabs(xNorm) <= 10.0 && fabs(yNorm) <= 10.0) {
        double centerX = disp.x * 0.5;
        double centerY = disp.y * 0.5;
        double screenX = centerX + xNorm * centerX;
        double screenY = centerY * (1.0 - yNorm);

        screenOut.x = static_cast<float>(screenX);
        screenOut.y = static_cast<float>(screenY);
        return true;
    }
    return false;
}

// ── Опрос игроков и выпавших предметов ───────────────────────────────────────
void JVMBridge::QueryWorldEntities(JNIEnv* env) {
    if (!env || !m_ok || !m_cMC) return;
    if (env->ExceptionCheck()) env->ExceptionClear();

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    jclass cMC = env->GetObjectClass(mc);

    jfieldID fPlayer = env->GetFieldID(cMC, "h", "Lbud;");
    if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
    if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
    jobject localPlayer = fPlayer ? env->GetObjectField(mc, fPlayer) : nullptr;

    jfieldID fWorld = env->GetFieldID(cMC, "f", "Lbsb;");
    if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "world", "Lnet/minecraft/client/multiplayer/WorldClient;"); }
    if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;"); }
    jobject world = fWorld ? env->GetObjectField(mc, fWorld) : nullptr;

    jobject conn = nullptr;
    jmethodID mGetConn = env->GetMethodID(cMC, "v", "()Lbrz;");
    if (!mGetConn) { env->ExceptionClear(); mGetConn = env->GetMethodID(cMC, "getConnection", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
    if (!mGetConn) { env->ExceptionClear(); mGetConn = env->GetMethodID(cMC, "func_147114_u", "()Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
    if (mGetConn) {
        conn = env->CallObjectMethod(mc, mGetConn);
        if (env->ExceptionCheck()) { env->ExceptionClear(); conn = nullptr; }
    }
    if (!conn && localPlayer) {
        jclass cPlayerLP = env->GetObjectClass(localPlayer);
        if (cPlayerLP) {
            jfieldID fPConn = env->GetFieldID(cPlayerLP, "d", "Lbrz;");
            if (!fPConn) { env->ExceptionClear(); fPConn = env->GetFieldID(cPlayerLP, "connection", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
            if (!fPConn) { env->ExceptionClear(); fPConn = env->GetFieldID(cPlayerLP, "field_71174_a", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
            if (fPConn) {
                conn = env->GetObjectField(localPlayer, fPConn);
                if (env->ExceptionCheck()) { env->ExceptionClear(); conn = nullptr; }
            }
            env->DeleteLocalRef(cPlayerLP);
        }
    }

    env->DeleteLocalRef(cMC);

    if (!world || !localPlayer || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (conn) env->DeleteLocalRef(conn);
        if (world) env->DeleteLocalRef(world);
        if (localPlayer) env->DeleteLocalRef(localPlayer);
        if (mc) env->DeleteLocalRef(mc);
        // Keep the last complete snapshot through transient JNI lookup
        // failures. Tick() clears it when the player actually leaves the world.
        return;
    }

    jclass cConn = conn ? env->GetObjectClass(conn) : nullptr;
    jmethodID mConnGetNPI = cConn ? env->GetMethodID(cConn, "a", "(Ljava/util/UUID;)Lbsc;") : nullptr;
    if (!mConnGetNPI && cConn) { env->ExceptionClear(); mConnGetNPI = env->GetMethodID(cConn, "getPlayerInfo", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;"); }
    if (!mConnGetNPI && cConn) { env->ExceptionClear(); mConnGetNPI = env->GetMethodID(cConn, "func_175102_a", "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;"); }

    jobject cl = GetMinecraftClassLoader(env);

    // Entity: vg / net.minecraft.entity.Entity
    jclass cEntity = FindMCClass(env, cl, "vg");
    if (!cEntity) cEntity = FindMCClass(env, cl, "net/minecraft/entity/Entity");

    jfieldID fPosX = nullptr;
    jfieldID fPosY = nullptr;
    jfieldID fPosZ = nullptr;
    jfieldID fPrevX = nullptr;
    jfieldID fPrevY = nullptr;
    jfieldID fPrevZ = nullptr;
    jfieldID fLastTickX = nullptr;
    jfieldID fLastTickY = nullptr;
    jfieldID fLastTickZ = nullptr;
    jfieldID fHeight = nullptr;

    if (cEntity) {
        fPosX = env->GetFieldID(cEntity, "p", "D");
        if (!fPosX) { env->ExceptionClear(); fPosX = env->GetFieldID(cEntity, "posX", "D"); }
        if (!fPosX) { env->ExceptionClear(); fPosX = env->GetFieldID(cEntity, "field_70165_t", "D"); }

        fPosY = env->GetFieldID(cEntity, "q", "D");
        if (!fPosY) { env->ExceptionClear(); fPosY = env->GetFieldID(cEntity, "posY", "D"); }
        if (!fPosY) { env->ExceptionClear(); fPosY = env->GetFieldID(cEntity, "field_70163_u", "D"); }

        fPosZ = env->GetFieldID(cEntity, "r", "D");
        if (!fPosZ) { env->ExceptionClear(); fPosZ = env->GetFieldID(cEntity, "posZ", "D"); }
        if (!fPosZ) { env->ExceptionClear(); fPosZ = env->GetFieldID(cEntity, "field_70161_v", "D"); }

        fPrevX = env->GetFieldID(cEntity, "m", "D");
        if (!fPrevX) { env->ExceptionClear(); fPrevX = env->GetFieldID(cEntity, "prevPosX", "D"); }
        if (!fPrevX) { env->ExceptionClear(); fPrevX = env->GetFieldID(cEntity, "field_70169_q", "D"); }

        fPrevY = env->GetFieldID(cEntity, "n", "D");
        if (!fPrevY) { env->ExceptionClear(); fPrevY = env->GetFieldID(cEntity, "prevPosY", "D"); }
        if (!fPrevY) { env->ExceptionClear(); fPrevY = env->GetFieldID(cEntity, "field_70167_r", "D"); }

        fPrevZ = env->GetFieldID(cEntity, "o", "D");
        if (!fPrevZ) { env->ExceptionClear(); fPrevZ = env->GetFieldID(cEntity, "prevPosZ", "D"); }
        if (!fPrevZ) { env->ExceptionClear(); fPrevZ = env->GetFieldID(cEntity, "field_70166_s", "D"); }

        fLastTickX = env->GetFieldID(cEntity, "M", "D");
        if (!fLastTickX) { env->ExceptionClear(); fLastTickX = env->GetFieldID(cEntity, "lastTickPosX", "D"); }
        if (!fLastTickX) { env->ExceptionClear(); fLastTickX = env->GetFieldID(cEntity, "field_70142_S", "D"); }

        fLastTickY = env->GetFieldID(cEntity, "N", "D");
        if (!fLastTickY) { env->ExceptionClear(); fLastTickY = env->GetFieldID(cEntity, "lastTickPosY", "D"); }
        if (!fLastTickY) { env->ExceptionClear(); fLastTickY = env->GetFieldID(cEntity, "field_70137_T", "D"); }

        fLastTickZ = env->GetFieldID(cEntity, "O", "D");
        if (!fLastTickZ) { env->ExceptionClear(); fLastTickZ = env->GetFieldID(cEntity, "lastTickPosZ", "D"); }
        if (!fLastTickZ) { env->ExceptionClear(); fLastTickZ = env->GetFieldID(cEntity, "field_70136_U", "D"); }

        fHeight = env->GetFieldID(cEntity, "H", "F");
        if (!fHeight) { env->ExceptionClear(); fHeight = env->GetFieldID(cEntity, "height", "F"); }
        if (!fHeight) { env->ExceptionClear(); fHeight = env->GetFieldID(cEntity, "field_70131_O", "F"); }
    }

    jfieldID fLastX = fLastTickX ? fLastTickX : fPrevX;
    jfieldID fLastY = fLastTickY ? fLastTickY : fPrevY;
    jfieldID fLastZ = fLastTickZ ? fLastTickZ : fPrevZ;

    // Читаем renderPartialTicks из Timer (bib.Y.c) для идеальной плавности рендера меток над головой
    float pt = 1.0f;
    jclass cMC_T = env->GetObjectClass(mc);
    if (cMC_T) {
        jfieldID fTimer = env->GetFieldID(cMC_T, "Y", "Lbih;");
        if (!fTimer) { env->ExceptionClear(); fTimer = env->GetFieldID(cMC_T, "timer", "Lnet/minecraft/util/Timer;"); }
        if (!fTimer) { env->ExceptionClear(); fTimer = env->GetFieldID(cMC_T, "field_71428_T", "Lnet/minecraft/util/Timer;"); }
        if (fTimer) {
            jobject timer = env->GetObjectField(mc, fTimer);
            if (timer && !env->ExceptionCheck()) {
                jclass cTimer = env->GetObjectClass(timer);
                jfieldID fTicks = env->GetFieldID(cTimer, "c", "F");
                if (!fTicks) { env->ExceptionClear(); fTicks = env->GetFieldID(cTimer, "renderPartialTicks", "F"); }
                if (!fTicks) { env->ExceptionClear(); fTicks = env->GetFieldID(cTimer, "field_194147_b", "F"); }
                if (fTicks) {
                    pt = env->GetFloatField(timer, fTicks);
                    if (pt < 0.0f) pt = 0.0f;
                    if (pt > 1.0f) pt = 1.0f;
                }
                m_data.timerPartialTicks = pt;
                env->DeleteLocalRef(cTimer);
                env->DeleteLocalRef(timer);
            } else env->ExceptionClear();
        }
        env->DeleteLocalRef(cMC_T);
    }

    double myX = (fPosX && localPlayer) ? env->GetDoubleField(localPlayer, fPosX) : 0.0;
    double myY = (fPosY && localPlayer) ? env->GetDoubleField(localPlayer, fPosY) : 0.0;
    double myZ = (fPosZ && localPlayer) ? env->GetDoubleField(localPlayer, fPosZ) : 0.0;
    double myLastX = (fLastTickX && localPlayer) ? env->GetDoubleField(localPlayer, fLastTickX) : myX;
    double myLastY = (fLastTickY && localPlayer) ? env->GetDoubleField(localPlayer, fLastTickY) : myY;
    double myLastZ = (fLastTickZ && localPlayer) ? env->GetDoubleField(localPlayer, fLastTickZ) : myZ;

    double myInterpX = myLastX + (myX - myLastX) * pt;
    double myInterpY = myLastY + (myY - myLastY) * pt;
    double myInterpZ = myLastZ + (myZ - myLastZ) * pt;
    float  myHeight = (fHeight && localPlayer) ? env->GetFloatField(localPlayer, fHeight) : 1.8f;

    jfieldID fYaw = cEntity ? env->GetFieldID(cEntity, "v", "F") : nullptr;
    if (!fYaw && cEntity) { env->ExceptionClear(); fYaw = env->GetFieldID(cEntity, "rotationYaw", "F"); }
    if (!fYaw && cEntity) { env->ExceptionClear(); fYaw = env->GetFieldID(cEntity, "field_70177_z", "F"); }

    jfieldID fPitch = cEntity ? env->GetFieldID(cEntity, "w", "F") : nullptr;
    if (!fPitch && cEntity) { env->ExceptionClear(); fPitch = env->GetFieldID(cEntity, "rotationPitch", "F"); }
    if (!fPitch && cEntity) { env->ExceptionClear(); fPitch = env->GetFieldID(cEntity, "field_70125_A", "F"); }

    jfieldID fPrevYaw = cEntity ? env->GetFieldID(cEntity, "x", "F") : nullptr;
    if (!fPrevYaw && cEntity) { env->ExceptionClear(); fPrevYaw = env->GetFieldID(cEntity, "prevRotationYaw", "F"); }
    if (!fPrevYaw && cEntity) { env->ExceptionClear(); fPrevYaw = env->GetFieldID(cEntity, "field_70126_B", "F"); }

    jfieldID fPrevPitch = cEntity ? env->GetFieldID(cEntity, "y", "F") : nullptr;
    if (!fPrevPitch && cEntity) { env->ExceptionClear(); fPrevPitch = env->GetFieldID(cEntity, "prevRotationPitch", "F"); }
    if (!fPrevPitch && cEntity) { env->ExceptionClear(); fPrevPitch = env->GetFieldID(cEntity, "field_70127_C", "F"); }

    float curYaw   = (fYaw && localPlayer)   ? env->GetFloatField(localPlayer, fYaw)   : 0.0f;
    float curPitch = (fPitch && localPlayer) ? env->GetFloatField(localPlayer, fPitch) : 0.0f;
    float prevYaw  = (fPrevYaw && localPlayer) ? env->GetFloatField(localPlayer, fPrevYaw) : curYaw;
    float prevPitch = (fPrevPitch && localPlayer) ? env->GetFloatField(localPlayer, fPrevPitch) : curPitch;

    float myYaw   = prevYaw + (curYaw - prevYaw) * pt;
    float myPitch = prevPitch + (curPitch - prevPitch) * pt;

    m_data.playerPos = { myInterpX, myInterpY, myInterpZ };

    // Если позиция камеры ещё не обновлена RenderManager в UpdateRenderMatrices, инициализируем её
    if (m_data.camPos.x == 0.0 && m_data.camPos.y == 0.0 && m_data.camPos.z == 0.0) {
        jmethodID mEyeH = (cEntity && localPlayer) ? env->GetMethodID(cEntity, "by", "()F") : nullptr;
        if (!mEyeH && cEntity) { env->ExceptionClear(); mEyeH = env->GetMethodID(cEntity, "getEyeHeight", "()F"); }
        if (!mEyeH && cEntity) { env->ExceptionClear(); mEyeH = env->GetMethodID(cEntity, "func_70047_e", "()F"); }
        float eyeH = (mEyeH && localPlayer) ? env->CallFloatMethod(localPlayer, mEyeH) : (fHeight ? (myHeight * 0.85f) : 1.62f);

        m_data.camYaw = myYaw;
        m_data.camPitch = myPitch;
        m_data.camPos.x = myInterpX;
        m_data.camPos.y = myInterpY + eyeH;
        m_data.camPos.z = myInterpZ;
        m_camPos = m_data.camPos;
    }

    jclass cWorld = env->GetObjectClass(world);

    // Method canEntityBeSeen (1.12.2 EntityLivingBase: func_70685_l / D(vg)Z) для проверки стен в AimBot
    jclass cLivingBase = FindMCClass(env, cl, "vp");
    if (!cLivingBase) cLivingBase = FindMCClass(env, cl, "net/minecraft/entity/EntityLivingBase");
    jmethodID mCanBeSeen = cLivingBase ? env->GetMethodID(cLivingBase, "D", "(Lvg;)Z") : nullptr;
    if (!mCanBeSeen && cLivingBase) { env->ExceptionClear(); mCanBeSeen = env->GetMethodID(cLivingBase, "func_70685_l", "(Lvg;)Z"); }
    if (!mCanBeSeen && cLivingBase) { env->ExceptionClear(); mCanBeSeen = env->GetMethodID(cLivingBase, "canEntityBeSeen", "(Lnet/minecraft/entity/Entity;)Z"); }
    if (!mCanBeSeen && cLivingBase) { env->ExceptionClear(); mCanBeSeen = env->GetMethodID(cLivingBase, "canEntityBeSeen", "(Lvg;)Z"); }

    // ── 1. Игроки (NameTags) ──────────────────────────────────────────────────
    std::vector<PlayerTagInfo> newPlayers;

    // Добавляем СЕБЯ (localPlayer) для отображения NameTag ТОЛЬКО при виде от 3-го лица (F5)!
    if (m_data.thirdPersonView > 0) {
        PlayerTagInfo selfTag;
        selfTag.name = m_data.username.empty() ? "Me" : m_data.username;
        selfTag.realName = selfTag.name;
        selfTag.pos = { myInterpX, myInterpY, myInterpZ };
        selfTag.width = 0.6;
        selfTag.height = myHeight;
        selfTag.distance = 0.0f;
        selfTag.health = 20.0f;
        selfTag.maxHealth = 20.0f;
        selfTag.isSneaking = false;
        for (int i = 0; i < 4; ++i) {
            selfTag.armor[i] = m_data.equipment[i];
            if (!m_data.equipment[i].isEmpty) selfTag.items.push_back(m_data.equipment[i]);
        }
        selfTag.heldItem = m_data.equipment[4];
        if (!m_data.equipment[4].isEmpty) selfTag.items.push_back(m_data.equipment[4]);
        newPlayers.push_back(selfTag);
    }

    if (env->ExceptionCheck()) env->ExceptionClear();

    std::unordered_set<int> seenEntityIds;
    auto processPlayer = [&](jobject pObj) {
        if (!pObj) return;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (localPlayer && env->IsSameObject(pObj, localPlayer)) return;

        jclass cPlayer = env->GetObjectClass(pObj);
        if (!cPlayer) return;

        // Дедупликация по уникальному Entity ID в мире (1.12.2: vg.c / field_145783_c)
        static jfieldID s_fEntityId = nullptr;
        if (!s_fEntityId && cEntity) {
            s_fEntityId = env->GetFieldID(cEntity, "field_145783_c", "I");
            if (!s_fEntityId) { env->ExceptionClear(); s_fEntityId = env->GetFieldID(cEntity, "c", "I"); }
            if (!s_fEntityId) { env->ExceptionClear(); s_fEntityId = env->GetFieldID(cEntity, "entityId", "I"); }
        }
        int entId = (s_fEntityId && pObj) ? env->GetIntField(pObj, s_fEntityId) : 0;
        if (env->ExceptionCheck()) { env->ExceptionClear(); entId = 0; }
        if (entId == 0) {
            jmethodID mGetEntId = env->GetMethodID(cPlayer, "func_145782_y", "()I");
            if (!mGetEntId) { env->ExceptionClear(); mGetEntId = env->GetMethodID(cPlayer, "getEntityId", "()I"); }
            if (mGetEntId) {
                entId = env->CallIntMethod(pObj, mGetEntId);
                if (env->ExceptionCheck()) { env->ExceptionClear(); entId = 0; }
            }
        }
        if (entId != 0) {
            if (seenEntityIds.count(entId) > 0) {
                env->DeleteLocalRef(cPlayer);
                return;
            }
            seenEntityIds.insert(entId);
        }

        double px = fPosX ? env->GetDoubleField(pObj, fPosX) : 0.0;
        double py = fPosY ? env->GetDoubleField(pObj, fPosY) : 0.0;
        double pz = fPosZ ? env->GetDoubleField(pObj, fPosZ) : 0.0;
        double pPrevX = fPrevX ? env->GetDoubleField(pObj, fPrevX) : px;
        double pPrevY = fPrevY ? env->GetDoubleField(pObj, fPrevY) : py;
        double pPrevZ = fPrevZ ? env->GetDoubleField(pObj, fPrevZ) : pz;
        double pLastX = fLastTickX ? env->GetDoubleField(pObj, fLastTickX) : pPrevX;
        double pLastY = fLastTickY ? env->GetDoubleField(pObj, fLastTickY) : pPrevY;
        double pLastZ = fLastTickZ ? env->GetDoubleField(pObj, fLastTickZ) : pPrevZ;

        double pInterpX = pLastX + (px - pLastX) * pt;
        double pInterpY = pLastY + (py - pLastY) * pt;
        double pInterpZ = pLastZ + (pz - pLastZ) * pt;
        float pHeight = fHeight ? env->GetFloatField(pObj, fHeight) : 1.8f;

        double dx = pInterpX - myInterpX;
        double dy = pInterpY - myInterpY;
        double dz = pInterpZ - myInterpZ;
        double dist = sqrt(dx*dx + dy*dy + dz*dz);

        if (dist > 500.0) {
            env->DeleteLocalRef(cPlayer);
            return;
        }

        std::string pName = "";
        std::string resolvedRealName = "";
        std::string entityUUIDStr = "";

        // Шаг -1: Проверяем постоянную память по Entity ID (гарантирует сохранение ника на любой дистанции)
        if (entId != 0) {
            auto itE = m_knownPlayerNamesByEntId.find(entId);
            if (itE != m_knownPlayerNamesByEntId.end() && IsValidPlayerName(itE->second)) {
                resolvedRealName = itE->second;
            }
        }

        // ── Шаг 0 (RustEx Bypass): поиск по UniqueID сущности в m_knownPlayerNamesByUUID и m_offlineTabNames ──
        jmethodID mGetUUID = env->GetMethodID(cPlayer, "bm", "()Ljava/util/UUID;");
        if (!mGetUUID && cEntity) { env->ExceptionClear(); mGetUUID = env->GetMethodID(cEntity, "bm", "()Ljava/util/UUID;"); }
        if (!mGetUUID) { env->ExceptionClear(); mGetUUID = env->GetMethodID(cPlayer, "getUniqueID", "()Ljava/util/UUID;"); }
        if (!mGetUUID) { env->ExceptionClear(); mGetUUID = env->GetMethodID(cPlayer, "func_110124_au", "()Ljava/util/UUID;"); }
        jobject uuidObj = mGetUUID ? env->CallObjectMethod(pObj, mGetUUID) : nullptr;
        if (env->ExceptionCheck()) { env->ExceptionClear(); uuidObj = nullptr; }
        if (!uuidObj) {
            jfieldID fUUID = env->GetFieldID(cPlayer, "aq", "Ljava/util/UUID;");
            if (!fUUID && cEntity) { env->ExceptionClear(); fUUID = env->GetFieldID(cEntity, "aq", "Ljava/util/UUID;"); }
            if (fUUID) {
                uuidObj = env->GetObjectField(pObj, fUUID);
                if (env->ExceptionCheck()) { env->ExceptionClear(); uuidObj = nullptr; }
            }
        }
        if (uuidObj) {
            jclass cUUID = env->GetObjectClass(uuidObj);
            jmethodID mToString = env->GetMethodID(cUUID, "toString", "()Ljava/lang/String;");
            if (mToString) {
                auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(uuidObj, mToString));
                if (jStr && !env->ExceptionCheck()) {
                    const char* uChars = env->GetStringUTFChars(jStr, nullptr);
                    if (uChars && uChars[0] != '\0') {
                        entityUUIDStr = uChars;
                        if (resolvedRealName.empty()) {
                            auto itU = m_knownPlayerNamesByUUID.find(entityUUIDStr);
                            if (itU != m_knownPlayerNamesByUUID.end() && IsValidPlayerName(itU->second)) {
                                resolvedRealName = itU->second;
                            }
                        }
                        if (resolvedRealName.empty()) {
                            auto it = m_offlineTabNames.find(uChars);
                            if (it != m_offlineTabNames.end() && IsValidPlayerName(it->second)) {
                                resolvedRealName = it->second;
                            }
                        }
                        env->ReleaseStringUTFChars(jStr, uChars);
                    }
                    env->DeleteLocalRef(jStr);
                } else env->ExceptionClear();
            }
            env->DeleteLocalRef(cUUID);
        }

        // ── Шаг 1: NetworkPlayerInfo (bsc) ──────────────────────────────────
        // 1.1 Прямой запрос через connection.getPlayerInfo(uuidObj) (работает на ЛЮБОЙ дистанции!)
        jobject npiObj = nullptr;
        if (conn && mConnGetNPI && uuidObj) {
            npiObj = env->CallObjectMethod(conn, mConnGetNPI, uuidObj);
            if (env->ExceptionCheck()) { env->ExceptionClear(); npiObj = nullptr; }
        }
        // 1.2 Fallback: вызов AbstractClientPlayer.c() / getPlayerInfo()
        if (!npiObj) {
            jmethodID mGetNPI = env->GetMethodID(cPlayer, "c", "()Lbsc;");
            if (!mGetNPI) { env->ExceptionClear(); mGetNPI = env->GetMethodID(cPlayer, "getPlayerInfo", "()Lnet/minecraft/client/network/NetworkPlayerInfo;"); }
            if (!mGetNPI) { env->ExceptionClear(); mGetNPI = env->GetMethodID(cPlayer, "func_175155_b", "()Lnet/minecraft/client/network/NetworkPlayerInfo;"); }
            if (mGetNPI) {
                npiObj = env->CallObjectMethod(pObj, mGetNPI);
                if (env->ExceptionCheck()) { env->ExceptionClear(); npiObj = nullptr; }
            }
        }
        // 1.3 Fallback: поле buc.d (playerInfo)
        if (!npiObj) {
            jfieldID fNPI = env->GetFieldID(cPlayer, "d", "Lbsc;");
            if (!fNPI) { env->ExceptionClear(); fNPI = env->GetFieldID(cPlayer, "playerInfo", "Lnet/minecraft/client/network/NetworkPlayerInfo;"); }
            if (!fNPI) { env->ExceptionClear(); fNPI = env->GetFieldID(cPlayer, "field_175157_a", "Lnet/minecraft/client/network/NetworkPlayerInfo;"); }
            if (fNPI) {
                npiObj = env->GetObjectField(pObj, fNPI);
                if (env->ExceptionCheck()) { env->ExceptionClear(); npiObj = nullptr; }
            }
        }

        if (uuidObj) {
            env->DeleteLocalRef(uuidObj);
            uuidObj = nullptr;
        }

        if (npiObj) {
            jclass cNPI = env->GetObjectClass(npiObj);
            // 1.1 bsc.l() -> hh (DisplayName в Tab)
            if (resolvedRealName.empty()) {
                jmethodID mNpiDisp = env->GetMethodID(cNPI, "l", "()Lhh;");
                if (!mNpiDisp) { env->ExceptionClear(); mNpiDisp = env->GetMethodID(cNPI, "getDisplayName", "()Lnet/minecraft/util/text/ITextComponent;"); }
                if (!mNpiDisp) { env->ExceptionClear(); mNpiDisp = env->GetMethodID(cNPI, "func_178854_k", "()Lnet/minecraft/util/text/ITextComponent;"); }
                if (mNpiDisp) {
                    jobject dispComp = env->CallObjectMethod(npiObj, mNpiDisp);
                    if (dispComp && !env->ExceptionCheck()) {
                        jclass cComp = env->GetObjectClass(dispComp);
                        jmethodID mD = env->GetMethodID(cComp, "d", "()Ljava/lang/String;");
                        if (!mD) { env->ExceptionClear(); mD = env->GetMethodID(cComp, "getFormattedText", "()Ljava/lang/String;"); }
                        if (!mD) { env->ExceptionClear(); mD = env->GetMethodID(cComp, "c", "()Ljava/lang/String;"); }
                        if (mD) {
                            auto jD = reinterpret_cast<jstring>(env->CallObjectMethod(dispComp, mD));
                            if (jD && !env->ExceptionCheck()) {
                                const char* s = env->GetStringUTFChars(jD, nullptr);
                                if (s) {
                                    std::string cand = ExtractRealPlayerName(s, m_data.username);
                                    if (IsValidPlayerName(cand)) resolvedRealName = cand;
                                    env->ReleaseStringUTFChars(jD, s);
                                }
                                env->DeleteLocalRef(jD);
                            } else env->ExceptionClear();
                        }
                        env->DeleteLocalRef(cComp);
                        env->DeleteLocalRef(dispComp);
                    } else env->ExceptionClear();
                }
            }
            // 1.2 bsc.a() -> GameProfile.getName()
            if (resolvedRealName.empty()) {
                jmethodID mNpiProf = env->GetMethodID(cNPI, "a", "()Lcom/mojang/authlib/GameProfile;");
                if (!mNpiProf) { env->ExceptionClear(); mNpiProf = env->GetMethodID(cNPI, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;"); }
                if (!mNpiProf) { env->ExceptionClear(); mNpiProf = env->GetMethodID(cNPI, "func_178845_a", "()Lcom/mojang/authlib/GameProfile;"); }
                if (mNpiProf) {
                    jobject prof = env->CallObjectMethod(npiObj, mNpiProf);
                    if (prof && !env->ExceptionCheck()) {
                        jclass cProf = env->GetObjectClass(prof);
                        jmethodID mPName = env->GetMethodID(cProf, "getName", "()Ljava/lang/String;");
                        if (mPName) {
                            auto jPN = reinterpret_cast<jstring>(env->CallObjectMethod(prof, mPName));
                            if (jPN && !env->ExceptionCheck()) {
                                const char* s = env->GetStringUTFChars(jPN, nullptr);
                                if (s) {
                                    std::string cand = ExtractRealPlayerName(s, m_data.username);
                                    if (IsValidPlayerName(cand)) resolvedRealName = cand;
                                    env->ReleaseStringUTFChars(jPN, s);
                                }
                                env->DeleteLocalRef(jPN);
                            } else env->ExceptionClear();
                        }
                        env->DeleteLocalRef(cProf);
                        env->DeleteLocalRef(prof);
                    } else env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(cNPI);
            env->DeleteLocalRef(npiObj);
        }

        // ── Шаг 2: Entity.i_().d() (Minecraft getDisplayName().getFormattedText()) ──
        if (resolvedRealName.empty()) {
            jmethodID mDisp = env->GetMethodID(cPlayer, "i_", "()Lhh;");
            if (!mDisp && cEntity) { env->ExceptionClear(); mDisp = env->GetMethodID(cEntity, "i_", "()Lhh;"); }
            if (!mDisp) { env->ExceptionClear(); mDisp = env->GetMethodID(cPlayer, "getDisplayName", "()Lnet/minecraft/util/text/ITextComponent;"); }
            if (mDisp) {
                jobject comp = env->CallObjectMethod(pObj, mDisp);
                if (comp && !env->ExceptionCheck()) {
                    jclass cComp = env->GetObjectClass(comp);
                    jmethodID mD = env->GetMethodID(cComp, "d", "()Ljava/lang/String;");
                    if (!mD) { env->ExceptionClear(); mD = env->GetMethodID(cComp, "getFormattedText", "()Ljava/lang/String;"); }
                    if (!mD) { env->ExceptionClear(); mD = env->GetMethodID(cComp, "c", "()Ljava/lang/String;"); }
                    if (!mD) { env->ExceptionClear(); mD = env->GetMethodID(cComp, "getUnformattedText", "()Ljava/lang/String;"); }
                    if (mD) {
                        auto jTxt = reinterpret_cast<jstring>(env->CallObjectMethod(comp, mD));
                        if (jTxt && !env->ExceptionCheck()) {
                            const char* s = env->GetStringUTFChars(jTxt, nullptr);
                            if (s && s[0] != '\0') {
                                std::string cand = ExtractRealPlayerName(s, m_data.username);
                                if (IsValidPlayerName(cand)) resolvedRealName = cand;
                                if (pName.empty() && IsValidPlayerName(s)) pName = s;
                            }
                            if (s) env->ReleaseStringUTFChars(jTxt, s);
                            env->DeleteLocalRef(jTxt);
                        } else env->ExceptionClear();
                    }
                    env->DeleteLocalRef(cComp);
                    env->DeleteLocalRef(comp);
                } else env->ExceptionClear();
            }
        }

        // ── Шаг 3: Entity.bq() (getCustomNameTag в 1.12.2) ───────────────────
        if (resolvedRealName.empty()) {
            jmethodID mCustom = env->GetMethodID(cPlayer, "bq", "()Ljava/lang/String;");
            if (!mCustom && cEntity) { env->ExceptionClear(); mCustom = env->GetMethodID(cEntity, "bq", "()Ljava/lang/String;"); }
            if (!mCustom) { env->ExceptionClear(); mCustom = env->GetMethodID(cPlayer, "getCustomNameTag", "()Ljava/lang/String;"); }
            if (!mCustom) { env->ExceptionClear(); mCustom = env->GetMethodID(cPlayer, "h_", "()Ljava/lang/String;"); }
            if (mCustom) {
                auto jCName = reinterpret_cast<jstring>(env->CallObjectMethod(pObj, mCustom));
                if (jCName && !env->ExceptionCheck()) {
                    const char* s = env->GetStringUTFChars(jCName, nullptr);
                    if (s && s[0] != '\0') {
                        std::string cand = ExtractRealPlayerName(s, m_data.username);
                        if (IsValidPlayerName(cand)) resolvedRealName = cand;
                        if (pName.empty() && IsValidPlayerName(s)) pName = s;
                    }
                    if (s) env->ReleaseStringUTFChars(jCName, s);
                    env->DeleteLocalRef(jCName);
                } else env->ExceptionClear();
            }
        }

        // ── Шаг 4: EntityPlayer getGameProfile() ─────────────────────────────
        if (resolvedRealName.empty()) {
            jmethodID mProf = env->GetMethodID(cPlayer, "bK", "()Lcom/mojang/authlib/GameProfile;");
            if (!mProf) { env->ExceptionClear(); mProf = env->GetMethodID(cPlayer, "func_146103_bH", "()Lcom/mojang/authlib/GameProfile;"); }
            if (!mProf) { env->ExceptionClear(); mProf = env->GetMethodID(cPlayer, "da", "()Lcom/mojang/authlib/GameProfile;"); }
            if (!mProf) { env->ExceptionClear(); mProf = env->GetMethodID(cPlayer, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;"); }
            jobject prof = mProf ? env->CallObjectMethod(pObj, mProf) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); prof = nullptr; }
            if (!prof) {
                const char* profFields[] = { "bS", "bU", "field_146106_bW", "g" };
                for (const char* pfn : profFields) {
                    jfieldID fProf = env->GetFieldID(cPlayer, pfn, "Lcom/mojang/authlib/GameProfile;");
                    if (fProf) {
                        prof = env->GetObjectField(pObj, fProf);
                        if (env->ExceptionCheck()) { env->ExceptionClear(); prof = nullptr; }
                        if (prof) break;
                    }
                    env->ExceptionClear();
                }
            }
            if (prof) {
                jclass cProf = env->GetObjectClass(prof);
                jmethodID mPName = env->GetMethodID(cProf, "getName", "()Ljava/lang/String;");
                if (mPName) {
                    auto jTxt = reinterpret_cast<jstring>(env->CallObjectMethod(prof, mPName));
                    if (jTxt && !env->ExceptionCheck()) {
                        const char* s = env->GetStringUTFChars(jTxt, nullptr);
                        if (s && s[0] != '\0') {
                            std::string cand = ExtractRealPlayerName(s, m_data.username);
                            if (IsValidPlayerName(cand)) resolvedRealName = cand;
                            if (pName.empty() && IsValidPlayerName(s)) pName = s;
                        }
                        if (s) env->ReleaseStringUTFChars(jTxt, s);
                        env->DeleteLocalRef(jTxt);
                    } else env->ExceptionClear();
                }
                env->DeleteLocalRef(cProf);
                env->DeleteLocalRef(prof);
            }
        }

        // ── Шаг 4.5: Scoreboard Team (getTeam().getMembershipCollection()) ──
        if (resolvedRealName.empty()) {
            jmethodID mGetTeam = env->GetMethodID(cPlayer, "aY", "()Lbhq;");
            if (!mGetTeam) { env->ExceptionClear(); mGetTeam = env->GetMethodID(cPlayer, "getTeam", "()Lnet/minecraft/scoreboard/Team;"); }
            if (!mGetTeam) { env->ExceptionClear(); mGetTeam = env->GetMethodID(cPlayer, "func_96124_cp", "()Lnet/minecraft/scoreboard/Team;"); }
            if (mGetTeam) {
                jobject teamObj = env->CallObjectMethod(pObj, mGetTeam);
                if (teamObj && !env->ExceptionCheck()) {
                    jclass cTeam = env->GetObjectClass(teamObj);
                    jmethodID mGetMembers = env->GetMethodID(cTeam, "d", "()Ljava/util/Collection;");
                    if (!mGetMembers) { env->ExceptionClear(); mGetMembers = env->GetMethodID(cTeam, "getMembershipCollection", "()Ljava/util/Collection;"); }
                    if (!mGetMembers) { env->ExceptionClear(); mGetMembers = env->GetMethodID(cTeam, "func_96670_c", "()Ljava/util/Collection;"); }
                    if (mGetMembers) {
                        jobject coll = env->CallObjectMethod(teamObj, mGetMembers);
                        if (coll && !env->ExceptionCheck()) {
                            jclass cColl = env->GetObjectClass(coll);
                            jmethodID mToArray = env->GetMethodID(cColl, "toArray", "()[Ljava/lang/Object;");
                            if (mToArray) {
                                jobjectArray arr = reinterpret_cast<jobjectArray>(env->CallObjectMethod(coll, mToArray));
                                if (arr && !env->ExceptionCheck()) {
                                    jsize len = env->GetArrayLength(arr);
                                    for (jsize ai = 0; ai < len; ++ai) {
                                        jobject elem = env->GetObjectArrayElement(arr, ai);
                                        if (elem) {
                                            auto jMem = reinterpret_cast<jstring>(elem);
                                            const char* mStr = env->GetStringUTFChars(jMem, nullptr);
                                            if (mStr) {
                                                std::string cand = ExtractRealPlayerName(mStr, m_data.username);
                                                if (IsValidPlayerName(cand) && cand != m_data.username) {
                                                    resolvedRealName = cand;
                                                }
                                                env->ReleaseStringUTFChars(jMem, mStr);
                                            }
                                            env->DeleteLocalRef(elem);
                                            if (!resolvedRealName.empty()) break;
                                        }
                                    }
                                    env->DeleteLocalRef(arr);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cColl);
                            env->DeleteLocalRef(coll);
                        } else env->ExceptionClear();
                    }
                    env->DeleteLocalRef(cTeam);
                    env->DeleteLocalRef(teamObj);
                } else env->ExceptionClear();
            }
        }

        // ── Шаг 5: Entity.h_() (getName) ────────────────────────────────────
        if (resolvedRealName.empty()) {
            jmethodID mGetName = env->GetMethodID(cPlayer, "h_", "()Ljava/lang/String;");
            if (!mGetName && cEntity) { env->ExceptionClear(); mGetName = env->GetMethodID(cEntity, "h_", "()Ljava/lang/String;"); }
            if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cPlayer, "getName", "()Ljava/lang/String;"); }
            if (mGetName) {
                auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(pObj, mGetName));
                if (jName && !env->ExceptionCheck()) {
                    const char* s = env->GetStringUTFChars(jName, nullptr);
                    if (s && s[0] != '\0') {
                        std::string cand = ExtractRealPlayerName(s, m_data.username);
                        if (IsValidPlayerName(cand)) resolvedRealName = cand;
                        if (pName.empty() && IsValidPlayerName(s)) pName = s;
                    }
                    if (s) env->ReleaseStringUTFChars(jName, s);
                    env->DeleteLocalRef(jName);
                } else env->ExceptionClear();
            }
        }

        if (!IsValidPlayerName(pName)) pName = IsValidPlayerName(resolvedRealName) ? resolvedRealName : "Player";

        // Фильтруем "Sleeping Player" (спящих игроков)
        if (pName.find("Sleeping") != std::string::npos || pName.find("sleeping") != std::string::npos ||
            resolvedRealName.find("Sleeping") != std::string::npos || resolvedRealName.find("sleeping") != std::string::npos) {
            env->DeleteLocalRef(cPlayer);
            return;
        }

        // Фильтруем пустые/невалидные сущности и "ground"
        if (pName == "ground" || pName == "Ground" || resolvedRealName == "ground" || resolvedRealName == "Ground") {
            env->DeleteLocalRef(cPlayer);
            return;
        }

        jmethodID mHP = env->GetMethodID(cPlayer, "cd", "()F");
        if (!mHP) { env->ExceptionClear(); mHP = env->GetMethodID(cPlayer, "getHealth", "()F"); }
        if (!mHP) { env->ExceptionClear(); mHP = env->GetMethodID(cPlayer, "func_110143_aJ", "()F"); }

        float hp = 20.0f;
        if (mHP) {
            hp = env->CallFloatMethod(pObj, mHP);
            if (env->ExceptionCheck()) { env->ExceptionClear(); hp = 20.0f; }
            if (hp < 0.0f || hp > 1000.0f) hp = 20.0f;
        }

        jmethodID mMaxHP = env->GetMethodID(cPlayer, "cy", "()F");
        if (!mMaxHP) { env->ExceptionClear(); mMaxHP = env->GetMethodID(cPlayer, "cY", "()F"); }
        if (!mMaxHP) { env->ExceptionClear(); mMaxHP = env->GetMethodID(cPlayer, "getMaxHealth", "()F"); }
        if (!mMaxHP) { env->ExceptionClear(); mMaxHP = env->GetMethodID(cPlayer, "func_110138_aP", "()F"); }
        float maxHp = 20.0f;
        if (mMaxHP) {
            maxHp = env->CallFloatMethod(pObj, mMaxHP);
            if (env->ExceptionCheck()) { env->ExceptionClear(); maxHp = 20.0f; }
            if (maxHp < 1.0f || maxHp > 1000.0f) maxHp = 20.0f;
        }

        jmethodID mSneak = env->GetMethodID(cPlayer, "aU", "()Z");
        if (!mSneak) { env->ExceptionClear(); mSneak = env->GetMethodID(cPlayer, "isSneaking", "()Z"); }
        if (!mSneak) { env->ExceptionClear(); mSneak = env->GetMethodID(cPlayer, "func_70093_af", "()Z"); }
        bool isSneak = mSneak ? env->CallBooleanMethod(pObj, mSneak) : false;

        std::string finalName = IsValidPlayerName(resolvedRealName) ? resolvedRealName : (IsValidPlayerName(pName) ? pName : "Player");
        if (finalName.find("Sleeping") != std::string::npos || finalName.find("sleeping") != std::string::npos ||
            finalName == "ground" || finalName == "Ground") {
            env->DeleteLocalRef(cPlayer);
            return;
        }

        PlayerTagInfo tag;
        tag.entId = entId;
        tag.uuidStr = entityUUIDStr;
        tag.name = finalName;
        tag.realName = finalName;

        // Запоминаем валидное имя игрока в постоянную память, чтобы оно не терялось на дистанции
        if (IsValidPlayerName(tag.realName) && tag.realName != "Player") {
            if (entId != 0) m_knownPlayerNamesByEntId[entId] = tag.realName;
            if (!entityUUIDStr.empty()) m_knownPlayerNamesByUUID[entityUUIDStr] = tag.realName;
        }

        // Истинная скорость движения цели (в блоках за тик, 20 тиков/сек) без опоры на багованные motionX/Y/Z
        DWORD now = GetTickCount();
        double dispX = px - pLastX;
        double dispY = py - pLastY;
        double dispZ = pz - pLastZ;

        // Ограничиваем нереалистичные скачки скорости (телепортации)
        double rawSpeed = sqrt(dispX * dispX + dispZ * dispZ);
        if (rawSpeed > 0.8) {
            double sc = 0.8 / rawSpeed;
            dispX *= sc;
            dispZ *= sc;
        }

        Vec3 curVel{ dispX, dispY, dispZ };
        if (entId != 0) {
            auto& tr = m_playerTrackers[entId];
            if (rawSpeed > 0.001) {
                // Если игрок двигается: плавно накапливаем скорость с высоким весом текущего тика (70%)
                tr.smoothVel.x = tr.smoothVel.x * 0.3 + dispX * 0.7;
                tr.smoothVel.y = tr.smoothVel.y * 0.3 + dispY * 0.7;
                tr.smoothVel.z = tr.smoothVel.z * 0.3 + dispZ * 0.7;
                tr.lastTime = now;
            } else {
                // Если игрок остановился (нет смещения более 80мс)
                if (now - tr.lastTime > 80) {
                    tr.smoothVel.x *= 0.5;
                    tr.smoothVel.y *= 0.5;
                    tr.smoothVel.z *= 0.5;
                    if (fabs(tr.smoothVel.x) < 0.005) tr.smoothVel.x = 0.0;
                    if (fabs(tr.smoothVel.y) < 0.005) tr.smoothVel.y = 0.0;
                    if (fabs(tr.smoothVel.z) < 0.005) tr.smoothVel.z = 0.0;
                }
            }
            curVel = tr.smoothVel;
        }

        tag.vel = curVel;

        // Поворот головы цели (rotationYawHead) для Forward
        float pYaw = fYaw ? env->GetFloatField(pObj, fYaw) : 0.0f;
        if (cLivingBase) {
            jfieldID fHeadYaw = env->GetFieldID(cLivingBase, "aP", "F");
            if (!fHeadYaw) { env->ExceptionClear(); fHeadYaw = env->GetFieldID(cLivingBase, "rotationYawHead", "F"); }
            if (!fHeadYaw) { env->ExceptionClear(); fHeadYaw = env->GetFieldID(cLivingBase, "field_70759_as", "F"); }
            if (fHeadYaw) {
                float hy = env->GetFloatField(pObj, fHeadYaw);
                if (!env->ExceptionCheck()) pYaw = hy;
                else env->ExceptionClear();
            }
        }
        tag.yaw = pYaw;

                // Проверяем пассажиров сущности (на RustEx стойка с ником часто привязана как пассажир bx)
                if (resolvedRealName.empty() || resolvedRealName == "Player") {
                    jfieldID fPass = cEntity ? env->GetFieldID(cEntity, "bx", "Ljava/util/List;") : nullptr;
                    if (!fPass && cEntity) { env->ExceptionClear(); fPass = env->GetFieldID(cEntity, "passengers", "Ljava/util/List;"); }
                    if (!fPass && cEntity) { env->ExceptionClear(); fPass = env->GetFieldID(cEntity, "field_184244_h", "Ljava/util/List;"); }
                    if (fPass) {
                        jobject pList = env->GetObjectField(pObj, fPass);
                        if (pList && !env->ExceptionCheck()) {
                            jclass cPL = env->GetObjectClass(pList);
                            jmethodID mSz = env->GetMethodID(cPL, "size", "()I");
                            jmethodID mGt = env->GetMethodID(cPL, "get", "(I)Ljava/lang/Object;");
                            int psz = (mSz && mGt) ? env->CallIntMethod(pList, mSz) : 0;
                            for (int pi = 0; pi < psz; ++pi) {
                                jobject passObj = env->CallObjectMethod(pList, mGt, pi);
                                if (passObj && !env->ExceptionCheck()) {
                                    jmethodID mPCustom = env->GetMethodID(cEntity, "bq", "()Ljava/lang/String;");
                                    if (!mPCustom) { env->ExceptionClear(); mPCustom = env->GetMethodID(cEntity, "getCustomNameTag", "()Ljava/lang/String;"); }
                                    if (!mPCustom) { env->ExceptionClear(); mPCustom = env->GetMethodID(cEntity, "h_", "()Ljava/lang/String;"); }
                                    if (!mPCustom) { env->ExceptionClear(); mPCustom = env->GetMethodID(cEntity, "getName", "()Ljava/lang/String;"); }
                                    if (mPCustom) {
                                        auto jPassName = reinterpret_cast<jstring>(env->CallObjectMethod(passObj, mPCustom));
                                        if (jPassName && !env->ExceptionCheck()) {
                                            const char* pch = env->GetStringUTFChars(jPassName, nullptr);
                                            if (pch && pch[0] != '\0') {
                                                std::string cand = ExtractRealPlayerName(pch, m_data.username);
                                                if (IsValidPlayerName(cand) && cand != m_data.username) {
                                                    resolvedRealName = cand;
                                                    tag.realName = cand;
                                                    tag.name = cand;
                                                }
                                                env->ReleaseStringUTFChars(jPassName, pch);
                                            }
                                            env->DeleteLocalRef(jPassName);
                                        } else env->ExceptionClear();
                                    }
                                    env->DeleteLocalRef(passObj);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cPL);
                            env->DeleteLocalRef(pList);
                        } else env->ExceptionClear();
                    }
                }

                tag.pos = { pInterpX, pInterpY, pInterpZ };
                tag.width = 0.6;
                tag.height = pHeight;
                tag.distance = static_cast<float>(dist);
                tag.health = hp;
                tag.maxHealth = maxHp;
                tag.isSneaking = isSneak;

                // Проверяем отдельные маркеры, а не подстроку "bot":
                // иначе обычные ники вроде Robot/Bottle ошибочно исключаются AimBot.
                std::string lowerPName = pName;
                for (char& ch : lowerPName) ch = static_cast<char>(tolower(ch));
                auto hasNameToken = [&](const char* token) {
                    const size_t tokenLen = strlen(token);
                    size_t pos = lowerPName.find(token);
                    while (pos != std::string::npos) {
                        const bool leftBoundary = pos == 0 || !isalnum(static_cast<unsigned char>(lowerPName[pos - 1]));
                        const size_t end = pos + tokenLen;
                        const bool rightBoundary = end >= lowerPName.size() || !isalnum(static_cast<unsigned char>(lowerPName[end]));
                        if (leftBoundary && rightBoundary) return true;
                        pos = lowerPName.find(token, pos + 1);
                    }
                    return false;
                };
                const bool botPrefix = lowerPName.rfind("bot", 0) == 0 &&
                    (lowerPName.size() == 3 || !isalpha(static_cast<unsigned char>(lowerPName[3])));
                if (lowerPName.find("[npc]") != std::string::npos || botPrefix ||
                    hasNameToken("bot") || hasNameToken("npc")) {
                    tag.isBot = true;
                }

                // Префикс ранга [VIP] и суффикс клана из getDisplayName()
                jmethodID mGetDisp = env->GetMethodID(cPlayer, "i_", "()Lhh;");
                if (!mGetDisp) { env->ExceptionClear(); mGetDisp = env->GetMethodID(cPlayer, "getDisplayName", "()Lnet/minecraft/util/text/ITextComponent;"); }
                if (!mGetDisp) { env->ExceptionClear(); mGetDisp = env->GetMethodID(cPlayer, "func_145748_c_", "()Lnet/minecraft/util/text/ITextComponent;"); }
                if (mGetDisp) {
                    jobject comp = env->CallObjectMethod(pObj, mGetDisp);
                    if (comp && !env->ExceptionCheck()) {
                        jclass cComp = env->GetObjectClass(comp);
                        jmethodID mGetText = env->GetMethodID(cComp, "e", "()Ljava/lang/String;");
                        if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "getUnformattedComponentText", "()Ljava/lang/String;"); }
                        if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "c", "()Ljava/lang/String;"); }
                        if (!mGetText) { env->ExceptionClear(); mGetText = env->GetMethodID(cComp, "getUnformattedText", "()Ljava/lang/String;"); }
                        if (mGetText) {
                            auto jText = reinterpret_cast<jstring>(env->CallObjectMethod(comp, mGetText));
                            if (jText && !env->ExceptionCheck()) {
                                const char* rawDisp = env->GetStringUTFChars(jText, nullptr);
                                if (rawDisp) {
                                    std::string dispStr = rawDisp;
                                    env->ReleaseStringUTFChars(jText, rawDisp);

                                    // Префикс ранга: "[VIP] Player" -> "[VIP] "
                                    size_t openB = dispStr.find('[');
                                    size_t closeB = dispStr.find(']');
                                    if (openB != std::string::npos && closeB != std::string::npos && closeB > openB) {
                                        tag.rankPrefix = dispStr.substr(openB, closeB - openB + 1) + " ";
                                    }

                                    // Суффикс клана: строго 1-в-1 как в NameTags.java (\+\d+|[A-Za-z]+)
                                    // Полностью исключаем §r, цифру 0 или мусорные символы
                                    size_t nickPos = dispStr.find(pName);
                                    if (nickPos != std::string::npos && nickPos + pName.length() < dispStr.length()) {
                                        std::string after = dispStr.substr(nickPos + pName.length());
                                        while (!after.empty() && (after.front() == ' ' || after.front() == '\t')) after.erase(0, 1);
                                        std::string clan;
                                        if (!after.empty()) {
                                            if (after[0] == '+' && after.length() > 1 && isdigit(static_cast<unsigned char>(after[1]))) {
                                                clan.push_back('+');
                                                for (size_t k = 1; k < after.size() && isdigit(static_cast<unsigned char>(after[k])); ++k) {
                                                    clan.push_back(after[k]);
                                                }
                                            } else if ((after[0] >= 'A' && after[0] <= 'Z') || (after[0] >= 'a' && after[0] <= 'z')) {
                                                for (size_t k = 0; k < after.size() && ((after[k] >= 'A' && after[k] <= 'Z') || (after[k] >= 'a' && after[k] <= 'z')); ++k) {
                                                    clan.push_back(after[k]);
                                                }
                                            }
                                        }
                                        if (!clan.empty()) {
                                            tag.clanSuffix = " " + clan;
                                        } else {
                                            tag.clanSuffix = "";
                                        }
                                    }
                                }
                                env->DeleteLocalRef(jText);
                            } else env->ExceptionClear();
                        }
                        env->DeleteLocalRef(cComp);
                        env->DeleteLocalRef(comp);
                    } else env->ExceptionClear();
                }

                // Активные зелья и проверка на [ZOOMED] (Potion ID 2 = Slowness в 1.12.2)
                jmethodID mGetPotions = env->GetMethodID(cPlayer, "cf", "()Ljava/util/Collection;");
                if (!mGetPotions) { env->ExceptionClear(); mGetPotions = env->GetMethodID(cPlayer, "getActivePotionEffects", "()Ljava/util/Collection;"); }
                if (!mGetPotions) { env->ExceptionClear(); mGetPotions = env->GetMethodID(cPlayer, "func_70651_bq", "()Ljava/util/Collection;"); }
                if (mGetPotions) {
                    jobject potColl = env->CallObjectMethod(pObj, mGetPotions);
                    if (potColl && !env->ExceptionCheck()) {
                        jclass cColl = env->GetObjectClass(potColl);
                        jmethodID mToArr = env->GetMethodID(cColl, "toArray", "()[Ljava/lang/Object;");
                        if (mToArr) {
                            auto arr = reinterpret_cast<jobjectArray>(env->CallObjectMethod(potColl, mToArr));
                            if (arr && !env->ExceptionCheck()) {
                                int potCount = env->GetArrayLength(arr);
                                for (int pi = 0; pi < potCount; ++pi) {
                                    jobject potEff = env->GetObjectArrayElement(arr, pi);
                                    if (potEff && !env->ExceptionCheck()) {
                                        jclass cPE = env->GetObjectClass(potEff);
                                        jmethodID mGetPot = env->GetMethodID(cPE, "a", "()Luz;");
                                        if (!mGetPot) { env->ExceptionClear(); mGetPot = env->GetMethodID(cPE, "getPotion", "()Lnet/minecraft/potion/Potion;"); }
                                        if (!mGetPot) { env->ExceptionClear(); mGetPot = env->GetMethodID(cPE, "func_188419_a", "()Lnet/minecraft/potion/Potion;"); }

                                        jmethodID mGetDur = env->GetMethodID(cPE, "b", "()I");
                                        if (!mGetDur) { env->ExceptionClear(); mGetDur = env->GetMethodID(cPE, "getDuration", "()I"); }
                                        if (!mGetDur) { env->ExceptionClear(); mGetDur = env->GetMethodID(cPE, "func_76459_b", "()I"); }

                                        int dur = mGetDur ? env->CallIntMethod(potEff, mGetDur) : 0;
                                        if (mGetPot) {
                                            jobject potObj = env->CallObjectMethod(potEff, mGetPot);
                                            if (potObj && !env->ExceptionCheck()) {
                                                jclass cP = env->GetObjectClass(potObj);
                                                jmethodID mGetId = env->GetStaticMethodID(cP, "a", "(Luz;)I");
                                                if (!mGetId) { env->ExceptionClear(); mGetId = env->GetStaticMethodID(cP, "getIdFromPotion", "(Lnet/minecraft/potion/Potion;)I"); }
                                                int potId = mGetId ? env->CallStaticIntMethod(cP, mGetId, potObj) : -1;
                                                if (potId == 2) {
                                                    // Potion ID 2 = Slowness (зум через прицел на серверах Rust)
                                                    tag.isZoomed = true;
                                                } else if (dur > 0) {
                                                    jmethodID mGetPName = env->GetMethodID(cP, "a", "()Ljava/lang/String;");
                                                    if (!mGetPName) { env->ExceptionClear(); mGetPName = env->GetMethodID(cP, "getName", "()Ljava/lang/String;"); }
                                                    if (!mGetPName) { env->ExceptionClear(); mGetPName = env->GetMethodID(cP, "func_76453_d", "()Ljava/lang/String;"); }
                                                    std::string effName = "Potion";
                                                    if (mGetPName) {
                                                        auto jPName = reinterpret_cast<jstring>(env->CallObjectMethod(potObj, mGetPName));
                                                        if (jPName && !env->ExceptionCheck()) {
                                                            const char* pChars = env->GetStringUTFChars(jPName, nullptr);
                                                            if (pChars) { effName = pChars; env->ReleaseStringUTFChars(jPName, pChars); }
                                                            env->DeleteLocalRef(jPName);
                                                        } else env->ExceptionClear();
                                                    }
                                                    if (effName.rfind("effect.", 0) == 0) effName = effName.substr(7);

                                                    int secs = dur / 20;
                                                    char pBuf[64];
                                                    snprintf(pBuf, sizeof(pBuf), "%s %d:%02d", effName.c_str(), secs / 60, secs % 60);

                                                    unsigned int pCol = (dur < 200) ? 0xFFFF4B4B : ((dur < 400) ? 0xFFFFD232 : 0xFF4BE66E);
                                                    tag.potions.push_back({ pBuf, pCol });
                                                }
                                                env->DeleteLocalRef(cP);
                                                env->DeleteLocalRef(potObj);
                                            } else env->ExceptionClear();
                                        }
                                        env->DeleteLocalRef(cPE);
                                        env->DeleteLocalRef(potEff);
                                    } else env->ExceptionClear();
                                }
                                env->DeleteLocalRef(arr);
                            } else env->ExceptionClear();
                        }
                        env->DeleteLocalRef(cColl);
                        env->DeleteLocalRef(potColl);
                    } else env->ExceptionClear();
                }

                // 1. Предмет в главной руке (co() / getHeldItemMainhand / func_184614_ca)
                jmethodID mMainH = env->GetMethodID(cPlayer, "co", "()Laip;");
                if (!mMainH) { env->ExceptionClear(); mMainH = env->GetMethodID(cPlayer, "getHeldItemMainhand", "()Lnet/minecraft/item/ItemStack;"); }
                if (!mMainH) { env->ExceptionClear(); mMainH = env->GetMethodID(cPlayer, "func_184614_ca", "()Lnet/minecraft/item/ItemStack;"); }
                if (!mMainH) { env->ExceptionClear(); mMainH = env->GetMethodID(cPlayer, "cb", "()Laip;"); }
                if (mMainH) {
                    jobject mainStack = env->CallObjectMethod(pObj, mMainH);
                    if (mainStack && !env->ExceptionCheck()) {
                        ArmorItem mh;
                        ParseItemStackHelper(env, mc, mainStack, mh);
                        tag.heldItem = mh;
                        if (!mh.isEmpty) tag.items.push_back(mh);
                        env->DeleteLocalRef(mainStack);
                    } else env->ExceptionClear();
                }

                // 2. Предмет во второй руке (cp() / getHeldItemOffhand / func_184592_cb)
                jmethodID mOffH = env->GetMethodID(cPlayer, "cp", "()Laip;");
                if (!mOffH) { env->ExceptionClear(); mOffH = env->GetMethodID(cPlayer, "getHeldItemOffhand", "()Lnet/minecraft/item/ItemStack;"); }
                if (!mOffH) { env->ExceptionClear(); mOffH = env->GetMethodID(cPlayer, "func_184592_cb", "()Lnet/minecraft/item/ItemStack;"); }
                if (!mOffH) { env->ExceptionClear(); mOffH = env->GetMethodID(cPlayer, "cc", "()Laip;"); }
                if (mOffH) {
                    jobject offStack = env->CallObjectMethod(pObj, mOffH);
                    if (offStack && !env->ExceptionCheck()) {
                        ArmorItem oh;
                        ParseItemStackHelper(env, mc, offStack, oh);
                        if (!oh.isEmpty) tag.items.push_back(oh);
                        env->DeleteLocalRef(offStack);
                    } else env->ExceptionClear();
                }

                // 3. Броня и экипировка игрока (100% надежный способ через EntityEquipmentSlot b(vl))
                bool armorParsed = false;
                jclass cSlot = FindMCClass(env, cl, "vl");
                if (!cSlot) cSlot = FindMCClass(env, cl, "net/minecraft/inventory/EntityEquipmentSlot");

                if (cSlot) {
                    jmethodID mGetSlot = env->GetMethodID(cPlayer, "b", "(Lvl;)Laip;");
                    if (!mGetSlot) { env->ExceptionClear(); mGetSlot = env->GetMethodID(cPlayer, "getItemStackFromSlot", "(Lnet/minecraft/inventory/EntityEquipmentSlot;)Lnet/minecraft/item/ItemStack;"); }
                    if (!mGetSlot) { env->ExceptionClear(); mGetSlot = env->GetMethodID(cPlayer, "func_184582_a", "(Lnet/minecraft/inventory/EntityEquipmentSlot;)Lnet/minecraft/item/ItemStack;"); }

                    if (mGetSlot) {
                        jfieldID fSlots[4] = {
                            env->GetStaticFieldID(cSlot, "c", "Lvl;"), // 0: FEET (boots)
                            env->GetStaticFieldID(cSlot, "d", "Lvl;"), // 1: LEGS (leggings)
                            env->GetStaticFieldID(cSlot, "e", "Lvl;"), // 2: CHEST (chestplate)
                            env->GetStaticFieldID(cSlot, "f", "Lvl;")  // 3: HEAD (helmet)
                        };
                        const char* slotNames[4] = { "FEET", "LEGS", "CHEST", "HEAD" };
                        for (int s = 0; s < 4; ++s) {
                            if (!fSlots[s]) {
                                env->ExceptionClear();
                                fSlots[s] = env->GetStaticFieldID(cSlot, slotNames[s], "Lnet/minecraft/inventory/EntityEquipmentSlot;");
                            }
                            if (fSlots[s]) {
                                jobject slotEnum = env->GetStaticObjectField(cSlot, fSlots[s]);
                                if (slotEnum && !env->ExceptionCheck()) {
                                    jobject stack = env->CallObjectMethod(pObj, mGetSlot, slotEnum);
                                    if (stack && !env->ExceptionCheck()) {
                                        ArmorItem arm;
                                        ParseItemStackHelper(env, mc, stack, arm);
                                        tag.armor[3 - s] = arm;
                                        if (!arm.isEmpty) {
                                            tag.items.push_back(arm);
                                            armorParsed = true;
                                        }
                                        env->DeleteLocalRef(stack);
                                    } else env->ExceptionClear();
                                    env->DeleteLocalRef(slotEnum);
                                } else env->ExceptionClear();
                            }
                        }

                        // Если главное оружие еще не считалось — читаем через MAINHAND (vl.a)
                        if (tag.heldItem.isEmpty) {
                            jfieldID fMainSlot = env->GetStaticFieldID(cSlot, "a", "Lvl;");
                            if (!fMainSlot) { env->ExceptionClear(); fMainSlot = env->GetStaticFieldID(cSlot, "MAINHAND", "Lnet/minecraft/inventory/EntityEquipmentSlot;"); }
                            if (fMainSlot) {
                                jobject mainEnum = env->GetStaticObjectField(cSlot, fMainSlot);
                                if (mainEnum && !env->ExceptionCheck()) {
                                    jobject mStack = env->CallObjectMethod(pObj, mGetSlot, mainEnum);
                                    if (mStack && !env->ExceptionCheck()) {
                                        ArmorItem mh;
                                        ParseItemStackHelper(env, mc, mStack, mh);
                                        tag.heldItem = mh;
                                        if (!mh.isEmpty) tag.items.push_back(mh);
                                        env->DeleteLocalRef(mStack);
                                    } else env->ExceptionClear();
                                    env->DeleteLocalRef(mainEnum);
                                } else env->ExceptionClear();
                            }
                        }
                    }
                    env->DeleteLocalRef(cSlot);
                }

                // Способ А: Вызов aP() (getArmorInventoryList / func_184193_aE)
                if (!armorParsed) {
                    jmethodID mArmorList = env->GetMethodID(cPlayer, "aP", "()Ljava/lang/Iterable;");
                    if (!mArmorList) { env->ExceptionClear(); mArmorList = env->GetMethodID(cPlayer, "getArmorInventoryList", "()Ljava/lang/Iterable;"); }
                    if (!mArmorList) { env->ExceptionClear(); mArmorList = env->GetMethodID(cPlayer, "func_184193_aE", "()Ljava/lang/Iterable;"); }
                    if (mArmorList) {
                        jobject aIter = env->CallObjectMethod(pObj, mArmorList);
                        if (aIter && !env->ExceptionCheck()) {
                            jclass cAIter = env->GetObjectClass(aIter);
                            jmethodID mIterator = env->GetMethodID(cAIter, "iterator", "()Ljava/util/Iterator;");
                            if (mIterator) {
                                jobject itObj = env->CallObjectMethod(aIter, mIterator);
                                if (itObj && !env->ExceptionCheck()) {
                                    jclass cIt = env->GetObjectClass(itObj);
                                    jmethodID mHasNext = env->GetMethodID(cIt, "hasNext", "()Z");
                                    jmethodID mNext = env->GetMethodID(cIt, "next", "()Ljava/lang/Object;");
                                    int slot = 0;
                                    while (slot < 4 && mHasNext && mNext && env->CallBooleanMethod(itObj, mHasNext)) {
                                        jobject stack = env->CallObjectMethod(itObj, mNext);
                                        if (stack && !env->ExceptionCheck()) {
                                            ArmorItem arm;
                                            ParseItemStackHelper(env, mc, stack, arm);
                                            tag.armor[3 - slot] = arm;
                                            if (!arm.isEmpty) {
                                                tag.items.push_back(arm);
                                                armorParsed = true;
                                            }
                                            env->DeleteLocalRef(stack);
                                        } else env->ExceptionClear();
                                        slot++;
                                    }
                                    env->DeleteLocalRef(cIt);
                                    env->DeleteLocalRef(itObj);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cAIter);
                            env->DeleteLocalRef(aIter);
                        } else env->ExceptionClear();
                    }
                }

                // Способ Б (резервный): Поиск inventory в иерархии классов игрока
                if (!armorParsed) {
                    jfieldID fInv = nullptr;
                    jclass curCls = cPlayer;
                    while (curCls && !fInv) {
                        fInv = env->GetFieldID(curCls, "bv", "Laec;");
                        if (!fInv) { env->ExceptionClear(); fInv = env->GetFieldID(curCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;"); }
                        if (!fInv) { env->ExceptionClear(); fInv = env->GetFieldID(curCls, "field_71071_by", "Lnet/minecraft/entity/player/InventoryPlayer;"); }
                        if (!fInv) {
                            env->ExceptionClear();
                            jclass superCls = env->GetSuperclass(curCls);
                            if (curCls != cPlayer) env->DeleteLocalRef(curCls);
                            curCls = superCls;
                        }
                    }
                    if (curCls && curCls != cPlayer) env->DeleteLocalRef(curCls);

                    if (fInv) {
                        jobject pInv = env->GetObjectField(pObj, fInv);
                        if (pInv && !env->ExceptionCheck()) {
                            jclass cInv = env->GetObjectClass(pInv);
                            jfieldID fArmor = env->GetFieldID(cInv, "b", "Lfi;");
                            if (!fArmor) { env->ExceptionClear(); fArmor = env->GetFieldID(cInv, "armorInventory", "Ljava/util/List;"); }
                            if (!fArmor) { env->ExceptionClear(); fArmor = env->GetFieldID(cInv, "armorInventory", "Lnet/minecraft/util/NonNullList;"); }
                            if (fArmor) {
                                jobject aList = env->GetObjectField(pInv, fArmor);
                                if (aList && !env->ExceptionCheck()) {
                                    jclass cAList = env->GetObjectClass(aList);
                                    jmethodID mAGet = env->GetMethodID(cAList, "get", "(I)Ljava/lang/Object;");
                                    if (mAGet) {
                                        for (int slot = 0; slot < 4; ++slot) {
                                            jobject stack = env->CallObjectMethod(aList, mAGet, slot);
                                            if (stack && !env->ExceptionCheck()) {
                                                ArmorItem arm;
                                                ParseItemStackHelper(env, mc, stack, arm);
                                                tag.armor[3 - slot] = arm;
                                                if (!arm.isEmpty) tag.items.push_back(arm);
                                                env->DeleteLocalRef(stack);
                                            } else env->ExceptionClear();
                                        }
                                    }
                                    env->DeleteLocalRef(cAList);
                                    env->DeleteLocalRef(aList);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cInv);
                            env->DeleteLocalRef(pInv);
                        } else env->ExceptionClear();
                    }
                }

        tag.canBeSeen = (mCanBeSeen && localPlayer) ? env->CallBooleanMethod(localPlayer, mCanBeSeen, pObj) : true;
        if (env->ExceptionCheck()) { env->ExceptionClear(); tag.canBeSeen = true; }

        tag.rawPos = { px, py, pz };
        tag.lastRender = { pLastX, pLastY, pLastZ };
        tag.prevTick = { pPrevX, pPrevY, pPrevZ };

        newPlayers.push_back(tag);
        env->DeleteLocalRef(cPlayer);
    };

    // Ищем список игроков в World (в 1.12.2 World.amu.j = playerEntities, i = weatherEffects)
    jclass cWorldSuper = env->GetSuperclass(cWorld);
    if (!cWorldSuper) cWorldSuper = FindMCClass(env, cl, "amu");
    if (!cWorldSuper) cWorldSuper = FindMCClass(env, cl, "net/minecraft/world/World");

    const char* playerListFields[] = { "j", "playerEntities", "field_73010_i", "k", "h", "i" };
    jfieldID fPlayerList = nullptr;
    for (const char* fn : playerListFields) {
        fPlayerList = env->GetFieldID(cWorld, fn, "Ljava/util/List;");
        if (fPlayerList && !env->ExceptionCheck()) break;
        env->ExceptionClear();
        fPlayerList = nullptr;
        if (cWorldSuper) {
            fPlayerList = env->GetFieldID(cWorldSuper, fn, "Ljava/util/List;");
            if (fPlayerList && !env->ExceptionCheck()) break;
            env->ExceptionClear();
            fPlayerList = nullptr;
        }
    }

    if (fPlayerList) {
        jobject playerList = env->GetObjectField(world, fPlayerList);
        if (playerList && !env->ExceptionCheck()) {
            jclass cList = env->GetObjectClass(playerList);
            jmethodID mSize = env->GetMethodID(cList, "size", "()I");
            jmethodID mGet = env->GetMethodID(cList, "get", "(I)Ljava/lang/Object;");

            int pCount = (mSize && mGet) ? env->CallIntMethod(playerList, mSize) : 0;
            if (pCount > 256) pCount = 256;

            for (int i = 0; i < pCount; ++i) {
                if (env->PushLocalFrame(256) != 0) continue;
                jobject pObj = env->CallObjectMethod(playerList, mGet, i);
                if (pObj && !env->ExceptionCheck()) {
                    processPlayer(pObj);
                } else env->ExceptionClear();
                env->PopLocalFrame(nullptr);
            }
            env->DeleteLocalRef(cList);
            env->DeleteLocalRef(playerList);
        } else env->ExceptionClear();
    }

    // ── 2. Выпавшие предметы (ItemESP) ────────────────────────────────────────
    const char* entListFields[] = { "loadedEntityList", "field_72996_f", "e" };
    jfieldID fEntityList = nullptr;
    for (const char* fn : entListFields) {
        fEntityList = env->GetFieldID(cWorld, fn, "Ljava/util/List;");
        if (fEntityList && !env->ExceptionCheck()) break;
        env->ExceptionClear();
        fEntityList = nullptr;
        if (cWorldSuper) {
            fEntityList = env->GetFieldID(cWorldSuper, fn, "Ljava/util/List;");
            if (fEntityList && !env->ExceptionCheck()) break;
            env->ExceptionClear();
            fEntityList = nullptr;
        }
    }
    if (cWorldSuper) env->DeleteLocalRef(cWorldSuper);

    jclass cEntityItem = FindMCClass(env, cl, "acl");
    if (!cEntityItem) cEntityItem = FindMCClass(env, cl, "net/minecraft/entity/item/EntityItem");

    // Классы игроков: получаем гарантированно через GetSuperclass от localPlayer
    jclass cEntityPlayer = nullptr;
    jclass cAbstractPlayer = nullptr;
    if (localPlayer) {
        jclass cLP = env->GetObjectClass(localPlayer); // EntityPlayerSP (bud)
        if (cLP) {
            cAbstractPlayer = env->GetSuperclass(cLP); // AbstractClientPlayer (buc)
            if (cAbstractPlayer) {
                cEntityPlayer = env->GetSuperclass(cAbstractPlayer); // EntityPlayer (aed)
            }
            env->DeleteLocalRef(cLP);
        }
    }
    if (!cEntityPlayer) {
        cEntityPlayer = FindMCClass(env, cl, "aed");
        if (!cEntityPlayer) cEntityPlayer = FindMCClass(env, cl, "net/minecraft/entity/player/EntityPlayer");
    }
    jclass cOtherPlayer = FindMCClass(env, cl, "bue");
    if (!cOtherPlayer) cOtherPlayer = FindMCClass(env, cl, "net/minecraft/client/entity/EntityOtherPlayerMP");

    jclass cEntityMob = FindMCClass(env, cl, "aab");
    if (!cEntityMob) cEntityMob = FindMCClass(env, cl, "net/minecraft/entity/monster/EntityMob");
    if (!cEntityMob) cEntityMob = FindMCClass(env, cl, "adt");

    jclass cEntityAnimal = FindMCClass(env, cl, "aam");
    if (!cEntityAnimal) cEntityAnimal = FindMCClass(env, cl, "net/minecraft/entity/passive/EntityAnimal");
    if (!cEntityAnimal) cEntityAnimal = FindMCClass(env, cl, "zv");

    jclass cEntityArmorStand = FindMCClass(env, cl, "abz");
    if (!cEntityArmorStand) cEntityArmorStand = FindMCClass(env, cl, "abg");
    if (!cEntityArmorStand) cEntityArmorStand = FindMCClass(env, cl, "net/minecraft/entity/item/EntityArmorStand");

    std::vector<DroppedItemInfo> newItems;
    std::vector<Vec3>            newArmorStands;
    std::vector<AutoMineTarget>  newMineTargets;
    std::vector<BlockESPInfo>    newEntityOres;
    std::vector<PlayerTagInfo>   newMobs;
    std::vector<PlayerTagInfo>   newAnimals;

    struct StandNameplate {
        Vec3 pos;
        std::string name;
    };
    std::vector<StandNameplate> standNameplates;

    jclass cBP = FindMCClass(env, cl, "et");
    if (!cBP) cBP = FindMCClass(env, cl, "net/minecraft/util/math/BlockPos");
    jmethodID mInitBP = cBP ? env->GetMethodID(cBP, "<init>", "(III)V") : nullptr;

    jmethodID mIsAir = env->GetMethodID(cWorld, "d", "(Let;)Z");
    if (!mIsAir) { env->ExceptionClear(); mIsAir = env->GetMethodID(cWorld, "isAirBlock", "(Lnet/minecraft/util/math/BlockPos;)Z"); }
    if (!mIsAir) { env->ExceptionClear(); mIsAir = env->GetMethodID(cWorld, "func_175623_d", "(Lnet/minecraft/util/math/BlockPos;)Z"); }

    jclass cSlot = FindMCClass(env, cl, "vl");
    if (!cSlot) cSlot = FindMCClass(env, cl, "net/minecraft/inventory/EntityEquipmentSlot");
    jfieldID fStandSlots[6] = { nullptr };
    if (cSlot) {
        fStandSlots[0] = env->GetStaticFieldID(cSlot, "a", "Lvl;"); // MAINHAND
        fStandSlots[1] = env->GetStaticFieldID(cSlot, "f", "Lvl;"); // HEAD
        fStandSlots[2] = env->GetStaticFieldID(cSlot, "b", "Lvl;"); // OFFHAND
        fStandSlots[3] = env->GetStaticFieldID(cSlot, "c", "Lvl;"); // FEET
        fStandSlots[4] = env->GetStaticFieldID(cSlot, "d", "Lvl;"); // LEGS
        fStandSlots[5] = env->GetStaticFieldID(cSlot, "e", "Lvl;"); // CHEST
    }
    jmethodID mGetSlot = cLivingBase ? env->GetMethodID(cLivingBase, "b", "(Lvl;)Laip;") : nullptr;
    if (!mGetSlot && cLivingBase) { env->ExceptionClear(); mGetSlot = env->GetMethodID(cLivingBase, "getItemStackFromSlot", "(Lnet/minecraft/inventory/EntityEquipmentSlot;)Lnet/minecraft/item/ItemStack;"); }
    if (!mGetSlot && cLivingBase) { env->ExceptionClear(); mGetSlot = env->GetMethodID(cLivingBase, "func_184582_a", "(Lnet/minecraft/inventory/EntityEquipmentSlot;)Lnet/minecraft/item/ItemStack;"); }

    jmethodID mGetEntId = cEntity ? env->GetMethodID(cEntity, "S", "()I") : nullptr;
    if (!mGetEntId && cEntity) { env->ExceptionClear(); mGetEntId = env->GetMethodID(cEntity, "getEntityId", "()I"); }
    if (!mGetEntId && cEntity) { env->ExceptionClear(); mGetEntId = env->GetMethodID(cEntity, "func_145782_y", "()I"); }

    if (fEntityList && (cEntityItem || cEntityPlayer || cEntityMob || cEntityAnimal || cEntityArmorStand)) {
        jobject entList = env->GetObjectField(world, fEntityList);
        if (entList && !env->ExceptionCheck()) {
            jclass cList = env->GetObjectClass(entList);
            jmethodID mSize = env->GetMethodID(cList, "size", "()I");
            jmethodID mGet = env->GetMethodID(cList, "get", "(I)Ljava/lang/Object;");

            int eCount = (mSize && mGet) ? env->CallIntMethod(entList, mSize) : 0;
            if (eCount > 512) eCount = 512;

            jmethodID mGetItem = cEntityItem ? env->GetMethodID(cEntityItem, "l", "()Laip;") : nullptr;
            if (!mGetItem && cEntityItem) { env->ExceptionClear(); mGetItem = env->GetMethodID(cEntityItem, "k", "()Laip;"); }
            if (!mGetItem && cEntityItem) { env->ExceptionClear(); mGetItem = env->GetMethodID(cEntityItem, "d", "()Laip;"); }
            if (!mGetItem && cEntityItem) { env->ExceptionClear(); mGetItem = env->GetMethodID(cEntityItem, "getEntityItem", "()Lnet/minecraft/item/ItemStack;"); }
            if (!mGetItem && cEntityItem) { env->ExceptionClear(); mGetItem = env->GetMethodID(cEntityItem, "getItem", "()Lnet/minecraft/item/ItemStack;"); }
            if (!mGetItem && cEntityItem) { env->ExceptionClear(); mGetItem = env->GetMethodID(cEntityItem, "func_92059_d", "()Lnet/minecraft/item/ItemStack;"); }

            jfieldID fItem = cEntityItem ? env->GetFieldID(cEntityItem, "field_70299_a", "Laip;") : nullptr;
            if (!fItem && cEntityItem) { env->ExceptionClear(); fItem = env->GetFieldID(cEntityItem, "item", "Laip;"); }
            if (!fItem && cEntityItem) { env->ExceptionClear(); fItem = env->GetFieldID(cEntityItem, "itemStack", "Lnet/minecraft/item/ItemStack;"); }

            for (int i = 0; i < eCount; ++i) {
                jobject eObj = env->CallObjectMethod(entList, mGet, i);
                if (!eObj || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                bool isPl = (cEntityPlayer && env->IsInstanceOf(eObj, cEntityPlayer)) ||
                            (cAbstractPlayer && env->IsInstanceOf(eObj, cAbstractPlayer)) ||
                            (cOtherPlayer && env->IsInstanceOf(eObj, cOtherPlayer));
                if (isPl) {
                    processPlayer(eObj);
                } else if (cEntityMob && env->IsInstanceOf(eObj, cEntityMob)) {
                    double mx = fPosX ? env->GetDoubleField(eObj, fPosX) : 0.0;
                    double my = fPosY ? env->GetDoubleField(eObj, fPosY) : 0.0;
                    double mz = fPosZ ? env->GetDoubleField(eObj, fPosZ) : 0.0;
                    double mLastX = fLastX ? env->GetDoubleField(eObj, fLastX) : mx;
                    double mLastY = fLastY ? env->GetDoubleField(eObj, fLastY) : my;
                    double mLastZ = fLastZ ? env->GetDoubleField(eObj, fLastZ) : mz;

                    double mInterpX = mLastX + (mx - mLastX) * pt;
                    double mInterpY = mLastY + (my - mLastY) * pt;
                    double mInterpZ = mLastZ + (mz - mLastZ) * pt;

                    double dx = mInterpX - myInterpX;
                    double dy = mInterpY - myInterpY;
                    double dz = mInterpZ - myInterpZ;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);

                    if (dist <= 500.0) {
                        PlayerTagInfo mTag;
                        mTag.name = "Mob";
                        jclass cM = env->GetObjectClass(eObj);
                        if (cM) {
                            jmethodID mGetName = env->GetMethodID(cM, "h_", "()Ljava/lang/String;");
                            if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cM, "getName", "()Ljava/lang/String;"); }
                            if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cM, "func_70005_c_", "()Ljava/lang/String;"); }
                            if (mGetName) {
                                auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(eObj, mGetName));
                                if (jName && !env->ExceptionCheck()) {
                                    const char* chars = env->GetStringUTFChars(jName, nullptr);
                                    if (chars) { mTag.name = chars; env->ReleaseStringUTFChars(jName, chars); }
                                    env->DeleteLocalRef(jName);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cM);
                        }
                        mTag.realName = mTag.name;
                        mTag.isBot = true;
                        if (mGetEntId) {
                            mTag.entId = env->CallIntMethod(eObj, mGetEntId);
                            if (env->ExceptionCheck()) { env->ExceptionClear(); mTag.entId = 0; }
                        }
                        mTag.pos = { mInterpX, mInterpY, mInterpZ };
                        mTag.width = 0.6;
                        mTag.height = fHeight ? env->GetFloatField(eObj, fHeight) : 1.95f;
                        mTag.distance = static_cast<float>(dist);
                        mTag.health = 20.0f;
                        mTag.maxHealth = 20.0f;
                        newMobs.push_back(mTag);
                    }
                } else if (cEntityAnimal && env->IsInstanceOf(eObj, cEntityAnimal)) {
                    double ax = fPosX ? env->GetDoubleField(eObj, fPosX) : 0.0;
                    double ay = fPosY ? env->GetDoubleField(eObj, fPosY) : 0.0;
                    double az = fPosZ ? env->GetDoubleField(eObj, fPosZ) : 0.0;
                    double aLastX = fLastX ? env->GetDoubleField(eObj, fLastX) : ax;
                    double aLastY = fLastY ? env->GetDoubleField(eObj, fLastY) : ay;
                    double aLastZ = fLastZ ? env->GetDoubleField(eObj, fLastZ) : az;

                    double aInterpX = aLastX + (ax - aLastX) * pt;
                    double aInterpY = aLastY + (ay - aLastY) * pt;
                    double aInterpZ = aLastZ + (az - aLastZ) * pt;

                    double dx = aInterpX - myInterpX;
                    double dy = aInterpY - myInterpY;
                    double dz = aInterpZ - myInterpZ;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);

                    if (dist <= 300.0) {
                        PlayerTagInfo aTag;
                        aTag.name = "Animal";
                        jclass cA = env->GetObjectClass(eObj);
                        if (cA) {
                            jmethodID mGetName = env->GetMethodID(cA, "h_", "()Ljava/lang/String;");
                            if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cA, "getName", "()Ljava/lang/String;"); }
                            if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cA, "func_70005_c_", "()Ljava/lang/String;"); }
                            if (mGetName) {
                                auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(eObj, mGetName));
                                if (jName && !env->ExceptionCheck()) {
                                    const char* chars = env->GetStringUTFChars(jName, nullptr);
                                    if (chars) { aTag.name = chars; env->ReleaseStringUTFChars(jName, chars); }
                                    env->DeleteLocalRef(jName);
                                } else env->ExceptionClear();
                            }
                            env->DeleteLocalRef(cA);
                        }
                        aTag.realName = aTag.name;
                        aTag.isBot = true;
                        if (mGetEntId) {
                            aTag.entId = env->CallIntMethod(eObj, mGetEntId);
                            if (env->ExceptionCheck()) { env->ExceptionClear(); aTag.entId = 0; }
                        }
                        aTag.pos = { aInterpX, aInterpY, aInterpZ };
                        aTag.width = 0.6;
                        aTag.height = fHeight ? env->GetFloatField(eObj, fHeight) : 1.4f;
                        aTag.distance = static_cast<float>(dist);
                        aTag.health = 20.0f;
                        aTag.maxHealth = 20.0f;
                        newAnimals.push_back(aTag);
                    }
                } else if (cEntityItem && env->IsInstanceOf(eObj, cEntityItem)) {
                    double ix = fPosX ? env->GetDoubleField(eObj, fPosX) : 0.0;
                    double iy = fPosY ? env->GetDoubleField(eObj, fPosY) : 0.0;
                    double iz = fPosZ ? env->GetDoubleField(eObj, fPosZ) : 0.0;
                    double iLastX = fLastX ? env->GetDoubleField(eObj, fLastX) : ix;
                    double iLastY = fLastY ? env->GetDoubleField(eObj, fLastY) : iy;
                    double iLastZ = fLastZ ? env->GetDoubleField(eObj, fLastZ) : iz;

                    double iInterpX = iLastX + (ix - iLastX) * pt;
                    double iInterpY = iLastY + (iy - iLastY) * pt;
                    double iInterpZ = iLastZ + (iz - iLastZ) * pt;

                    double dx = iInterpX - myInterpX;
                    double dy = iInterpY - myInterpY;
                    double dz = iInterpZ - myInterpZ;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);

                    if (dist <= 300.0) {
                        jobject stack = nullptr;
                        if (mGetItem) {
                            stack = env->CallObjectMethod(eObj, mGetItem);
                        } else if (fItem) {
                            stack = env->GetObjectField(eObj, fItem);
                        }

                        if (stack && !env->ExceptionCheck()) {
                            jclass cIS = env->GetObjectClass(stack);
                            jmethodID mEmpty = env->GetMethodID(cIS, "b", "()Z");
                            if (!mEmpty) { env->ExceptionClear(); mEmpty = env->GetMethodID(cIS, "isEmpty", "()Z"); }
                            if (!mEmpty) { env->ExceptionClear(); mEmpty = env->GetMethodID(cIS, "func_190926_b", "()Z"); }
                            bool empty = mEmpty ? env->CallBooleanMethod(stack, mEmpty) : false;

                            if (!empty) {
                                DroppedItemInfo dItem;
                                dItem.pos = { iInterpX, iInterpY, iInterpZ };
                                dItem.distance = static_cast<float>(dist);

                                // Количество предметов в стаке (E()I в 1.12.2)
                                jmethodID mCount = env->GetMethodID(cIS, "E", "()I");
                                if (!mCount) { env->ExceptionClear(); mCount = env->GetMethodID(cIS, "getCount", "()I"); }
                                if (!mCount) { env->ExceptionClear(); mCount = env->GetMethodID(cIS, "func_190916_E", "()I"); }
                                dItem.count = mCount ? env->CallIntMethod(stack, mCount) : 1;
                                if (dItem.count < 1) dItem.count = 1;

                                // Используем ParseItemStackHelper со встроенным обходом "sosihuy" и получением UV иконки
                                ArmorItem arm;
                                ParseItemStackHelper(env, mc, stack, arm);
                                dItem.itemId = arm.itemId;
                                dItem.name = arm.name;
                                dItem.hasIcon = arm.hasIcon;
                                dItem.minU = arm.minU;
                                dItem.maxU = arm.maxU;
                                dItem.minV = arm.minV;
                                dItem.maxV = arm.maxV;

                                // Гарантированный фоллбэк для EntityItem: если иконка не получена, ищем по itemId или имени
                                if (!dItem.hasIcon && mc) {
                                    const char* vSp = GetVanillaItemSpriteNameById(dItem.itemId);
                                    if (!vSp) vSp = GetGeneralItemSpriteName(dItem.itemId, dItem.name);
                                    if (vSp && vSp[0] != '\0') {
                                        float u1, u2, v1, v2;
                                        if (QuerySpriteByName(env, mc, vSp, u1, u2, v1, v2)) {
                                            dItem.minU = u1; dItem.maxU = u2;
                                            dItem.minV = v1; dItem.maxV = v2;
                                            dItem.hasIcon = true;
                                        }
                                    }
                                }

                                newItems.push_back(dItem);
                            }
                            env->DeleteLocalRef(cIS);
                            env->DeleteLocalRef(stack);
                        } else env->ExceptionClear();
                    }
                } else {
                    bool isStand = false;
                    if (cEntityArmorStand && env->IsInstanceOf(eObj, cEntityArmorStand)) {
                        isStand = true;
                    }

                    if (isStand) {
                        double sx = fPosX ? env->GetDoubleField(eObj, fPosX) : 0.0;
                        double sy = fPosY ? env->GetDoubleField(eObj, fPosY) : 0.0;
                        double sz = fPosZ ? env->GetDoubleField(eObj, fPosZ) : 0.0;
                        if (sx != 0.0 || sy != 0.0 || sz != 0.0) {
                            double sLastX = fLastX ? env->GetDoubleField(eObj, fLastX) : sx;
                            double sLastY = fLastY ? env->GetDoubleField(eObj, fLastY) : sy;
                            double sLastZ = fLastZ ? env->GetDoubleField(eObj, fLastZ) : sz;
                            double sInterpX = sLastX + (sx - sLastX) * pt;
                            double sInterpY = sLastY + (sy - sLastY) * pt;
                            double sInterpZ = sLastZ + (sz - sLastZ) * pt;

                            double dx = sInterpX - myInterpX;
                            double dy = sInterpY - myInterpY;
                            double dz = sInterpZ - myInterpZ;
                            double dist = sqrt(dx*dx + dy*dy + dz*dz);
                            if (dist <= 300.0) {
                                if (dist <= 64.0) {
                                    newArmorStands.push_back({ sInterpX, sInterpY, sInterpZ });
                                }

                                // 1. Проверяем экипировку ArmorStand (3D лут на земле, стойка или спящий игрок)
                                bool hasItem = false;
                                ArmorItem standArm[4];   // FEET, LEGS, CHEST, HEAD
                                ArmorItem standHeld;     // MAINHAND
                                ArmorItem standOffhand;  // OFFHAND
                                bool hasBodyArmor = false;

                                if (dist <= 64.0) {
                                    // Способ А: Прямое чтение NonNullList<ItemStack> (armorItems / handItems)
                                    static jclass s_cList = nullptr;
                                    static jmethodID s_mListGet = nullptr;
                                    if (!s_cList) {
                                        jclass localList = env->FindClass("java/util/List");
                                        if (localList) {
                                            s_cList = (jclass)env->NewGlobalRef(localList);
                                            s_mListGet = env->GetMethodID(s_cList, "get", "(I)Ljava/lang/Object;");
                                            env->DeleteLocalRef(localList);
                                        } else env->ExceptionClear();
                                    }

                                    jfieldID fArmorList = env->GetFieldID(cEntityArmorStand, "bv", "Lfi;");
                                    if (!fArmorList) { env->ExceptionClear(); fArmorList = env->GetFieldID(cEntityArmorStand, "armorItems", "Lnet/minecraft/util/NonNullList;"); }
                                    if (!fArmorList) { env->ExceptionClear(); fArmorList = env->GetFieldID(cEntityArmorStand, "field_184799_bv", "Lnet/minecraft/util/NonNullList;"); }

                                    jfieldID fHandList = env->GetFieldID(cEntityArmorStand, "bu", "Lfi;");
                                    if (!fHandList) { env->ExceptionClear(); fHandList = env->GetFieldID(cEntityArmorStand, "handItems", "Lnet/minecraft/util/NonNullList;"); }
                                    if (!fHandList) { env->ExceptionClear(); fHandList = env->GetFieldID(cEntityArmorStand, "field_184798_bu", "Lnet/minecraft/util/NonNullList;"); }

                                    if (s_cList && s_mListGet && fArmorList) {
                                        jobject armorList = env->GetObjectField(eObj, fArmorList);
                                        if (armorList && !env->ExceptionCheck()) {
                                            for (int a = 0; a < 4; ++a) {
                                                jobject stack = env->CallObjectMethod(armorList, s_mListGet, a);
                                                if (stack && !env->ExceptionCheck()) {
                                                    ParseItemStackHelper(env, mc, stack, standArm[a]);
                                                    env->DeleteLocalRef(stack);
                                                    if (!standArm[a].isEmpty) {
                                                        if (a < 3) hasBodyArmor = true;
                                                        else hasItem = true; // HEAD (3D предмет на стойке)
                                                    }
                                                } else if (env->ExceptionCheck()) env->ExceptionClear();
                                            }
                                            env->DeleteLocalRef(armorList);
                                        } else if (env->ExceptionCheck()) env->ExceptionClear();
                                    }

                                    if (s_cList && s_mListGet && fHandList) {
                                        jobject handList = env->GetObjectField(eObj, fHandList);
                                        if (handList && !env->ExceptionCheck()) {
                                            jobject mainStack = env->CallObjectMethod(handList, s_mListGet, 0);
                                            if (mainStack && !env->ExceptionCheck()) {
                                                ParseItemStackHelper(env, mc, mainStack, standHeld);
                                                env->DeleteLocalRef(mainStack);
                                                if (!standHeld.isEmpty && (standHeld.itemId > 0 || !standHeld.name.empty())) {
                                                    hasItem = true;
                                                }
                                            } else if (env->ExceptionCheck()) env->ExceptionClear();

                                            jobject offStack = env->CallObjectMethod(handList, s_mListGet, 1);
                                            if (offStack && !env->ExceptionCheck()) {
                                                ParseItemStackHelper(env, mc, offStack, standOffhand);
                                                env->DeleteLocalRef(offStack);
                                                if (!standOffhand.isEmpty && (standOffhand.itemId > 0 || !standOffhand.name.empty())) {
                                                    hasItem = true;
                                                }
                                            } else if (env->ExceptionCheck()) env->ExceptionClear();

                                            env->DeleteLocalRef(handList);
                                        } else if (env->ExceptionCheck()) env->ExceptionClear();
                                    }

                                    // Способ Б (Фоллбэк через slot method):
                                    if (!hasItem && !hasBodyArmor && cSlot && mGetSlot) {
                                        int aMap[4] = { 3, 4, 5, 1 };
                                        for (int a = 0; a < 4; ++a) {
                                            int sIdx = aMap[a];
                                            if (fStandSlots[sIdx]) {
                                                jobject slotEnum = env->GetStaticObjectField(cSlot, fStandSlots[sIdx]);
                                                if (slotEnum && !env->ExceptionCheck()) {
                                                    jobject stack = env->CallObjectMethod(eObj, mGetSlot, slotEnum);
                                                    env->DeleteLocalRef(slotEnum);
                                                    if (stack && !env->ExceptionCheck()) {
                                                        ParseItemStackHelper(env, mc, stack, standArm[a]);
                                                        env->DeleteLocalRef(stack);
                                                        if (!standArm[a].isEmpty) {
                                                            if (a < 3) hasBodyArmor = true;
                                                            else hasItem = true;
                                                        }
                                                    } else env->ExceptionClear();
                                                } else env->ExceptionClear();
                                            }
                                        }
                                        if (fStandSlots[0]) {
                                            jobject slotEnum = env->GetStaticObjectField(cSlot, fStandSlots[0]);
                                            if (slotEnum && !env->ExceptionCheck()) {
                                                jobject stack = env->CallObjectMethod(eObj, mGetSlot, slotEnum);
                                                env->DeleteLocalRef(slotEnum);
                                                if (stack && !env->ExceptionCheck()) {
                                                    ParseItemStackHelper(env, mc, stack, standHeld);
                                                    env->DeleteLocalRef(stack);
                                                    if (!standHeld.isEmpty && (standHeld.itemId > 0 || !standHeld.name.empty())) {
                                                        hasItem = true;
                                                    }
                                                } else env->ExceptionClear();
                                            } else env->ExceptionClear();
                                        }
                                    }
                                }

                                // 2. Имя стойки и теги (на серверах Rust лут на земле: "ground.armorstand", "ground", и т.д.)
                                std::string standName = "";
                                jmethodID mGetCustom = env->GetMethodID(cEntity, "bq", "()Ljava/lang/String;");
                                if (!mGetCustom) { env->ExceptionClear(); mGetCustom = env->GetMethodID(cEntity, "getCustomNameTag", "()Ljava/lang/String;"); }
                                if (!mGetCustom) { env->ExceptionClear(); mGetCustom = env->GetMethodID(cEntity, "func_95999_t", "()Ljava/lang/String;"); }
                                if (!mGetCustom) { env->ExceptionClear(); mGetCustom = env->GetMethodID(cEntity, "h_", "()Ljava/lang/String;"); }
                                if (!mGetCustom) { env->ExceptionClear(); mGetCustom = env->GetMethodID(cEntity, "getName", "()Ljava/lang/String;"); }
                                if (mGetCustom) {
                                    auto jName = reinterpret_cast<jstring>(env->CallObjectMethod(eObj, mGetCustom));
                                    if (jName && !env->ExceptionCheck()) {
                                        const char* chars = env->GetStringUTFChars(jName, nullptr);
                                        if (chars) { standName = StripColorCodes(chars); env->ReleaseStringUTFChars(jName, chars); }
                                        env->DeleteLocalRef(jName);
                                    } else env->ExceptionClear();
                                }

                                // Чтение Scoreboard Tags (Tags: ["ground.armorstand", ...])
                                std::string standTags = "";
                                jmethodID mGetTags = env->GetMethodID(cEntity, "P", "()Ljava/util/Set;");
                                if (!mGetTags) { env->ExceptionClear(); mGetTags = env->GetMethodID(cEntity, "getTags", "()Ljava/util/Set;"); }
                                if (!mGetTags) { env->ExceptionClear(); mGetTags = env->GetMethodID(cEntity, "func_184216_O", "()Ljava/util/Set;"); }
                                if (mGetTags) {
                                    jobject setTags = env->CallObjectMethod(eObj, mGetTags);
                                    if (setTags && !env->ExceptionCheck()) {
                                        jclass cSet = env->GetObjectClass(setTags);
                                        jmethodID mToArr = env->GetMethodID(cSet, "toArray", "()[Ljava/lang/Object;");
                                        if (mToArr) {
                                            jobjectArray arr = reinterpret_cast<jobjectArray>(env->CallObjectMethod(setTags, mToArr));
                                            if (arr && !env->ExceptionCheck()) {
                                                jsize tLen = env->GetArrayLength(arr);
                                                for (jsize ti = 0; ti < tLen; ++ti) {
                                                    jobject strObj = env->GetObjectArrayElement(arr, ti);
                                                    if (strObj) {
                                                        const char* tc = env->GetStringUTFChars(reinterpret_cast<jstring>(strObj), nullptr);
                                                        if (tc) {
                                                            standTags += " ";
                                                            standTags += tc;
                                                            env->ReleaseStringUTFChars(reinterpret_cast<jstring>(strObj), tc);
                                                        }
                                                        env->DeleteLocalRef(strObj);
                                                    }
                                                }
                                                env->DeleteLocalRef(arr);
                                            } else env->ExceptionClear();
                                        }
                                        env->DeleteLocalRef(cSet);
                                        env->DeleteLocalRef(setTags);
                                    } else env->ExceptionClear();
                                }

                                std::string allStandInfo = CleanAndLower(standName + " " + standTags);

                                // 3. Исключаем двери, свиней/животных, спящих игроков, трупы и объекты базы
                                bool isExcluded = false;
                                if (hasBodyArmor || !standArm[0].isEmpty || !standArm[1].isEmpty || !standArm[2].isEmpty || // Спящие игроки и трупы всегда имеют элементы брони
                                    allStandInfo.find("door") != std::string::npos || allStandInfo.find("двер") != std::string::npos ||
                                    allStandInfo.find("gate") != std::string::npos || allStandInfo.find("ворот") != std::string::npos ||
                                    allStandInfo.find("hatch") != std::string::npos || allStandInfo.find("люк") != std::string::npos ||
                                    allStandInfo.find("pig") != std::string::npos || allStandInfo.find("свин") != std::string::npos ||
                                    allStandInfo.find("boar") != std::string::npos || allStandInfo.find("кабан") != std::string::npos ||
                                    allStandInfo.find("bear") != std::string::npos || allStandInfo.find("медвед") != std::string::npos ||
                                    allStandInfo.find("wolf") != std::string::npos || allStandInfo.find("волк") != std::string::npos ||
                                    allStandInfo.find("sleep") != std::string::npos || allStandInfo.find("спящ") != std::string::npos ||
                                    allStandInfo.find("corpse") != std::string::npos || allStandInfo.find("труп") != std::string::npos ||
                                    allStandInfo.find("dead") != std::string::npos || allStandInfo.find("body") != std::string::npos ||
                                    allStandInfo.find("тело") != std::string::npos || allStandInfo.find("лежащ") != std::string::npos ||
                                    allStandInfo.find("player") != std::string::npos || allStandInfo.find("игрок") != std::string::npos) {
                                    isExcluded = true;
                                }

                                // 4. Предмет на земле: именно ground.armorstand, ground. или ground_
                                bool isGroundItem = (!isExcluded && (
                                    allStandInfo.find("ground.armorstand") != std::string::npos ||
                                    allStandInfo.find("ground.") != std::string::npos ||
                                    allStandInfo.find("ground_") != std::string::npos ||
                                    (allStandInfo.find("ground") != std::string::npos && (allStandInfo.find("item") != std::string::npos || allStandInfo.find("drop") != std::string::npos))
                                ));

                                if (isGroundItem && dist <= 64.0) {
                                    DroppedItemInfo gItem;
                                    gItem.pos = { sInterpX, sInterpY, sInterpZ };
                                    gItem.distance = static_cast<float>(dist);
                                    gItem.isGroundStand = true;

                                    const ArmorItem* bestItem = nullptr;
                                    // На серверах Rust предмет на ground.armorstand лежит на ГОЛОВЕ (slot 3) или в РУКЕ
                                    if (!standArm[3].isEmpty && (standArm[3].hasIcon || standArm[3].itemId > 0 || !standArm[3].name.empty())) {
                                        bestItem = &standArm[3]; // HEAD (3D модель предмета на голове)
                                    } else if (!standHeld.isEmpty && (standHeld.hasIcon || standHeld.itemId > 0 || !standHeld.name.empty())) {
                                        bestItem = &standHeld;   // MAINHAND
                                    } else if (!standOffhand.isEmpty && (standOffhand.hasIcon || standOffhand.itemId > 0 || !standOffhand.name.empty())) {
                                        bestItem = &standOffhand;// OFFHAND
                                    }

                                    if (bestItem) {
                                        gItem.name = bestItem->name;
                                        gItem.itemId = bestItem->itemId;
                                        gItem.count = bestItem->count > 0 ? bestItem->count : 1;
                                        gItem.hasIcon = bestItem->hasIcon;
                                        gItem.minU = bestItem->minU; gItem.maxU = bestItem->maxU;
                                        gItem.minV = bestItem->minV; gItem.maxV = bestItem->maxV;
                                    }

                                    // Если предмет не определился из слотов — определяем имя из allStandInfo (ground.armorstand.<item_name>)
                                    if (gItem.name.empty() || gItem.name == "ground" || gItem.name == "Ground Item" || gItem.name == "Item" || gItem.name == "Armor Stand") {
                                        if (allStandInfo.find("pumpkin") != std::string::npos || allStandInfo.find("тыкв") != std::string::npos) gItem.name = "Pumpkin";
                                        else if (allStandInfo.find("meat") != std::string::npos || allStandInfo.find("beef") != std::string::npos || allStandInfo.find("мясо") != std::string::npos || allStandInfo.find("говяд") != std::string::npos) gItem.name = "Meat";
                                        else if (allStandInfo.find("pork") != std::string::npos || allStandInfo.find("свинин") != std::string::npos) gItem.name = "Porkchop";
                                        else if (allStandInfo.find("bread") != std::string::npos || allStandInfo.find("хлеб") != std::string::npos) gItem.name = "Bread";
                                        else if (allStandInfo.find("apple") != std::string::npos || allStandInfo.find("яблок") != std::string::npos) gItem.name = "Apple";
                                        else if (allStandInfo.find("potato") != std::string::npos || allStandInfo.find("карто") != std::string::npos) gItem.name = "Potato";
                                        else if (allStandInfo.find("wood") != std::string::npos || allStandInfo.find("дерев") != std::string::npos || allStandInfo.find("log") != std::string::npos) gItem.name = "Wood";
                                        else if (allStandInfo.find("stone") != std::string::npos || allStandInfo.find("камен") != std::string::npos || allStandInfo.find("rock") != std::string::npos) gItem.name = "Stone";
                                        else if (allStandInfo.find("cloth") != std::string::npos || allStandInfo.find("ткань") != std::string::npos) gItem.name = "Cloth";
                                        else if (allStandInfo.find("fat") != std::string::npos || allStandInfo.find("жир") != std::string::npos) gItem.name = "Fat";
                                        else if (allStandInfo.find("bone") != std::string::npos || allStandInfo.find("кост") != std::string::npos) gItem.name = "Bone";
                                        else if (allStandInfo.find("leather") != std::string::npos || allStandInfo.find("кож") != std::string::npos) gItem.name = "Leather";
                                        else if (allStandInfo.find("fuel") != std::string::npos || allStandInfo.find("топлив") != std::string::npos || allStandInfo.find("тнк") != std::string::npos) gItem.name = "Fuel";
                                        else if (allStandInfo.find("bandage") != std::string::npos || allStandInfo.find("бинт") != std::string::npos) gItem.name = "Bandage";
                                        else if (allStandInfo.find("medkit") != std::string::npos || allStandInfo.find("аптеч") != std::string::npos || allStandInfo.find("шприц") != std::string::npos) gItem.name = "Medkit";
                                        else if (allStandInfo.find("mushroom") != std::string::npos || allStandInfo.find("гриб") != std::string::npos) gItem.name = "Mushroom";
                                        else if (allStandInfo.find("corn") != std::string::npos || allStandInfo.find("кукуруз") != std::string::npos) gItem.name = "Corn";
                                        else if (allStandInfo.find("sulfur") != std::string::npos || allStandInfo.find("сер") != std::string::npos) gItem.name = "Sulfur";
                                        else if (allStandInfo.find("metal") != std::string::npos || allStandInfo.find("желез") != std::string::npos || allStandInfo.find("iron") != std::string::npos) gItem.name = "Metal";
                                        else if (allStandInfo.find("scrap") != std::string::npos || allStandInfo.find("скрап") != std::string::npos) gItem.name = "Scrap";
                                        else if (allStandInfo.find("ak") != std::string::npos || allStandInfo.find("rifle") != std::string::npos || allStandInfo.find("кала") != std::string::npos) gItem.name = "Rifle";
                                        else if (allStandInfo.find("bow") != std::string::npos || allStandInfo.find("лук") != std::string::npos) gItem.name = "Bow";
                                        else if (allStandInfo.find("hatchet") != std::string::npos || allStandInfo.find("axe") != std::string::npos || allStandInfo.find("топор") != std::string::npos) gItem.name = "Hatchet";
                                        else if (allStandInfo.find("pickaxe") != std::string::npos || allStandInfo.find("кирк") != std::string::npos) gItem.name = "Pickaxe";
                                        else if (allStandInfo.find("sword") != std::string::npos || allStandInfo.find("меч") != std::string::npos) gItem.name = "Sword";
                                        else if (allStandInfo.find("spear") != std::string::npos || allStandInfo.find("копь") != std::string::npos) gItem.name = "Spear";
                                        else if (allStandInfo.find("spring") != std::string::npos || allStandInfo.find("пружин") != std::string::npos) gItem.name = "Spring";
                                        else if (allStandInfo.find("gear") != std::string::npos || allStandInfo.find("шестерен") != std::string::npos) gItem.name = "Gears";
                                        else if (allStandInfo.find("pipe") != std::string::npos || allStandInfo.find("труб") != std::string::npos) gItem.name = "Pipe";
                                        else if (allStandInfo.find("sheet") != std::string::npos || allStandInfo.find("лист") != std::string::npos) gItem.name = "Sheet Metal";
                                        else if (allStandInfo.find("c4") != std::string::npos || allStandInfo.find("explosive") != std::string::npos || allStandInfo.find("взрыв") != std::string::npos) gItem.name = "Explosives";
                                    }

                                    // Если у модели не было текстуры — пробуем получить 2D иконку из ресурс-пака
                                    if (!gItem.hasIcon) {
                                        float rU1, rU2, rV1, rV2;
                                        if (QueryResourcePackSprite(env, mc, gItem.itemId, gItem.name, rU1, rU2, rV1, rV2)) {
                                            gItem.minU = rU1; gItem.maxU = rU2;
                                            gItem.minV = rV1; gItem.maxV = rV2;
                                            gItem.hasIcon = true;
                                        } else {
                                            const char* gSp = GetVanillaItemSpriteNameById(gItem.itemId);
                                            if (!gSp) gSp = GetGeneralItemSpriteName(gItem.itemId, gItem.name);
                                            if (gSp && gSp[0] != '\0') {
                                                if (QuerySpriteByName(env, mc, gSp, rU1, rU2, rV1, rV2)) {
                                                    gItem.minU = rU1; gItem.maxU = rU2;
                                                    gItem.minV = rV1; gItem.maxV = rV2;
                                                    gItem.hasIcon = true;
                                                }
                                            }
                                        }
                                    }

                                    // ВАЖНО: Добавляем предмет ТОЛЬКО если у него есть валидная иконка!
                                    // Никаких пустых дистанций "5m" над спящими игроками, дверями или пустыми стойками!
                                    if (gItem.hasIcon && gItem.maxU > gItem.minU && gItem.maxV > gItem.minV) {
                                        newItems.push_back(gItem);
                                    }
                                } else if (!standName.empty() &&
                                           standName != "Armor Stand" && standName != "Стойка для брони" &&
                                           standName != "Player" && standName != "Unknown" && standName != "?") {
                                    standNameplates.push_back({ { sInterpX, sInterpY, sInterpZ }, standName });
                                }

                                // 2.5 BlockESP: распознавание рудных жил и ресурсных узлов RustEx (Ore Vein / Stone / Metal / Sulfur)
                                bool isOreNode = false;
                                std::string oreNodeName = "";
                                double rNode = 1.0, gNode = 0.85, bNode = 0.2;

                                if (!standName.empty() && standName != "Armor Stand") {
                                    std::string lName = standName;
                                    for (char& ch : lName) ch = static_cast<char>(tolower(ch));
                                    if (lName.find("ore") != std::string::npos || lName.find("vein") != std::string::npos ||
                                        lName.find("руда") != std::string::npos || lName.find("жила") != std::string::npos ||
                                        lName.find("камень") != std::string::npos || lName.find("желез") != std::string::npos ||
                                        lName.find("сера") != std::string::npos || lName.find("stone") != std::string::npos ||
                                        lName.find("sulfur") != std::string::npos || lName.find("metal") != std::string::npos) {
                                        isOreNode = true;
                                        oreNodeName = standName;
                                        if (lName.find("sulfur") != std::string::npos || lName.find("сера") != std::string::npos) {
                                            rNode = 1.0; gNode = 1.0; bNode = 0.0;
                                        } else if (lName.find("metal") != std::string::npos || lName.find("желез") != std::string::npos) {
                                            rNode = 0.95; gNode = 0.95; bNode = 0.95;
                                        } else if (lName.find("stone") != std::string::npos || lName.find("камень") != std::string::npos) {
                                            rNode = 0.75; gNode = 0.75; bNode = 0.80;
                                        } else {
                                            rNode = 1.0; gNode = 0.85; bNode = 0.2;
                                        }
                                    }
                                }

                                if (!isOreNode && !hasBodyArmor) {
                                    if (!standArm[3].isEmpty) {
                                        std::string hName = standArm[3].name;
                                        for (char& ch : hName) ch = static_cast<char>(tolower(ch));
                                        if (hName.find("ore") != std::string::npos || hName.find("руда") != std::string::npos ||
                                            hName.find("stone") != std::string::npos || hName.find("камень") != std::string::npos ||
                                            standArm[3].itemId == 1 || standArm[3].itemId == 14 || standArm[3].itemId == 15 || standArm[3].itemId == 1125) {
                                            isOreNode = true;
                                            oreNodeName = !standName.empty() ? standName : (!standArm[3].name.empty() ? standArm[3].name : "Ore Vein");
                                            rNode = 1.0; gNode = 0.85; bNode = 0.2;
                                        }
                                    }
                                }

                                if (isOreNode) {
                                    BlockESPInfo nodeInfo;
                                    nodeInfo.pos = { sInterpX, sInterpY, sInterpZ };
                                    nodeInfo.red = rNode; nodeInfo.green = gNode; nodeInfo.blue = bNode;
                                    nodeInfo.name = oreNodeName.empty() ? "Ore Vein" : oreNodeName;
                                    nodeInfo.isEntity = true;
                                    newEntityOres.push_back(nodeInfo);
                                }

                                // 2. AutoMine: поиск блоков вокруг ArmorStand (1-в-1 как в AutoMine.java)
                                if (m_autoMineEnabled && dist <= 10.0 && cBP && mInitBP && mIsAir) {
                                    int iX = static_cast<int>(floor(sInterpX));
                                    int iY = static_cast<int>(floor(sInterpY));
                                    int iZ = static_cast<int>(floor(sInterpZ));

                                    double bestBlockDistSq = 9999.0;
                                    int bestCX = 0, bestCY = 0, bestCZ = 0;
                                    bool foundBlock = false;

                                    // Обход области 4x4 по горизонтали [-2, 1]. Сначала
                                    // проверяем уровень стойки, затем блок ниже: если
                                    // центры равноудалены, цель остаётся на правильном уровне.
                                    for (int ox = -2; ox < 2; ++ox) {
                                        for (int oz = -2; oz < 2; ++oz) {
                                            for (int oy = 0; oy >= -1; --oy) {
                                                int cx = iX + ox;
                                                int cy = iY + oy;
                                                int cz = iZ + oz;

                                                jobject bp = env->NewObject(cBP, mInitBP, cx, cy, cz);
                                                if (bp && !env->ExceptionCheck()) {
                                                    bool isAir = env->CallBooleanMethod(world, mIsAir, bp);
                                                    if (env->ExceptionCheck()) { env->ExceptionClear(); isAir = true; }
                                                    env->DeleteLocalRef(bp);
                                                    if (!isAir) {
                                                        double ddx = (cx + 0.5) - sInterpX;
                                                        double ddy = (cy + 0.5) - sInterpY;
                                                        double ddz = (cz + 0.5) - sInterpZ;
                                                        double dsq = ddx*ddx + ddy*ddy + ddz*ddz;
                                                        const bool tiePrefersThisBlock = foundBlock &&
                                                            fabs(dsq - bestBlockDistSq) < 1e-9 &&
                                                            cx == iX && cy == iY && cz == iZ;
                                                        if (dsq < bestBlockDistSq - 1e-9 || tiePrefersThisBlock) {
                                                            bestBlockDistSq = dsq;
                                                            bestCX = cx; bestCY = cy; bestCZ = cz;
                                                            foundBlock = true;
                                                        }
                                                    }
                                                } else env->ExceptionClear();
                                            }
                                        }
                                    }

                                    if (foundBlock) {
                                        AutoMineTarget amt;
                                        amt.standPos = { sInterpX, sInterpY, sInterpZ };
                                        amt.blockPos = { (double)bestCX, (double)bestCY, (double)bestCZ };
                                        newMineTargets.push_back(amt);
                                    }
                                }
                            }
                        }
                    }
                }
                env->DeleteLocalRef(eObj);
            }
            env->DeleteLocalRef(cList);
            env->DeleteLocalRef(entList);
        } else env->ExceptionClear();
    }
    // Сопоставление игроков с бейджами реальных ников от стоек брони (RustEx / Spigot Bypass)
    for (auto& pl : newPlayers) {
        if (pl.distance < 0.1f) continue; // Пропускаем себя

        double bestStandDist = 999.0;
        std::string bestStandName = "";

        for (const auto& snp : standNameplates) {
            double ddx = pl.pos.x - snp.pos.x;
            double ddz = pl.pos.z - snp.pos.z;
            double horizDist = sqrt(ddx * ddx + ddz * ddz);
            double ddy = snp.pos.y - pl.pos.y;

            // На RustEx бейдж стойки брони висит над игроком (при быстром беге увеличиваем радиус до 16м)
            if (horizDist < 16.0 && ddy >= -4.0 && ddy <= 7.0) {
                std::string cand = ExtractRealPlayerName(snp.name, m_data.username);
                if (IsValidPlayerName(cand) && cand != m_data.username) {
                    double totalD = horizDist + fabs(ddy - 1.8) * 0.4;
                    if (totalD < bestStandDist) {
                        bestStandDist = totalD;
                        bestStandName = cand;
                    }
                }
            }
        }

        if (!bestStandName.empty()) {
            pl.realName = bestStandName;
            pl.name = bestStandName;
            if (pl.entId != 0) m_knownPlayerNamesByEntId[pl.entId] = bestStandName;
            if (!pl.uuidStr.empty()) m_knownPlayerNamesByUUID[pl.uuidStr] = bestStandName;
        }

        // Если ник всё ещё "Player", достаём из постоянной памяти по entId или UUID
        if (pl.realName == "Player" || pl.name == "Player" || !IsValidPlayerName(pl.realName)) {
            if (pl.entId != 0) {
                auto itE = m_knownPlayerNamesByEntId.find(pl.entId);
                if (itE != m_knownPlayerNamesByEntId.end() && IsValidPlayerName(itE->second)) {
                    pl.realName = itE->second;
                    pl.name = itE->second;
                }
            }
            if ((pl.realName == "Player" || pl.name == "Player") && !pl.uuidStr.empty()) {
                auto itU = m_knownPlayerNamesByUUID.find(pl.uuidStr);
                if (itU != m_knownPlayerNamesByUUID.end() && IsValidPlayerName(itU->second)) {
                    pl.realName = itU->second;
                    pl.name = itU->second;
                }
                if (pl.realName == "Player" || pl.name == "Player") {
                    auto itOff = m_offlineTabNames.find(pl.uuidStr);
                    if (itOff != m_offlineTabNames.end() && IsValidPlayerName(itOff->second)) {
                        pl.realName = itOff->second;
                        pl.name = itOff->second;
                    }
                }
            }
        }
    }

    m_data.droppedItems = std::move(newItems);
    m_data.players = std::move(newPlayers);
    m_data.mobs = std::move(newMobs);
    m_data.animals = std::move(newAnimals);
    m_data.armorStands = std::move(newArmorStands);
    m_data.mineTargets = std::move(newMineTargets);
    m_data.entityOreBlocks = std::move(newEntityOres);

    if (cConn) env->DeleteLocalRef(cConn);
    if (conn) env->DeleteLocalRef(conn);
    if (cSlot) env->DeleteLocalRef(cSlot);
    if (cBP) env->DeleteLocalRef(cBP);
    if (cEntityArmorStand) env->DeleteLocalRef(cEntityArmorStand);
    if (cEntityItem) env->DeleteLocalRef(cEntityItem);
    if (cEntityPlayer) env->DeleteLocalRef(cEntityPlayer);
    if (cAbstractPlayer) env->DeleteLocalRef(cAbstractPlayer);
    if (cOtherPlayer) env->DeleteLocalRef(cOtherPlayer);
    if (cEntityMob) env->DeleteLocalRef(cEntityMob);
    if (cEntityAnimal) env->DeleteLocalRef(cEntityAnimal);
    if (cEntity) env->DeleteLocalRef(cEntity);
    if (cl) env->DeleteLocalRef(cl);
    env->DeleteLocalRef(cWorld);
    env->DeleteLocalRef(world);
    env->DeleteLocalRef(localPlayer);
    env->DeleteLocalRef(mc);
}

// ── Сканирование руд и растений для BlockESP (1-в-1 как в BlockESP.java) ────
void JVMBridge::ScanBlockESP(JNIEnv* env, float range, bool scanOres, bool scanFood) {
    ScanBlockESPParallel(env, range, scanOres, scanFood, 0, 1);
}

// ── Многопоточное сканирование руд и растений для BlockESP (задействует все ядра CPU) ────
struct ChunkKey {
    int cx;
    int cz;
    bool operator==(const ChunkKey& o) const { return cx == o.cx && cz == o.cz; }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return (static_cast<size_t>(k.cx) * 31) ^ static_cast<size_t>(k.cz);
    }
};

static std::mutex s_chunkCacheMutex;
static std::unordered_map<ChunkKey, std::pair<std::chrono::steady_clock::time_point, std::vector<BlockESPInfo>>, ChunkKeyHash> s_chunkCache;

void JVMBridge::ScanBlockESPParallel(JNIEnv* env, float range, bool scanOres, bool scanFood, unsigned int threadIndex, unsigned int totalThreads) {
    if (!env || !m_ok || !m_cMC || (!scanOres && !scanFood)) {
        std::lock_guard<std::mutex> lock(m_blockEspMutex);
        if (threadIndex < 16) m_workerBlocks[threadIndex].clear();
        return;
    }

    if (env->PushLocalFrame(64) != 0) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); env->PopLocalFrame(nullptr); return; }

    jclass cMC = env->GetObjectClass(mc);
    jfieldID fWorld = env->GetFieldID(cMC, "f", "Lbsb;");
    if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "world", "Lnet/minecraft/client/multiplayer/WorldClient;"); }
    if (!fWorld) { env->ExceptionClear(); fWorld = env->GetFieldID(cMC, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;"); }
    jobject world = fWorld ? env->GetObjectField(mc, fWorld) : nullptr;
    if (!world || env->ExceptionCheck()) { env->ExceptionClear(); env->PopLocalFrame(nullptr); return; }

    double px = (m_data.playerPos.x != 0.0) ? m_data.playerPos.x : m_data.camPos.x;
    double py = (m_data.playerPos.y != 0.0) ? m_data.playerPos.y : m_data.camPos.y;
    double pz = (m_data.playerPos.z != 0.0) ? m_data.playerPos.z : m_data.camPos.z;

    jclass cWorld = env->GetObjectClass(world);
    jobject cl = m_classLoader ? m_classLoader : GetMinecraftClassLoader(env);

    jmethodID mGetCP = env->GetMethodID(cWorld, "f", "()Lbrx;");
    if (!mGetCP) { env->ExceptionClear(); mGetCP = env->GetMethodID(cWorld, "getChunkProvider", "()Lnet/minecraft/client/multiplayer/ChunkProviderClient;"); }
    if (!mGetCP) { env->ExceptionClear(); mGetCP = env->GetMethodID(cWorld, "func_72863_F", "()Lnet/minecraft/client/multiplayer/ChunkProviderClient;"); }
    jobject chunkProvider = mGetCP ? env->CallObjectMethod(world, mGetCP) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); chunkProvider = nullptr; }

    if (!chunkProvider) {
        jfieldID fCP = env->GetFieldID(cWorld, "c", "Lbrx;");
        if (!fCP) { env->ExceptionClear(); fCP = env->GetFieldID(cWorld, "clientChunkProvider", "Lnet/minecraft/client/multiplayer/ChunkProviderClient;"); }
        if (fCP) {
            chunkProvider = env->GetObjectField(world, fCP);
            if (env->ExceptionCheck()) { env->ExceptionClear(); chunkProvider = nullptr; }
        }
    }

    jmethodID mGetLoadedChunk = nullptr;
    if (chunkProvider) {
        jclass cCP = env->GetObjectClass(chunkProvider);
        mGetLoadedChunk = env->GetMethodID(cCP, "a", "(II)Laxw;");
        if (!mGetLoadedChunk) { env->ExceptionClear(); mGetLoadedChunk = env->GetMethodID(cCP, "func_186026_b", "(II)Laxw;"); }
        if (!mGetLoadedChunk) { env->ExceptionClear(); mGetLoadedChunk = env->GetMethodID(cCP, "getLoadedChunk", "(II)Laxw;"); }
        if (!mGetLoadedChunk) { env->ExceptionClear(); mGetLoadedChunk = env->GetMethodID(cCP, "a", "(II)Lnet/minecraft/world/chunk/Chunk;"); }
        if (!mGetLoadedChunk) { env->ExceptionClear(); mGetLoadedChunk = env->GetMethodID(cCP, "func_186026_b", "(II)Lnet/minecraft/world/chunk/Chunk;"); }
        if (!mGetLoadedChunk) { env->ExceptionClear(); mGetLoadedChunk = env->GetMethodID(cCP, "getLoadedChunk", "(II)Lnet/minecraft/world/chunk/Chunk;"); }
        if (!mGetLoadedChunk) { env->ExceptionClear(); mGetLoadedChunk = env->GetMethodID(cCP, "c", "(II)Laxw;"); }
        env->DeleteLocalRef(cCP);
    }

    if (!mGetLoadedChunk) {
        env->ExceptionClear();
        if (chunkProvider) env->DeleteLocalRef(chunkProvider);
        env->PopLocalFrame(nullptr);
        return;
    }

    jclass cChunk = FindMCClass(env, cl, "axw");
    if (!cChunk) cChunk = FindMCClass(env, cl, "net/minecraft/world/chunk/Chunk");
    if (!cChunk) {
        env->ExceptionClear();
        if (chunkProvider) env->DeleteLocalRef(chunkProvider);
        env->PopLocalFrame(nullptr);
        return;
    }

    jmethodID mChunkEmpty = env->GetMethodID(cChunk, "f", "()Z");
    if (!mChunkEmpty) { env->ExceptionClear(); mChunkEmpty = env->GetMethodID(cChunk, "b", "()Z"); }
    if (!mChunkEmpty) { env->ExceptionClear(); mChunkEmpty = env->GetMethodID(cChunk, "isEmpty", "()Z"); }

    jfieldID fStorage = env->GetFieldID(cChunk, "f", "[Laxx;");
    if (!fStorage) { env->ExceptionClear(); fStorage = env->GetFieldID(cChunk, "h", "[Laxx;"); }
    if (!fStorage) { env->ExceptionClear(); fStorage = env->GetFieldID(cChunk, "storageArrays", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"); }
    if (!fStorage) { env->ExceptionClear(); fStorage = env->GetFieldID(cChunk, "field_76652_q", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"); }
    if (!fStorage) { env->ExceptionClear(); fStorage = env->GetFieldID(cChunk, "d", "[Laxx;"); }
    if (!fStorage) { env->ExceptionClear(); fStorage = env->GetFieldID(cChunk, "a", "[Laxx;"); }

    jmethodID mGetStorage = env->GetMethodID(cChunk, "h", "()[Laxx;");
    if (!mGetStorage) { env->ExceptionClear(); mGetStorage = env->GetMethodID(cChunk, "f", "()[Laxx;"); }
    if (!mGetStorage) { env->ExceptionClear(); mGetStorage = env->GetMethodID(cChunk, "a", "()[Laxx;"); }
    if (!mGetStorage) { env->ExceptionClear(); mGetStorage = env->GetMethodID(cChunk, "d", "()[Laxx;"); }
    if (!mGetStorage) { env->ExceptionClear(); mGetStorage = env->GetMethodID(cChunk, "getBlockStorageArray", "()[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"); }
    if (!mGetStorage) { env->ExceptionClear(); mGetStorage = env->GetMethodID(cChunk, "func_76587_i", "()[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;"); }

    jclass cEBS = FindMCClass(env, cl, "axx");
    if (!cEBS) cEBS = FindMCClass(env, cl, "net/minecraft/world/chunk/storage/ExtendedBlockStorage");

    jmethodID mIsEmpty = cEBS ? env->GetMethodID(cEBS, "a", "()Z") : nullptr;
    if (!mIsEmpty && cEBS) { env->ExceptionClear(); mIsEmpty = env->GetMethodID(cEBS, "isEmpty", "()Z"); }

    jmethodID mGetState = cEBS ? env->GetMethodID(cEBS, "a", "(III)Lawt;") : nullptr;
    if (!mGetState && cEBS) { env->ExceptionClear(); mGetState = env->GetMethodID(cEBS, "get", "(III)Lnet/minecraft/block/state/IBlockState;"); }
    if (!mGetState && cEBS) { env->ExceptionClear(); mGetState = env->GetMethodID(cEBS, "func_177485_a", "(III)Lnet/minecraft/block/state/IBlockState;"); }

    jclass cState = FindMCClass(env, cl, "awt");
    if (!cState) cState = FindMCClass(env, cl, "net/minecraft/block/state/IBlockState");

    jmethodID mGetBlock = cState ? env->GetMethodID(cState, "u", "()Laow;") : nullptr;
    if (!mGetBlock && cState) { env->ExceptionClear(); mGetBlock = env->GetMethodID(cState, "u", "()Laou;"); }
    if (!mGetBlock && cState) { env->ExceptionClear(); mGetBlock = env->GetMethodID(cState, "c", "()Laow;"); }
    if (!mGetBlock && cState) { env->ExceptionClear(); mGetBlock = env->GetMethodID(cState, "d", "()Laow;"); }
    if (!mGetBlock && cState) { env->ExceptionClear(); mGetBlock = env->GetMethodID(cState, "getBlock", "()Lnet/minecraft/block/Block;"); }
    if (!mGetBlock && cState) { env->ExceptionClear(); mGetBlock = env->GetMethodID(cState, "func_177230_c", "()Lnet/minecraft/block/Block;"); }

    jclass cBlock = FindMCClass(env, cl, "aow");
    if (!cBlock) cBlock = FindMCClass(env, cl, "aou");
    if (!cBlock) cBlock = FindMCClass(env, cl, "net/minecraft/block/Block");

    jmethodID mGetId = cBlock ? env->GetStaticMethodID(cBlock, "a", "(Laow;)I") : nullptr;
    if (!mGetId && cBlock) { env->ExceptionClear(); mGetId = env->GetStaticMethodID(cBlock, "a", "(Laou;)I"); }
    if (!mGetId && cBlock) { env->ExceptionClear(); mGetId = env->GetStaticMethodID(cBlock, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I"); }
    if (!mGetId && cBlock) { env->ExceptionClear(); mGetId = env->GetStaticMethodID(cBlock, "func_149682_b", "(Lnet/minecraft/block/Block;)I"); }

    jmethodID mGetMeta = (cBlock && cState) ? env->GetMethodID(cBlock, "e", "(Lawt;)I") : nullptr;
    if (!mGetMeta && cBlock && cState) { env->ExceptionClear(); mGetMeta = env->GetMethodID(cBlock, "d", "(Lawt;)I"); }
    if (!mGetMeta && cBlock && cState) { env->ExceptionClear(); mGetMeta = env->GetMethodID(cBlock, "c", "(Lawt;)I"); }
    if (!mGetMeta && cBlock && cState) { env->ExceptionClear(); mGetMeta = env->GetMethodID(cBlock, "getMetaFromState", "(Lnet/minecraft/block/state/IBlockState;)I"); }
    if (!mGetMeta && cBlock && cState) { env->ExceptionClear(); mGetMeta = env->GetMethodID(cBlock, "func_176201_c", "(Lnet/minecraft/block/state/IBlockState;)I"); }

    if ((!fStorage && !mGetStorage) || !cEBS || !mGetState || !mGetBlock || !mGetId) {
        env->ExceptionClear();
        if (chunkProvider) env->DeleteLocalRef(chunkProvider);
        env->PopLocalFrame(nullptr);
        return;
    }

    int r = static_cast<int>(std::min(range, 700.0f));
    float rangeSq = (float)r * (float)r;

    int chunkMinX = ((int)px - r) >> 4;
    int chunkMaxX = ((int)px + r) >> 4;
    int chunkMinZ = ((int)pz - r) >> 4;
    int chunkMaxZ = ((int)pz + r) >> 4;
    int minY = std::max(0, (int)py - r);
    int maxY = std::min(255, (int)py + r);

    std::vector<std::pair<int, int>> chunksToScan;
    chunksToScan.reserve(150);
    for (int cx = chunkMinX; cx <= chunkMaxX; ++cx) {
        for (int cz = chunkMinZ; cz <= chunkMaxZ; ++cz) {
            chunksToScan.push_back({cx, cz});
        }
    }

    std::vector<BlockESPInfo> threadFound;
    threadFound.reserve(64);

    auto nowTime = std::chrono::steady_clock::now();

    for (size_t i = threadIndex; i < chunksToScan.size() && threadFound.size() < 1000; i += totalThreads) {
        int cx = chunksToScan[i].first;
        int cz = chunksToScan[i].second;

        // Быстрая проверка кэша чанка (0 JNI вызовов!)
        bool fromCache = false;
        {
            std::lock_guard<std::mutex> clock(s_chunkCacheMutex);
            auto it = s_chunkCache.find({cx, cz});
            if (it != s_chunkCache.end()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(nowTime - it->second.first).count();
                if (elapsed < 4) {
                    threadFound.insert(threadFound.end(), it->second.second.begin(), it->second.second.end());
                    fromCache = true;
                }
            }
        }
        if (fromCache) continue;

        jobject chunk = env->CallObjectMethod(chunkProvider, mGetLoadedChunk, cx, cz);
        if (env->ExceptionCheck()) { env->ExceptionClear(); chunk = nullptr; }
        if (!chunk) continue;

        if (mChunkEmpty && env->CallBooleanMethod(chunk, mChunkEmpty)) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(chunk);
            continue;
        }

        jobjectArray storageArr = nullptr;
        if (fStorage) {
            storageArr = reinterpret_cast<jobjectArray>(env->GetObjectField(chunk, fStorage));
            if (env->ExceptionCheck()) { env->ExceptionClear(); storageArr = nullptr; }
        }
        if (!storageArr && mGetStorage) {
            storageArr = reinterpret_cast<jobjectArray>(env->CallObjectMethod(chunk, mGetStorage));
            if (env->ExceptionCheck()) { env->ExceptionClear(); storageArr = nullptr; }
        }

        if (!storageArr) {
            env->DeleteLocalRef(chunk);
            continue;
        }

        std::vector<BlockESPInfo> chunkBlocks;
        int blockMinX = cx << 4;
        int blockMinZ = cz << 4;

        int sMin = std::max(0, minY >> 4);
        int sMax = std::min(15, maxY >> 4);

        for (int sIdx = sMin; sIdx <= sMax; ++sIdx) {
            int yBase = sIdx << 4;
            if (yBase + 15 < minY || yBase > maxY) continue;

            jobject storage = env->GetObjectArrayElement(storageArr, sIdx);
            if (!storage || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

            if (mIsEmpty && env->CallBooleanMethod(storage, mIsEmpty)) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(storage);
                continue;
            }

            if (env->PushLocalFrame(64) != 0) {
                env->DeleteLocalRef(storage);
                continue;
            }

            // Полное точное сканирование каждого блока жилы руд
            for (int lx = 0; lx < 16; ++lx) {
                int wx = blockMinX + lx;
                double dx = wx - px;
                for (int lz = 0; lz < 16; ++lz) {
                    int wz = blockMinZ + lz;
                    double dz = wz - pz;
                    if (dx * dx + dz * dz > rangeSq) continue;

                    for (int ly = 0; ly < 16; ++ly) {
                        int wy = yBase + ly;
                        if (wy < minY || wy > maxY) continue;
                        double dy = wy - py;
                        if (dx * dx + dy * dy + dz * dz > rangeSq) continue;

                        jobject state = env->CallObjectMethod(storage, mGetState, lx, ly, lz);
                        if (!state || env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                        jobject block = env->CallObjectMethod(state, mGetBlock);
                        if (!block || env->ExceptionCheck()) {
                            env->ExceptionClear();
                            env->DeleteLocalRef(state);
                            continue;
                        }

                        int blockId = env->CallStaticIntMethod(cBlock, mGetId, block);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            env->DeleteLocalRef(block);
                            env->DeleteLocalRef(state);
                            continue;
                        }

                        bool isTarget = false;
                        if (scanOres && (blockId == 56 || blockId == 15 || blockId == 14 || blockId == 129)) isTarget = true;
                        if (scanFood && (blockId == 6 || blockId == 175)) isTarget = true;

                        if (!isTarget) {
                            env->DeleteLocalRef(block);
                            env->DeleteLocalRef(state);
                            continue;
                        }

                        double rCol = -1.0, gCol = -1.0, bCol = -1.0;
                        std::string name;

                        if (scanOres) {
                            if (blockId == 56) {
                                rCol = 0.0; gCol = 1.0; bCol = 1.0; name = "Diamond Ore";
                            } else if (blockId == 15) {
                                rCol = 1.0; gCol = 1.0; bCol = 1.0; name = "Iron Ore";
                            } else if (blockId == 14) {
                                rCol = 1.0; gCol = 1.0; bCol = 0.0; name = "Gold Ore";
                            } else if (blockId == 129) {
                                rCol = 0.0; gCol = 1.0; bCol = 0.3; name = "Emerald Ore";
                            }
                        }

                        if (scanFood && rCol < 0.0) {
                            if (blockId == 6) {
                                int meta = mGetMeta ? env->CallIntMethod(block, mGetMeta, state) : 0;
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                int type = meta & 7;
                                if (type == 3) {
                                    rCol = 1.0; gCol = 0.0; bCol = 0.0; name = "Jungle";
                                } else if (type == 5) {
                                    rCol = 1.0; gCol = 1.0; bCol = 0.0; name = "Dark Oak";
                                }
                            } else if (blockId == 175) {
                                int meta = mGetMeta ? env->CallIntMethod(block, mGetMeta, state) : 0;
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                if ((meta & 7) == 5 && meta < 8) {
                                    rCol = 0.0; gCol = 1.0; bCol = 0.0; name = "Peony";
                                }
                            }
                        }

                        if (rCol >= 0.0) {
                            bool outside = false;
                            if (wy >= 255) {
                                outside = true;
                            } else if (ly < 15) {
                                jobject upState = env->CallObjectMethod(storage, mGetState, lx, ly + 1, lz);
                                if (upState && !env->ExceptionCheck()) {
                                    jobject upBlock = env->CallObjectMethod(upState, mGetBlock);
                                    if (upBlock && !env->ExceptionCheck()) {
                                        int upId = env->CallStaticIntMethod(cBlock, mGetId, upBlock);
                                        if (env->ExceptionCheck()) { env->ExceptionClear(); upId = -1; }
                                        outside = (upId == 0 || upId == 18 || upId == 161 || upId == 31 || upId == 175 || upId == 78 || upId == 106);
                                        env->DeleteLocalRef(upBlock);
                                    } else { env->ExceptionClear(); }
                                    env->DeleteLocalRef(upState);
                                } else { env->ExceptionClear(); }
                            } else {
                                if (sIdx + 1 >= 16) {
                                    outside = true;
                                } else {
                                    jobject nextStorage = env->GetObjectArrayElement(storageArr, sIdx + 1);
                                    if (!nextStorage || env->ExceptionCheck()) {
                                        env->ExceptionClear();
                                        outside = true;
                                    } else {
                                        if (mIsEmpty && env->CallBooleanMethod(nextStorage, mIsEmpty)) {
                                            if (env->ExceptionCheck()) env->ExceptionClear();
                                            outside = true;
                                        } else {
                                            jobject upState = env->CallObjectMethod(nextStorage, mGetState, lx, 0, lz);
                                            if (upState && !env->ExceptionCheck()) {
                                                jobject upBlock = env->CallObjectMethod(upState, mGetBlock);
                                                if (upBlock && !env->ExceptionCheck()) {
                                                    int upId = env->CallStaticIntMethod(cBlock, mGetId, upBlock);
                                                    if (env->ExceptionCheck()) { env->ExceptionClear(); upId = -1; }
                                                    outside = (upId == 0 || upId == 18 || upId == 161 || upId == 31 || upId == 175 || upId == 78 || upId == 106);
                                                    env->DeleteLocalRef(upBlock);
                                                } else { env->ExceptionClear(); }
                                                env->DeleteLocalRef(upState);
                                            } else { env->ExceptionClear(); }
                                        }
                                        env->DeleteLocalRef(nextStorage);
                                    }
                                }
                            }

                            if (outside) {
                                BlockESPInfo bInfo;
                                bInfo.pos = { (double)wx, (double)wy, (double)wz };
                                bInfo.red = rCol;
                                bInfo.green = gCol;
                                bInfo.blue = bCol;
                                bInfo.color = IM_COL32((int)(rCol * 255), (int)(gCol * 255), (int)(bCol * 255), 240);
                                bInfo.name = name;
                                chunkBlocks.push_back(bInfo);
                            }
                        }

                        env->DeleteLocalRef(block);
                        env->DeleteLocalRef(state);
                    }
                }
            }
            env->PopLocalFrame(nullptr);
            env->DeleteLocalRef(storage);
        }
        env->DeleteLocalRef(storageArr);
        env->DeleteLocalRef(chunk);

        {
            std::lock_guard<std::mutex> clock(s_chunkCacheMutex);
            s_chunkCache[{cx, cz}] = { nowTime, chunkBlocks };
            if (s_chunkCache.size() > 10000) s_chunkCache.clear();
        }
        threadFound.insert(threadFound.end(), chunkBlocks.begin(), chunkBlocks.end());
    }

    if (chunkProvider) env->DeleteLocalRef(chunkProvider);

    {
        std::lock_guard<std::mutex> lock(m_blockEspMutex);
        if (threadIndex < 16) {
            m_workerBlocks[threadIndex] = std::move(threadFound);
        }
    }

    env->PopLocalFrame(nullptr);
}

void JVMBridge::RenderNativeItemESP(float range, float iconSize) {
    (void)range; (void)iconSize;
}

void JVMBridge::RenderNativeArmorHUD(int yOffset) {
    (void)yOffset;
}

// ── Управление вращением игрока (AimBot, AutoMine) ───────────────────────────
void JVMBridge::SetPlayerRotation(float yaw, float pitch) {
    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return;

    if (env->PushLocalFrame(32) != 0) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); env->PopLocalFrame(nullptr); return; }

    jclass cMC = env->GetObjectClass(mc);
    const char* playerFields[] = { "h", "player", "field_71439_g" };
    jfieldID fPlayer = nullptr;
    for (const char* pf : playerFields) {
        fPlayer = env->GetFieldID(cMC, pf, "Lbud;");
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, pf, "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, pf, "Laed;"); }
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, pf, "Lnet/minecraft/entity/player/EntityPlayer;"); }
        if (fPlayer) break;
    }

    if (fPlayer) {
        jobject player = env->GetObjectField(mc, fPlayer);
        if (player && !env->ExceptionCheck()) {
            jclass cPlayer = env->GetObjectClass(player);

            jfieldID fYaw = env->GetFieldID(cPlayer, "v", "F");
            if (!fYaw) { env->ExceptionClear(); fYaw = env->GetFieldID(cPlayer, "rotationYaw", "F"); }
            if (!fYaw) { env->ExceptionClear(); fYaw = env->GetFieldID(cPlayer, "field_70177_z", "F"); }

            jfieldID fPitch = env->GetFieldID(cPlayer, "w", "F");
            if (!fPitch) { env->ExceptionClear(); fPitch = env->GetFieldID(cPlayer, "rotationPitch", "F"); }
            if (!fPitch) { env->ExceptionClear(); fPitch = env->GetFieldID(cPlayer, "field_70125_A", "F"); }

            jfieldID fPrevYaw = env->GetFieldID(cPlayer, "x", "F");
            if (!fPrevYaw) { env->ExceptionClear(); fPrevYaw = env->GetFieldID(cPlayer, "prevRotationYaw", "F"); }
            if (!fPrevYaw) { env->ExceptionClear(); fPrevYaw = env->GetFieldID(cPlayer, "field_70126_B", "F"); }

            jfieldID fPrevPitch = env->GetFieldID(cPlayer, "y", "F");
            if (!fPrevPitch) { env->ExceptionClear(); fPrevPitch = env->GetFieldID(cPlayer, "prevRotationPitch", "F"); }
            if (!fPrevPitch) { env->ExceptionClear(); fPrevPitch = env->GetFieldID(cPlayer, "field_70127_C", "F"); }

            if (fYaw && fPitch) {
                // Синхронизируем обе стороны интерполяции. Иначе камера
                // рендерит смесь старого prevRotation и нового угла, а AimBot
                // продолжает рассчитывать следующую поправку от этой смеси.
                env->SetFloatField(player, fYaw, yaw);
                env->SetFloatField(player, fPitch, pitch);
                if (fPrevYaw) env->SetFloatField(player, fPrevYaw, yaw);
                if (fPrevPitch) env->SetFloatField(player, fPrevPitch, pitch);
                // Keep the render-side sample in sync between JVMBridge ticks;
                // otherwise every overlay frame applies its correction from
                // the same stale camera angle and produces oscillation.
                m_data.camYaw = yaw;
                m_data.camPitch = pitch;

                // EntityLivingBase.rotationYawHead (1.12.2: vp.aP / rotationYawHead / field_70759_as)
                jfieldID fYawHead = env->GetFieldID(cPlayer, "aP", "F");
                if (!fYawHead) { env->ExceptionClear(); fYawHead = env->GetFieldID(cPlayer, "rotationYawHead", "F"); }
                if (!fYawHead) { env->ExceptionClear(); fYawHead = env->GetFieldID(cPlayer, "field_70759_as", "F"); }
                if (fYawHead) env->SetFloatField(player, fYawHead, yaw);

                jfieldID fPrevYawHead = env->GetFieldID(cPlayer, "aQ", "F");
                if (!fPrevYawHead) { env->ExceptionClear(); fPrevYawHead = env->GetFieldID(cPlayer, "prevRotationYawHead", "F"); }
                if (!fPrevYawHead) { env->ExceptionClear(); fPrevYawHead = env->GetFieldID(cPlayer, "field_70758_at", "F"); }
                if (fPrevYawHead) env->SetFloatField(player, fPrevYawHead, yaw);

                jfieldID fRenderYawOffset = env->GetFieldID(cPlayer, "aN", "F");
                if (!fRenderYawOffset) { env->ExceptionClear(); fRenderYawOffset = env->GetFieldID(cPlayer, "renderYawOffset", "F"); }
                if (!fRenderYawOffset) { env->ExceptionClear(); fRenderYawOffset = env->GetFieldID(cPlayer, "field_70761_aq", "F"); }
                if (fRenderYawOffset) env->SetFloatField(player, fRenderYawOffset, yaw);

                jfieldID fPrevRenderYawOffset = env->GetFieldID(cPlayer, "aO", "F");
                if (!fPrevRenderYawOffset) { env->ExceptionClear(); fPrevRenderYawOffset = env->GetFieldID(cPlayer, "prevRenderYawOffset", "F"); }
                if (!fPrevRenderYawOffset) { env->ExceptionClear(); fPrevRenderYawOffset = env->GetFieldID(cPlayer, "field_70760_ar", "F"); }
                if (fPrevRenderYawOffset) env->SetFloatField(player, fPrevRenderYawOffset, yaw);

                // Keep RenderManager's camera-facing values in sync as well.
                jobject cl = GetMinecraftClassLoader(env);
                jclass cRM = FindMCClass(env, cl, "bzf");
                if (!cRM) cRM = FindMCClass(env, cl, "net/minecraft/client/renderer/entity/RenderManager");
                jfieldID fRM = env->GetFieldID(cMC, "aa", "Lbzf;");
                if (!fRM) { env->ExceptionClear(); fRM = env->GetFieldID(cMC, "renderManager", "Lnet/minecraft/client/renderer/entity/RenderManager;"); }
                if (!fRM) { env->ExceptionClear(); fRM = env->GetFieldID(cMC, "field_175616_W", "Lnet/minecraft/client/renderer/entity/RenderManager;"); }
                if (fRM && cRM) {
                    jobject rmObj = env->GetObjectField(mc, fRM);
                    if (rmObj && !env->ExceptionCheck()) {
                        jfieldID fRMYaw = env->GetFieldID(cRM, "f", "F");
                        if (!fRMYaw) { env->ExceptionClear(); fRMYaw = env->GetFieldID(cRM, "playerViewY", "F"); }
                        if (!fRMYaw) { env->ExceptionClear(); fRMYaw = env->GetFieldID(cRM, "field_78735_i", "F"); }
                        jfieldID fRMPitch = env->GetFieldID(cRM, "e", "F");
                        if (!fRMPitch) { env->ExceptionClear(); fRMPitch = env->GetFieldID(cRM, "playerViewX", "F"); }
                        if (!fRMPitch) { env->ExceptionClear(); fRMPitch = env->GetFieldID(cRM, "field_78732_a", "F"); }
                        if (fRMYaw) env->SetFloatField(rmObj, fRMYaw, yaw);
                        if (fRMPitch) env->SetFloatField(rmObj, fRMPitch, pitch);
                        env->DeleteLocalRef(rmObj);
                    } else if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                    }
                }
                if (cRM) env->DeleteLocalRef(cRM);
                if (cl) env->DeleteLocalRef(cl);

            }
            env->DeleteLocalRef(cPlayer);
            env->DeleteLocalRef(player);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);
    env->PopLocalFrame(nullptr);
}

// ── Взмах рукой (AutoMine, AimBot AutoShoot) ──────────────────────────────────
void JVMBridge::PlayerSwingArm() {
    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return;

    if (env->PushLocalFrame(32) != 0) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    jclass cMC = env->GetObjectClass(mc);
    const char* playerFields[] = { "h", "player", "field_71439_g" };
    jfieldID fPlayer = nullptr;
    for (const char* pf : playerFields) {
        fPlayer = env->GetFieldID(cMC, pf, "Lbud;");
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, pf, "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, pf, "Laed;"); }
        if (!fPlayer) { env->ExceptionClear(); fPlayer = env->GetFieldID(cMC, pf, "Lnet/minecraft/entity/player/EntityPlayer;"); }
        if (fPlayer) break;
    }

    if (fPlayer) {
        jobject player = env->GetObjectField(mc, fPlayer);
        if (player && !env->ExceptionCheck()) {
            jclass cPlayer = env->GetObjectClass(player);
            jobject cl = GetMinecraftClassLoader(env);

            // 1. Ванильный вызов player.swingArm(EnumHand.MAIN_HAND) -> bud.a(Lub;)V
            // Это штатный клиентский метод, который запускает анимацию И шлет пакет без обхода Netty
            jclass cHand = FindMCClass(env, cl, "ub");
            if (!cHand) cHand = FindMCClass(env, cl, "net/minecraft/util/EnumHand");
            jfieldID fMainHand = cHand ? env->GetStaticFieldID(cHand, "a", "Lub;") : nullptr;
            if (!fMainHand && cHand) { env->ExceptionClear(); fMainHand = env->GetStaticFieldID(cHand, "MAIN_HAND", "Lnet/minecraft/util/EnumHand;"); }
            jobject mainHand = (cHand && fMainHand) ? env->GetStaticObjectField(cHand, fMainHand) : nullptr;
            if (env->ExceptionCheck()) { env->ExceptionClear(); mainHand = nullptr; }

            bool swung = false;
            if (mainHand) {
                jmethodID mSwing = env->GetMethodID(cPlayer, "a", "(Lub;)V");
                if (!mSwing) { env->ExceptionClear(); mSwing = env->GetMethodID(cPlayer, "swingArm", "(Lnet/minecraft/util/EnumHand;)V"); }
                if (!mSwing) { env->ExceptionClear(); mSwing = env->GetMethodID(cPlayer, "func_184609_a", "(Lnet/minecraft/util/EnumHand;)V"); }
                if (mSwing) {
                    env->CallVoidMethod(player, mSwing, mainHand);
                    if (!env->ExceptionCheck()) swung = true;
                    else env->ExceptionClear();
                }
                env->DeleteLocalRef(mainHand);
            }
            if (cHand) env->DeleteLocalRef(cHand);

            // Фолбэк визуальной анимации на случай если метод не найден
            if (!swung) {
                const char* swingProgFields[] = { "aA", "aE", "field_82175_bq", "isSwingInProgress" };
                for (const char* fn : swingProgFields) {
                    jfieldID fld = env->GetFieldID(cPlayer, fn, "Z");
                    if (fld) {
                        env->SetBooleanField(player, fld, JNI_TRUE);
                        break;
                    }
                    env->ExceptionClear();
                }

                const char* swingIntFields[] = { "aB", "av", "field_110158_av", "swingProgressInt" };
                for (const char* fn : swingIntFields) {
                    jfieldID fld = env->GetFieldID(cPlayer, fn, "I");
                    if (fld) {
                        env->SetIntField(player, fld, 0);
                        break;
                    }
                    env->ExceptionClear();
                }
            }

            env->DeleteLocalRef(cPlayer);
            env->DeleteLocalRef(player);
        } else env->ExceptionClear();
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->PopLocalFrame(nullptr);
}

// ── Безопасное разрушение блока через sendPacket(CPacketPlayerDigging) (AutoMine 1-в-1 как в AutoMine.java) ──────────
void JVMBridge::SendDiggingPacket(int x, int y, int z, int facing) {
    (void)facing;
    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return;

    if (env->PushLocalFrame(64) != 0) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    jclass cMC = env->GetObjectClass(mc);
    jobject cl = GetMinecraftClassLoader(env);

    // BlockPos (et)
    jclass cBP = FindMCClass(env, cl, "et");
    if (!cBP) cBP = FindMCClass(env, cl, "net/minecraft/util/math/BlockPos");
    jmethodID mInitBP = cBP ? env->GetMethodID(cBP, "<init>", "(III)V") : nullptr;
    jobject blockPos = (cBP && mInitBP) ? env->NewObject(cBP, mInitBP, x, y, z) : nullptr;

    // EnumFacing.DOWN (fa.a)
    jclass cFacing = FindMCClass(env, cl, "fa");
    if (!cFacing) cFacing = FindMCClass(env, cl, "net/minecraft/util/EnumFacing");
    jfieldID fDown = cFacing ? env->GetStaticFieldID(cFacing, "a", "Lfa;") : nullptr;
    if (!fDown && cFacing) { env->ExceptionClear(); fDown = env->GetStaticFieldID(cFacing, "DOWN", "Lnet/minecraft/util/EnumFacing;"); }
    jobject facingObj = (cFacing && fDown) ? env->GetStaticObjectField(cFacing, fDown) : nullptr;

    if (blockPos && facingObj) {
        // 1. Прямая отправка сетевого пакета CPacketPlayerDigging(Action.START_DESTROY_BLOCK, blockPos, EnumFacing.DOWN)
        // Точь-в-точь как в AutoMine.java:
        // this.mc.player.connection.sendPacket(new CPacketPlayerDigging(CPacketPlayerDigging.Action.START_DESTROY_BLOCK, this.blockPos, EnumFacing.DOWN));
        jfieldID fPl = env->GetFieldID(cMC, "h", "Lbud;");
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, "player", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (!fPl) { env->ExceptionClear(); fPl = env->GetFieldID(cMC, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;"); }
        if (fPl) {
            jobject player = env->GetObjectField(mc, fPl);
            if (player && !env->ExceptionCheck()) {
                jclass cPl = env->GetObjectClass(player);
                jfieldID fConn = env->GetFieldID(cPl, "d", "Lbrz;");
                if (!fConn) { env->ExceptionClear(); fConn = env->GetFieldID(cPl, "connection", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
                if (!fConn) { env->ExceptionClear(); fConn = env->GetFieldID(cPl, "field_71174_a", "Lnet/minecraft/client/network/NetHandlerPlayClient;"); }
                if (fConn) {
                    jobject conn = env->GetObjectField(player, fConn);
                    if (conn && !env->ExceptionCheck()) {
                        jclass cConn = env->GetObjectClass(conn);
                        jclass cDigPacket = FindMCClass(env, cl, "la");
                        if (!cDigPacket) cDigPacket = FindMCClass(env, cl, "net/minecraft/network/play/client/CPacketPlayerDigging");
                        jclass cAction = FindMCClass(env, cl, "la$a");
                        if (!cAction) cAction = FindMCClass(env, cl, "net/minecraft/network/play/client/CPacketPlayerDigging$Action");

                        if (cDigPacket && cAction) {
                            jfieldID fStart = env->GetStaticFieldID(cAction, "a", "Lla$a;");
                            if (!fStart) { env->ExceptionClear(); fStart = env->GetStaticFieldID(cAction, "START_DESTROY_BLOCK", "Lnet/minecraft/network/play/client/CPacketPlayerDigging$Action;"); }
                            if (fStart) {
                                jobject act = env->GetStaticObjectField(cAction, fStart);
                                if (act && !env->ExceptionCheck()) {
                                    jmethodID mInitDig = env->GetMethodID(cDigPacket, "<init>", "(Lla$a;Let;Lfa;)V");
                                    if (!mInitDig) { env->ExceptionClear(); mInitDig = env->GetMethodID(cDigPacket, "<init>", "(Lnet/minecraft/network/play/client/CPacketPlayerDigging$Action;Lnet/minecraft/util/math/BlockPos;Lnet/minecraft/util/EnumFacing;)V"); }
                                    if (mInitDig) {
                                        jobject packet = env->NewObject(cDigPacket, mInitDig, act, blockPos, facingObj);
                                        if (packet && !env->ExceptionCheck()) {
                                            jclass cPacket = FindMCClass(env, cl, "ht");
                                            if (!cPacket) cPacket = FindMCClass(env, cl, "net/minecraft/network/Packet");
                                            jmethodID mSend = env->GetMethodID(cConn, "a", "(Lht;)V");
                                            if (!mSend) { env->ExceptionClear(); mSend = env->GetMethodID(cConn, "sendPacket", "(Lnet/minecraft/network/Packet;)V"); }
                                            if (!mSend) { env->ExceptionClear(); mSend = env->GetMethodID(cConn, "func_147297_a", "(Lnet/minecraft/network/Packet;)V"); }
                                            if (mSend) {
                                                env->CallVoidMethod(conn, mSend, packet);
                                                if (env->ExceptionCheck()) env->ExceptionClear();
                                            }
                                            if (cPacket) env->DeleteLocalRef(cPacket);
                                            env->DeleteLocalRef(packet);
                                        }
                                    }
                                    env->DeleteLocalRef(act);
                                }
                            }
                            env->DeleteLocalRef(cAction);
                            env->DeleteLocalRef(cDigPacket);
                        }
                        env->DeleteLocalRef(cConn);
                        env->DeleteLocalRef(conn);
                    }
                }
                env->DeleteLocalRef(cPl);
                env->DeleteLocalRef(player);
            }
        }

        // 2. PlayerControllerMP: сброс задержки удара blockHitDelay = 0
        jfieldID fPC = env->GetFieldID(cMC, "c", "Lbsa;");
        if (!fPC) { env->ExceptionClear(); fPC = env->GetFieldID(cMC, "playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;"); }
        if (!fPC) { env->ExceptionClear(); fPC = env->GetFieldID(cMC, "field_71442_b", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;"); }
        if (fPC) {
            jobject pc = env->GetObjectField(mc, fPC);
            if (pc && !env->ExceptionCheck()) {
                jclass cPC = env->GetObjectClass(pc);
                jmethodID mClickBlock = env->GetMethodID(cPC, "a", "(Let;Lfa;)Z");
                if (!mClickBlock) { env->ExceptionClear(); mClickBlock = env->GetMethodID(cPC, "clickBlock", "(Lnet/minecraft/util/math/BlockPos;Lnet/minecraft/util/EnumFacing;)Z"); }
                if (mClickBlock) {
                    env->CallBooleanMethod(pc, mClickBlock, blockPos, facingObj);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }
                jfieldID fDelay = env->GetFieldID(cPC, "d", "I");
                if (!fDelay) { env->ExceptionClear(); fDelay = env->GetFieldID(cPC, "blockHitDelay", "I"); }
                if (!fDelay) { env->ExceptionClear(); fDelay = env->GetFieldID(cPC, "field_78781_i", "I"); }
                if (fDelay) env->SetIntField(pc, fDelay, 0);

                env->DeleteLocalRef(cPC);
                env->DeleteLocalRef(pc);
            }
        }

        env->DeleteLocalRef(facingObj);
        env->DeleteLocalRef(blockPos);
    }

    if (cFacing) env->DeleteLocalRef(cFacing);
    if (cBP) env->DeleteLocalRef(cBP);
    if (cl) env->DeleteLocalRef(cl);
    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);
    env->PopLocalFrame(nullptr);
}

// ── Обновление клавиш ходьбы при открытом GUI (GuiWalk) ──────────────────────
void JVMBridge::UpdateGuiWalkKeys(bool forward, bool back, bool left, bool right, bool jump) {
    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return;

    if (env->PushLocalFrame(32) != 0) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    jclass cMC = env->GetObjectClass(mc);
    jfieldID fSet = env->GetFieldID(cMC, "t", "Lbid;");
    if (!fSet) { env->ExceptionClear(); fSet = env->GetFieldID(cMC, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;"); }
    if (!fSet) { env->ExceptionClear(); fSet = env->GetFieldID(cMC, "field_71474_y", "Lnet/minecraft/client/settings/GameSettings;"); }

    if (fSet) {
        jobject settings = env->GetObjectField(mc, fSet);
        if (settings && !env->ExceptionCheck()) {
            jclass cSet = env->GetObjectClass(settings);

            auto setKey = [&](const char* notchF, const char* srgF, bool state) {
                jfieldID fKB = env->GetFieldID(cSet, notchF, "Lbhy;");
                if (!fKB) {
                    env->ExceptionClear();
                    fKB = env->GetFieldID(cSet, srgF, "Lnet/minecraft/client/settings/KeyBinding;");
                }
                if (!fKB) {
                    env->ExceptionClear();
                    return;
                }

                jobject kb = env->GetObjectField(settings, fKB);
                if (kb && !env->ExceptionCheck()) {
                    jclass cKB = env->GetObjectClass(kb);
                    // В 1.12.2 bhy: i = pressed (Z), j = pressTime (I), h = keyCode (I)
                    jfieldID fPressed = env->GetFieldID(cKB, "i", "Z");
                    if (!fPressed) { env->ExceptionClear(); fPressed = env->GetFieldID(cKB, "pressed", "Z"); }
                    if (!fPressed) { env->ExceptionClear(); fPressed = env->GetFieldID(cKB, "field_74513_e", "Z"); }
                    if (fPressed) {
                        env->SetBooleanField(kb, fPressed, state);
                    }

                    jfieldID fPressTime = env->GetFieldID(cKB, "j", "I");
                    if (!fPressTime) { env->ExceptionClear(); fPressTime = env->GetFieldID(cKB, "pressTime", "I"); }
                    if (!fPressTime) { env->ExceptionClear(); fPressTime = env->GetFieldID(cKB, "field_151474_i", "I"); }
                    if (fPressTime) {
                        env->SetIntField(kb, fPressTime, state ? 1 : 0);
                    }

                    if (env->ExceptionCheck()) env->ExceptionClear();
                    env->DeleteLocalRef(cKB);
                    env->DeleteLocalRef(kb);
                } else if (env->ExceptionCheck()) env->ExceptionClear();
            };

            // В 1.12.2 bid (GameSettings):
            // T = keyBindForward
            // V = keyBindBack
            // U = keyBindLeft
            // W = keyBindRight
            // X = keyBindJump
            // Z = keyBindSprint
            setKey("T", "keyBindForward", forward);
            setKey("V", "keyBindBack",    back);
            setKey("U", "keyBindLeft",    left);
            setKey("W", "keyBindRight",   right);
            setKey("X", "keyBindJump",    jump);
            setKey("Z", "keyBindSprint",  false);

            env->DeleteLocalRef(cSet);
            env->DeleteLocalRef(settings);
        } else if (env->ExceptionCheck()) env->ExceptionClear();
    } else if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cMC);
    env->DeleteLocalRef(mc);
    env->PopLocalFrame(nullptr);
}

void JVMBridge::ResetGuiWalkKeys() {
    UpdateGuiWalkKeys(false, false, false, false, false);
}

// ── Поиск игрока под прицелом (MCF — Middle Click Friend) ─────────────────────
std::string JVMBridge::QueryPointedPlayer() {
    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return "";

    if (env->PushLocalFrame(32) != 0) return "";

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return "";
    }

    jclass cMC = env->GetObjectClass(mc);
    std::string resultName = "";

    // 1. pointedEntity (bib.j / field_147125_j / pointedEntity)
    jfieldID fPointed = env->GetFieldID(cMC, "j", "Lvg;");
    if (!fPointed) { env->ExceptionClear(); fPointed = env->GetFieldID(cMC, "pointedEntity", "Lnet/minecraft/entity/Entity;"); }
    if (!fPointed) { env->ExceptionClear(); fPointed = env->GetFieldID(cMC, "field_147125_j", "Lnet/minecraft/entity/Entity;"); }

    jobject pointed = fPointed ? env->GetObjectField(mc, fPointed) : nullptr;
    if (pointed && !env->ExceptionCheck()) {
        jclass cEnt = env->GetObjectClass(pointed);
        jmethodID mGetName = env->GetMethodID(cEnt, "h_", "()Ljava/lang/String;");
        if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cEnt, "getName", "()Ljava/lang/String;"); }
        if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cEnt, "func_70005_c_", "()Ljava/lang/String;"); }
        if (mGetName) {
            auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(pointed, mGetName));
            if (jStr && !env->ExceptionCheck()) {
                const char* chars = env->GetStringUTFChars(jStr, nullptr);
                if (chars) {
                    resultName = chars;
                    env->ReleaseStringUTFChars(jStr, chars);
                }
            } else env->ExceptionClear();
        }
    } else env->ExceptionClear();

    // 2. Fallback через objectMouseOver.entityHit
    if (resultName.empty()) {
        jfieldID fRay = env->GetFieldID(cMC, "s", "Lbru;");
        if (!fRay) { env->ExceptionClear(); fRay = env->GetFieldID(cMC, "objectMouseOver", "Lnet/minecraft/util/math/RayTraceResult;"); }
        if (!fRay) { env->ExceptionClear(); fRay = env->GetFieldID(cMC, "field_71476_x", "Lnet/minecraft/util/math/RayTraceResult;"); }

        if (fRay) {
            jobject ray = env->GetObjectField(mc, fRay);
            if (ray && !env->ExceptionCheck()) {
                jclass cRay = env->GetObjectClass(ray);
                jfieldID fHit = env->GetFieldID(cRay, "d", "Lvg;");
                if (!fHit) { env->ExceptionClear(); fHit = env->GetFieldID(cRay, "entityHit", "Lnet/minecraft/entity/Entity;"); }
                if (!fHit) { env->ExceptionClear(); fHit = env->GetFieldID(cRay, "field_72308_g", "Lnet/minecraft/entity/Entity;"); }

                if (fHit) {
                    jobject hit = env->GetObjectField(ray, fHit);
                    if (hit && !env->ExceptionCheck()) {
                        jclass cHit = env->GetObjectClass(hit);
                        jmethodID mGetName = env->GetMethodID(cHit, "h_", "()Ljava/lang/String;");
                        if (!mGetName) { env->ExceptionClear(); mGetName = env->GetMethodID(cHit, "getName", "()Ljava/lang/String;"); }
                        if (mGetName) {
                            auto jStr = reinterpret_cast<jstring>(env->CallObjectMethod(hit, mGetName));
                            if (jStr && !env->ExceptionCheck()) {
                                const char* chars = env->GetStringUTFChars(jStr, nullptr);
                                if (chars) {
                                    resultName = chars;
                                    env->ReleaseStringUTFChars(jStr, chars);
                                }
                            } else env->ExceptionClear();
                        }
                    } else env->ExceptionClear();
                }
            } else env->ExceptionClear();
        } else env->ExceptionClear();
    }

    env->PopLocalFrame(nullptr);
    return resultName;
}

// ── Поиск блоков и армор стендов для AutoMine ──────────────────────────────
bool JVMBridge::QueryAutoMineTarget(float maxDist, Vec3& outBlockPos, Vec3& outAimPos) {
    if (m_data.mineTargets.empty()) return false;

    double myX = (m_data.playerPos.x != 0.0) ? m_data.playerPos.x : m_data.camPos.x;
    double myY = (m_data.playerPos.y != 0.0) ? m_data.playerPos.y : (m_data.camPos.y - 1.62);
    double myZ = (m_data.playerPos.z != 0.0) ? m_data.playerPos.z : m_data.camPos.z;

    bool found = false;
    double bestDist = static_cast<double>(maxDist);

    for (const auto& target : m_data.mineTargets) {
        double dx = target.standPos.x - myX;
        double dy = target.standPos.y - myY;
        double dz = target.standPos.z - myZ;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);

        if (dist <= bestDist) {
            bestDist = dist;
            outBlockPos = target.blockPos;
            // The ArmorStand is the server's visible cross marker. Mine the
            // associated block, but rotate toward the marker position itself.
            outAimPos = { target.standPos.x, target.standPos.y + 0.35, target.standPos.z };
            found = true;
        }
    }

    return found;
}

// ── Скрытие серверного прицела через Forge (GuiIngameForge.renderCrosshairs) ──
void JVMBridge::SetHideVanillaCrosshair(bool hide) {
    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return;

    static jclass s_cForge = nullptr;
    static jfieldID s_fRenderCrosshairs = nullptr;
    if (!s_cForge) {
        jobject cl = m_classLoader ? m_classLoader : GetMinecraftClassLoader(env);
        jclass cF = FindMCClass(env, cl, "net/minecraftforge/client/GuiIngameForge");
        if (cF) {
            s_cForge = reinterpret_cast<jclass>(env->NewGlobalRef(cF));
            env->DeleteLocalRef(cF);
            s_fRenderCrosshairs = env->GetStaticFieldID(s_cForge, "renderCrosshairs", "Z");
            if (!s_fRenderCrosshairs) env->ExceptionClear();
        } else {
            env->ExceptionClear();
        }
    }

    if (s_cForge && s_fRenderCrosshairs) {
        env->SetStaticBooleanField(s_cForge, s_fRenderCrosshairs, hide ? JNI_FALSE : JNI_TRUE);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
}

// ── Очистка собственного никнейма в истории чата (NameProtect) ───────────────
void JVMBridge::SanitizeChat(const std::string& targetName, const std::string& replacement) {
    if (targetName.empty() || replacement.empty() || targetName == replacement) return;

    JNIEnv* env = GetJNIEnv();
    if (!env || !m_ok) return;

    jobject mc = GetMinecraftInstance(env);
    if (!mc || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    static jfieldID s_fIngameGUI = nullptr;
    static jfieldID s_fPersistantChat = nullptr;
    static jfieldID s_fDrawnChatLines = nullptr;
    static jfieldID s_fChatLines = nullptr;
    static jfieldID s_fLineString = nullptr;
    static jmethodID s_mGetFormatted = nullptr;
    static jclass s_cTextCompStr = nullptr;
    static jmethodID s_mInitTextCompStr = nullptr;
    static jclass s_cList = nullptr;
    static jmethodID s_mListSize = nullptr;
    static jmethodID s_mListGet = nullptr;

    if (!s_fIngameGUI) {
        jclass cMC = env->GetObjectClass(mc);
        if (cMC) {
            const char* igNames[] = { "ingameGUI", "field_71456_v", "q", "r" };
            for (const char* ign : igNames) {
                s_fIngameGUI = env->GetFieldID(cMC, ign, "Lnet/minecraft/client/gui/GuiIngame;");
                if (!s_fIngameGUI) { env->ExceptionClear(); s_fIngameGUI = env->GetFieldID(cMC, ign, "Lbiq;"); }
                if (!s_fIngameGUI) { env->ExceptionClear(); s_fIngameGUI = env->GetFieldID(cMC, ign, "Lbvq;"); }
                if (s_fIngameGUI) break;
                env->ExceptionClear();
            }
            env->DeleteLocalRef(cMC);
        }
    }

    if (!s_fIngameGUI) return;

    jobject ig = env->GetObjectField(mc, s_fIngameGUI);
    if (!ig || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    if (!s_fPersistantChat) {
        jclass cIG = env->GetObjectClass(ig);
        if (cIG) {
            const char* pcNames[] = { "persistantChatGUI", "field_73840_e", "l", "k", "m", "s" };
            for (const char* pcn : pcNames) {
                s_fPersistantChat = env->GetFieldID(cIG, pcn, "Lnet/minecraft/client/gui/GuiNewChat;");
                if (!s_fPersistantChat) { env->ExceptionClear(); s_fPersistantChat = env->GetFieldID(cIG, pcn, "Lbvu;"); }
                if (!s_fPersistantChat) { env->ExceptionClear(); s_fPersistantChat = env->GetFieldID(cIG, pcn, "Lbit;"); }
                if (s_fPersistantChat) break;
                env->ExceptionClear();
            }
            env->DeleteLocalRef(cIG);
        }
    }

    if (!s_fPersistantChat) { env->DeleteLocalRef(ig); return; }

    jobject chatGUI = env->GetObjectField(ig, s_fPersistantChat);
    env->DeleteLocalRef(ig);
    if (!chatGUI || env->ExceptionCheck()) { env->ExceptionClear(); return; }

    if (!s_fDrawnChatLines || !s_fChatLines) {
        jclass cChatGUI = env->GetObjectClass(chatGUI);
        if (cChatGUI) {
            const char* drawnNames[] = { "drawnChatLines", "field_146247_f", "h", "g", "f" };
            for (const char* dn : drawnNames) {
                s_fDrawnChatLines = env->GetFieldID(cChatGUI, dn, "Ljava/util/List;");
                if (s_fDrawnChatLines) break;
                env->ExceptionClear();
            }
            const char* chatNames[] = { "chatLines", "field_146246_g", "g", "h", "f" };
            for (const char* cn : chatNames) {
                jfieldID f = env->GetFieldID(cChatGUI, cn, "Ljava/util/List;");
                if (f && f != s_fDrawnChatLines) { s_fChatLines = f; break; }
                env->ExceptionClear();
            }
            env->DeleteLocalRef(cChatGUI);
        }
    }

    if (!s_cList) {
        jclass cL = env->FindClass("java/util/List");
        if (cL) {
            s_cList = reinterpret_cast<jclass>(env->NewGlobalRef(cL));
            env->DeleteLocalRef(cL);
            s_mListSize = env->GetMethodID(s_cList, "size", "()I");
            s_mListGet = env->GetMethodID(s_cList, "get", "(I)Ljava/lang/Object;");
        }
    }

    if (!s_cTextCompStr) {
        jclass cTCS = FindMCClass(env, m_classLoader, "net/minecraft/util/text/TextComponentString");
        if (!cTCS) cTCS = FindMCClass(env, m_classLoader, "ho"); // 1.12.2 Notch
        if (!cTCS) cTCS = FindMCClass(env, m_classLoader, "hg");
        if (!cTCS) cTCS = FindMCClass(env, m_classLoader, "hi");
        if (!cTCS) cTCS = FindMCClass(env, m_classLoader, "hj");
        if (cTCS) {
            s_cTextCompStr = reinterpret_cast<jclass>(env->NewGlobalRef(cTCS));
            env->DeleteLocalRef(cTCS);
            s_mInitTextCompStr = env->GetMethodID(s_cTextCompStr, "<init>", "(Ljava/lang/String;)V");
        }
    }

    // Собираем всевозможные вариации собственного никнейма (например "_I_Love_UOY", "I_Love_UOY", "I Love UOY")
    std::vector<std::string> searchTargets;
    auto AddNickVariants = [&](const std::string& n) {
        if (n.empty() || n == "Unknown" || n == "Player" || n == replacement) return;
        auto AddOne = [&](const std::string& v) {
            if (v.empty() || v == replacement) return;
            if (std::find(searchTargets.begin(), searchTargets.end(), v) == searchTargets.end()) {
                searchTargets.push_back(v);
            }
        };
        AddOne(n);
        std::string withUnder = n;
        for (char& ch : withUnder) { if (ch == ' ') ch = '_'; }
        AddOne(withUnder);
        std::string withSpace = n;
        for (char& ch : withSpace) { if (ch == '_') ch = ' '; }
        AddOne(withSpace);

        size_t sIdx = n.find_first_not_of('_');
        size_t eIdx = n.find_last_not_of('_');
        if (sIdx != std::string::npos && eIdx != std::string::npos) {
            std::string core = n.substr(sIdx, eIdx - sIdx + 1);
            AddOne(core);
            AddOne(std::string("_") + core);
            AddOne(core + "_");
            AddOne(std::string("_") + core + "_");
            std::string coreUnder = core;
            for (char& ch : coreUnder) { if (ch == ' ') ch = '_'; }
            AddOne(coreUnder);
            AddOne(std::string("_") + coreUnder);
            AddOne(coreUnder + "_");
        }
    };
    AddNickVariants(targetName);
    AddNickVariants(m_data.username);

    auto SanitizeList = [&](jfieldID fList) {
        if (!fList || !s_mListSize || !s_mListGet) return;
        jobject listObj = env->GetObjectField(chatGUI, fList);
        if (!listObj || env->ExceptionCheck()) { env->ExceptionClear(); return; }

        jint sz = env->CallIntMethod(listObj, s_mListSize);
        if (env->ExceptionCheck()) { env->ExceptionClear(); sz = 0; }
        if (sz > 100) sz = 100;

        for (jint i = 0; i < sz; ++i) {
            if (env->PushLocalFrame(16) != 0) break;
            jobject chatLine = env->CallObjectMethod(listObj, s_mListGet, i);
            if (!chatLine || env->ExceptionCheck()) { env->ExceptionClear(); env->PopLocalFrame(nullptr); continue; }

            if (!s_fLineString) {
                jclass cCL = env->GetObjectClass(chatLine);
                if (cCL) {
                    const char* lsNames[] = { "lineString", "field_74541_b", "b", "a" };
                    for (const char* lsn : lsNames) {
                        s_fLineString = env->GetFieldID(cCL, lsn, "Lnet/minecraft/util/text/ITextComponent;");
                        if (!s_fLineString) { env->ExceptionClear(); s_fLineString = env->GetFieldID(cCL, lsn, "Lhh;"); }
                        if (s_fLineString) break;
                        env->ExceptionClear();
                    }
                    env->DeleteLocalRef(cCL);
                }
            }

            if (s_fLineString) {
                jobject textComp = env->GetObjectField(chatLine, s_fLineString);
                if (textComp && !env->ExceptionCheck()) {
                    if (!s_cTextCompStr || !s_mInitTextCompStr) {
                        jclass cCompClass = env->GetObjectClass(textComp);
                        if (cCompClass) {
                            jmethodID mInit = env->GetMethodID(cCompClass, "<init>", "(Ljava/lang/String;)V");
                            if (mInit) {
                                s_cTextCompStr = reinterpret_cast<jclass>(env->NewGlobalRef(cCompClass));
                                s_mInitTextCompStr = mInit;
                            }
                            env->DeleteLocalRef(cCompClass);
                        }
                    }

                    if (!s_mGetFormatted) {
                        jclass cComp = env->GetObjectClass(textComp);
                        if (cComp) {
                            s_mGetFormatted = env->GetMethodID(cComp, "d", "()Ljava/lang/String;");
                            if (!s_mGetFormatted) { env->ExceptionClear(); s_mGetFormatted = env->GetMethodID(cComp, "getFormattedText", "()Ljava/lang/String;"); }
                            if (!s_mGetFormatted) { env->ExceptionClear(); s_mGetFormatted = env->GetMethodID(cComp, "func_150254_d", "()Ljava/lang/String;"); }
                            if (!s_mGetFormatted) { env->ExceptionClear(); s_mGetFormatted = env->GetMethodID(cComp, "c", "()Ljava/lang/String;"); }
                            if (!s_mGetFormatted) { env->ExceptionClear(); s_mGetFormatted = env->GetMethodID(cComp, "getUnformattedText", "()Ljava/lang/String;"); }
                            env->DeleteLocalRef(cComp);
                        }
                    }

                    if (s_mGetFormatted && s_cTextCompStr && s_mInitTextCompStr) {
                        jstring jStr = reinterpret_cast<jstring>(env->CallObjectMethod(textComp, s_mGetFormatted));
                        if (jStr && !env->ExceptionCheck()) {
                            const char* cChars = env->GetStringUTFChars(jStr, nullptr);
                            if (cChars) {
                                std::string s(cChars);
                                env->ReleaseStringUTFChars(jStr, cChars);

                                bool modified = false;
                                for (const auto& tName : searchTargets) {
                                    size_t pos = s.find(tName);
                                    if (pos != std::string::npos) {
                                        while (pos != std::string::npos) {
                                            s.replace(pos, tName.length(), replacement);
                                            pos = s.find(tName, pos + replacement.length());
                                        }
                                        modified = true;
                                    }
                                }

                                if (modified) {
                                    jstring newJStr = env->NewStringUTF(s.c_str());
                                    if (newJStr) {
                                        jobject newComp = env->NewObject(s_cTextCompStr, s_mInitTextCompStr, newJStr);
                                        if (newComp && !env->ExceptionCheck()) {
                                            env->SetObjectField(chatLine, s_fLineString, newComp);
                                            env->DeleteLocalRef(newComp);
                                        } else {
                                            env->ExceptionClear();
                                        }
                                        env->DeleteLocalRef(newJStr);
                                    }
                                }
                            }
                        } else {
                            env->ExceptionClear();
                        }
                    }
                } else {
                    env->ExceptionClear();
                }
            }
            env->PopLocalFrame(nullptr);
        }
        env->DeleteLocalRef(listObj);
    };

    SanitizeList(s_fDrawnChatLines);
    SanitizeList(s_fChatLines);

    env->DeleteLocalRef(chatGUI);
}
