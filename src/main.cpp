// ============================================================
//  AutoPlay AI v1.4.0
//  - Boton flotante visible en juego
//  - Sin hooks peligrosos
//  - Ultra seguro para gama baja
// ============================================================

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

#include <unordered_set>
#include <string>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

// ============================================================
//  CONFIG GLOBAL
// ============================================================

struct AIConfig {
    bool  aiEnabled     = false;
    bool  safeMode      = false;
    bool  speedhack     = false;
    float speedMult     = 1.0f;
    bool  speedMusic    = false;
    bool  tpsBypass     = false;
    float tpsValue      = 240.0f;
};

static AIConfig g_cfg;
static bool     g_buttonHeld  = false;
static bool     g_levelActive = false;
static float    g_tpsAccum    = 0.0f;

// ============================================================
//  IDs PELIGROSOS
// ============================================================

static const std::unordered_set<int> HAZARD_IDS = {
    8, 39, 40, 49,
    395, 396,
    148, 149, 150,
    184, 185,
    1616, 1617,
    1619, 1620,
    1331, 1332,
    2015, 2016,
    1705, 1706,
};

static constexpr float        HAZARD_X       = 140.0f;
static constexpr float        HAZARD_Y       = 90.0f;
static constexpr float        SOLID_X        = 85.0f;
static constexpr float        SOLID_Y_MARGIN = 26.0f;
static constexpr unsigned int MAX_PER_FRAME  = 180;

// ============================================================
//  HELPERS
// ============================================================

static float safeParseFloat(const std::string& s, float fallback) {
    if (s.empty()) return fallback;
    try {
        float v = std::stof(s);
        return (std::isfinite(v) && v > 0.0f) ? v : fallback;
    } catch (...) {
        return fallback;
    }
}

static void safeRelease() {
    if (!g_buttonHeld) return;
    auto* pl = PlayLayer::get();
    if (pl && pl->m_player1) {
        pl->m_player1->releaseButton(PlayerButton::Jump);
    }
    g_buttonHeld = false;
}

static bool isHazard(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType == GameObjectType::Decoration) return false;
    if (obj->m_isNoTouch) return false;
    return HAZARD_IDS.count(obj->m_objectID) > 0;
}

static bool isSolid(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType != GameObjectType::Solid) return false;
    if (obj->m_isNoTouch) return false;
    return true;
}

// ============================================================
//  HOOK: FMODAudioEngine
// ============================================================

class $modify(AIAudio, FMODAudioEngine) {
    void update(float dt) {
        if (g_cfg.speedhack && g_cfg.speedMusic && g_levelActive) {
            FMODAudioEngine::update(dt * g_cfg.speedMult);
        } else {
            FMODAudioEngine::update(dt);
        }
    }
};

// ============================================================
//  HOOK: PlayLayer
// ============================================================

class $modify(AIPlayLayer, PlayLayer) {

    void update(float dt) {
        // Speedhack
        float useDt = (g_cfg.speedhack) ? dt * g_cfg.speedMult : dt;

        // TPS Bypass via acumulador (no hookea CCScheduler)
        if (g_cfg.tpsBypass && g_levelActive) {
            g_tpsAccum += useDt;
            float target = 1.0f / g_cfg.tpsValue;
            if (g_tpsAccum < target) return;
            g_tpsAccum -= target;
            useDt = target;
        }

        PlayLayer::update(useDt);

        // ---- IA ----
        if (!g_cfg.aiEnabled || !g_levelActive) return;

        auto* player = m_player1;
        if (!player) return;
        if (player->m_isDead) {
            safeRelease();
            return;
        }

        auto* objects = m_objects;
        if (!objects) return;

        unsigned int total = objects->count();
        if (total == 0) return;

        CCPoint playerPos  = player->getPosition();
        bool    shouldPress = false;
        unsigned int checked = 0;

        for (unsigned int i = 0; i < total; i++) {
            if (checked >= MAX_PER_FRAME) break;
            // Si el array cambio de tamanio, abortar
            if (objects->count() != total) break;

            CCObject* raw = objects->objectAtIndex(i);
            if (!raw) continue;

            auto* obj = typeinfo_cast<GameObject*>(raw);
            if (!obj) continue;
            if (!obj->isVisible()) continue;
            if (obj->m_objectType == GameObjectType::Decoration) continue;
            if (obj->m_isNoTouch) continue;

            checked++;

            CCPoint objPos = obj->getPosition();
            float   dX     = objPos.x - playerPos.x;
            if (dX < -10.0f || dX > HAZARD_X) continue;

            float dY = std::abs(objPos.y - playerPos.y);
            if (dY > HAZARD_Y) continue;

            if (isHazard(obj)) { shouldPress = true; break; }

            if (isSolid(obj)) {
                if (dX < SOLID_X && objPos.y >= playerPos.y - SOLID_Y_MARGIN) {
                    shouldPress = true;
                    break;
                }
            }
        }

        if (shouldPress && !g_buttonHeld) {
            if (m_player1 && !m_player1->m_isDead) {
                m_player1->pushButton(PlayerButton::Jump);
                g_buttonHeld = true;
            }
        } else if (!shouldPress && g_buttonHeld) {
            if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
            g_buttonHeld = false;
        }
    }

    // Safe Mode = noclip
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_cfg.safeMode) return;
        AIPlayLayer::destroyPlayer(player, obj);
    }

    void resetLevel() {
        g_buttonHeld = false;
        g_tpsAccum   = 0.0f;
        if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
        AIPlayLayer::resetLevel();
        g_levelActive = true;
    }

    void onQuit() {
        g_levelActive = false;
        g_buttonHeld  = false;
        g_tpsAccum    = 0.0f;
        if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
        AIPlayLayer::onQuit();
    }

    // Usar startGame en vez de init para evitar firma incorrecta
    void startGame() {
        g_levelActive = true;
        g_buttonHeld  = false;
        g_tpsAccum    = 0.0f;
        AIPlayLayer::startGame();
    }
};

// ============================================================
//  PANEL QOL - aparece al pausar
// ============================================================

static constexpr int TAG_PANEL       = 800;
static constexpr int TAG_SPEED_INPUT = 801;
static constexpr int TAG_TPS_INPUT   = 802;

class $modify(AIPauseLayer, PauseLayer) {

    CCMenuItemToggle* mkTog(const char* on, const char* off,
                             bool state, SEL_MenuHandler sel) {
        auto lOn  = CCLabelBMFont::create(on,  "bigFont.fnt");
        auto lOff = CCLabelBMFont::create(off, "bigFont.fnt");
        lOn->setScale(0.38f);  lOff->setScale(0.38f);
        lOn->setColor({0, 210, 70}); lOff->setColor({210, 50, 50});
        auto iOn  = CCMenuItemLabel::create(lOn,  this, sel);
        auto iOff = CCMenuItemLabel::create(lOff, this, sel);
        auto t = CCMenuItemToggle::createWithTarget(this, sel, iOff, iOn, nullptr);
        t->setSelectedIndex(state ? 1 : 0);
        return t;
    }

    CCLabelBMFont* mkLbl(const char* txt, float sc = 0.32f) {
        auto l = CCLabelBMFont::create(txt, "bigFont.fnt");
        l->setScale(sc);
        l->setColor({200, 215, 255});
        return l;
    }

    CCTextInputNode* mkInput(const char* hint, float w, int tag) {
        auto n = CCTextInputNode::create(w, 20.0f, hint, "bigFont.fnt");
        n->setMaxLabelScale(0.35f);
        n->setAllowedChars("0123456789.");
        n->setTag(tag);
        return n;
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto ws  = CCDirector::sharedDirector()->getWinSize();
        // Posicion: esquina inferior derecha, siempre visible
        float px = ws.width - 88.0f;
        float py = 115.0f;

        // Fondo
        auto bg = CCScale9Sprite::create("square02_small.png");
        bg->setContentSize({170.0f, 210.0f});
        bg->setPosition({px, py});
        bg->setOpacity(220);
        bg->setColor({5, 5, 18});
        bg->setZOrder(9);
        this->addChild(bg);

        // Titulo
        auto title = CCLabelBMFont::create("AutoPlay AI", "goldFont.fnt");
        title->setScale(0.44f);
        title->setPosition({px, py + 95.0f});
        this->addChild(title, 11);

        auto menu = CCMenu::create();
        menu->setPosition({px, py});
        menu->setZOrder(10);
        menu->setTag(TAG_PANEL);

        float y = 76.0f;

        // Activate AI
        { auto l = mkLbl("Activate AI"); l->setPosition({-44.0f, y}); menu->addChild(l);
          auto t = mkTog("ON","OFF", g_cfg.aiEnabled, menu_selector(AIPauseLayer::onAI));
          t->setPosition({56.0f, y}); menu->addChild(t); y -= 32.0f; }

        // Safe Mode
        { auto l = mkLbl("Safe Mode"); l->setPosition({-44.0f, y}); menu->addChild(l);
          auto t = mkTog("ON","OFF", g_cfg.safeMode, menu_selector(AIPauseLayer::onSafe));
          t->setPosition({56.0f, y}); menu->addChild(t); y -= 32.0f; }

        // Speedhack
        { auto l = mkLbl("Speedhack"); l->setPosition({-44.0f, y}); menu->addChild(l);
          auto t = mkTog("ON","OFF", g_cfg.speedhack, menu_selector(AIPauseLayer::onSpeed));
          t->setPosition({56.0f, y}); menu->addChild(t); y -= 20.0f;

          auto sp = mkInput("Speed%", 55.0f, TAG_SPEED_INPUT);
          sp->setPosition({-10.0f, y}); menu->addChild(sp); y -= 18.0f;

          auto lm = mkLbl("Music", 0.26f); lm->setPosition({-44.0f, y}); menu->addChild(lm);
          auto tm = mkTog("Y","N", g_cfg.speedMusic, menu_selector(AIPauseLayer::onSpeedMusic));
          tm->setScale(0.7f); tm->setPosition({-8.0f, y}); menu->addChild(tm); y -= 26.0f; }

        // TPS Bypass
        { auto l = mkLbl("TPS Bypass"); l->setPosition({-44.0f, y}); menu->addChild(l);
          auto t = mkTog("ON","OFF", g_cfg.tpsBypass, menu_selector(AIPauseLayer::onTPS));
          t->setPosition({56.0f, y}); menu->addChild(t); y -= 20.0f;

          auto tp = mkInput("FPS", 55.0f, TAG_TPS_INPUT);
          tp->setPosition({-10.0f, y}); menu->addChild(tp); }

        this->addChild(menu);
        this->schedule(schedule_selector(AIPauseLayer::syncInputs), 0.4f);
    }

    void syncInputs(float) {
        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByTag(TAG_PANEL));
        if (!menu) return;

        auto* spN = typeinfo_cast<CCTextInputNode*>(menu->getChildByTag(TAG_SPEED_INPUT));
        if (spN) {
            float v = safeParseFloat(std::string(spN->getString()), 100.0f);
            if (v >= 1.0f && v <= 2000.0f) g_cfg.speedMult = v / 100.0f;
        }

        auto* tpN = typeinfo_cast<CCTextInputNode*>(menu->getChildByTag(TAG_TPS_INPUT));
        if (tpN) {
            float v = safeParseFloat(std::string(tpN->getString()), 240.0f);
            if (v >= 30.0f && v <= 1000.0f) g_cfg.tpsValue = v;
        }
    }

    void onAI(CCObject*) {
        g_cfg.aiEnabled = !g_cfg.aiEnabled;
        if (!g_cfg.aiEnabled) safeRelease();
        Notification::create(g_cfg.aiEnabled ? "AI: ON" : "AI: OFF",
            NotificationIcon::Success)->show();
    }

    void onSafe(CCObject*) {
        g_cfg.safeMode = !g_cfg.safeMode;
        Notification::create(g_cfg.safeMode ? "Safe Mode: ON" : "Safe Mode: OFF",
            NotificationIcon::Success)->show();
    }

    void onSpeed(CCObject*) {
        g_cfg.speedhack = !g_cfg.speedhack;
        Notification::create(g_cfg.speedhack ? "Speedhack: ON" : "Speedhack: OFF",
            NotificationIcon::Success)->show();
    }

    void onSpeedMusic(CCObject*) {
        g_cfg.speedMusic = !g_cfg.speedMusic;
        Notification::create(g_cfg.speedMusic ? "Speed Music: ON" : "Speed Music: OFF",
            NotificationIcon::Success)->show();
    }

    void onTPS(CCObject*) {
        g_cfg.tpsBypass = !g_cfg.tpsBypass;
        g_tpsAccum = 0.0f;
        Notification::create(g_cfg.tpsBypass ? "TPS: ON" : "TPS: OFF",
            NotificationIcon::Success)->show();
    }
};

// ============================================================
//  ENTRADA
// ============================================================

$on_mod(Loaded) {
    log::info("AutoPlay AI v1.4.0 loaded.");
}
