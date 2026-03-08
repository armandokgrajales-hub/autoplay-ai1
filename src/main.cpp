#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// ============================================================
//  ESTADO GLOBAL
// ============================================================

struct AIConfig {
    bool aiEnabled    = false;
    bool safeMode     = false;   // ignora hitboxes mortales (noclip basico)
    bool speedhack    = false;
    bool tpsBypass    = false;
    float speedMult   = 1.0f;
    float tpsValue    = 240.0f;
};

static AIConfig g_cfg;
static bool g_buttonHeld = false;

// IDs peligrosos GD 2.2
static const std::unordered_set<int> HAZARD_IDS = {
    8, 39, 40, 49,
    395, 396,
    148, 149, 150,
    184, 185,
    1616, 1617, 1619, 1620,
    1331, 1332,
    2015, 2016,
    1705, 1706,
};

static constexpr float LOOKAHEAD_X    = 160.0f;
static constexpr float LOOKAHEAD_Y    = 95.0f;
static constexpr float SOLID_X        = 95.0f;
static constexpr float SOLID_Y_MARGIN = 28.0f;
// Maximo de objetos revisados por frame para evitar crash
static constexpr unsigned int MAX_CHECK = 300;

// ============================================================
//  UTILIDADES
// ============================================================

static bool isHazard(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType == GameObjectType::Decoration) return false;
    return HAZARD_IDS.count(obj->m_objectID) > 0;
}

// ============================================================
//  HOOK: CCScheduler - TPS Bypass
// ============================================================

class $modify(AIScheduler, CCScheduler) {
    void update(float dt) {
        if (g_cfg.tpsBypass && PlayLayer::get()) {
            float newDt = 1.0f / g_cfg.tpsValue;
            CCScheduler::update(newDt);
        } else {
            CCScheduler::update(dt);
        }
    }
};

// ============================================================
//  HOOK: PlayLayer
// ============================================================

class $modify(AIPlayLayer, PlayLayer) {

    void update(float dt) {
        // Speedhack
        float realDt = g_cfg.speedhack ? dt * g_cfg.speedMult : dt;
        PlayLayer::update(realDt);

        if (!g_cfg.aiEnabled) return;

        auto* player = m_player1;
        if (!player) return;

        if (player->m_isDead) {
            if (g_buttonHeld) {
                player->releaseButton(PlayerButton::Jump);
                g_buttonHeld = false;
            }
            return;
        }

        CCPoint playerPos = player->getPosition();
        bool shouldPress  = false;

        auto* objects = m_objects;
        if (!objects) return;

        unsigned int total   = objects->count();
        unsigned int checked = 0;

        for (unsigned int i = 0; i < total && checked < MAX_CHECK; i++) {
            auto* raw = objects->objectAtIndex(i);
            if (!raw) continue;

            auto* obj = typeinfo_cast<GameObject*>(raw);
            if (!obj) continue;
            if (!obj->isVisible()) continue;
            if (obj->m_objectType == GameObjectType::Decoration) continue;

            checked++;

            CCPoint objPos = obj->getPosition();
            float deltaX   = objPos.x - playerPos.x;

            if (deltaX < -10.0f || deltaX > LOOKAHEAD_X) continue;

            float deltaY = std::abs(objPos.y - playerPos.y);
            if (deltaY > LOOKAHEAD_Y) continue;

            if (isHazard(obj)) {
                shouldPress = true;
                break;
            }

            if (obj->m_objectType == GameObjectType::Solid) {
                if (deltaX < SOLID_X && objPos.y >= playerPos.y - SOLID_Y_MARGIN) {
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

    // Safe Mode - noclip basico
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_cfg.safeMode) return;  // ignorar muerte
        AIPlayLayer::destroyPlayer(player, obj);
    }

    void resetLevel() {
        AIPlayLayer::resetLevel();
        g_buttonHeld = false;
        if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
    }

    void onQuit() {
        g_buttonHeld = false;
        if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
        AIPlayLayer::onQuit();
    }
};

// ============================================================
//  GUI QOL - PauseLayer
// ============================================================

class $modify(AIPauseLayer, PauseLayer) {

    // Crear fila de toggle con etiqueta
    CCMenuItemToggle* makeToggle(const char* labelText, bool currentState, SEL_MenuHandler selector) {
        auto on  = CCLabelBMFont::create("ON",  "bigFont.fnt");
        auto off = CCLabelBMFont::create("OFF", "bigFont.fnt");
        on->setScale(0.45f);
        off->setScale(0.45f);
        on->setColor({0, 255, 100});
        off->setColor({255, 80, 80});

        auto itemOn  = CCMenuItemLabel::create(on,  this, selector);
        auto itemOff = CCMenuItemLabel::create(off, this, selector);

        auto toggle = CCMenuItemToggle::createWithTarget(this, selector, itemOff, itemOn, nullptr);
        toggle->setSelectedIndex(currentState ? 1 : 0);
        return toggle;
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Panel de fondo
        auto bg = CCScale9Sprite::create("square02_small.png");
        bg->setContentSize({160.0f, 140.0f});
        bg->setPosition({winSize.width - 88.0f, 90.0f});
        bg->setOpacity(180);
        bg->setColor({0, 0, 0});
        bg->setZOrder(9);
        this->addChild(bg);

        // Titulo del panel
        auto title = CCLabelBMFont::create("AutoPlay AI", "goldFont.fnt");
        title->setScale(0.5f);
        title->setPosition({winSize.width - 88.0f, 152.0f});
        this->addChild(title, 10);

        // Menu con todos los toggles
        auto menu = CCMenu::create();
        menu->setPosition({winSize.width - 88.0f, 90.0f});
        menu->setZOrder(10);

        // ----- Activate AI -----
        auto aiLabel = CCLabelBMFont::create("Activate AI", "bigFont.fnt");
        aiLabel->setScale(0.38f);
        aiLabel->setColor({180, 220, 255});

        auto aiToggle = makeToggle("AI", g_cfg.aiEnabled,
            menu_selector(AIPauseLayer::onToggleAI));
        aiToggle->setPositionX(45.0f);

        auto aiRow = CCNode::create();
        aiLabel->setPosition({-30.0f, 0.0f});
        aiRow->addChild(aiLabel);
        aiRow->addChild(aiToggle);
        aiRow->setPosition({0.0f, 48.0f});
        menu->addChild(aiRow);

        // ----- Safe Mode -----
        auto smLabel = CCLabelBMFont::create("Safe Mode", "bigFont.fnt");
        smLabel->setScale(0.38f);
        smLabel->setColor({180, 220, 255});

        auto smToggle = makeToggle("SM", g_cfg.safeMode,
            menu_selector(AIPauseLayer::onToggleSafeMode));
        smToggle->setPositionX(45.0f);

        auto smRow = CCNode::create();
        smLabel->setPosition({-30.0f, 0.0f});
        smRow->addChild(smLabel);
        smRow->addChild(smToggle);
        smRow->setPosition({0.0f, 16.0f});
        menu->addChild(smRow);

        // ----- Speedhack -----
        auto shLabel = CCLabelBMFont::create("Speedhack", "bigFont.fnt");
        shLabel->setScale(0.38f);
        shLabel->setColor({180, 220, 255});

        auto shToggle = makeToggle("SH", g_cfg.speedhack,
            menu_selector(AIPauseLayer::onToggleSpeedhack));
        shToggle->setPositionX(45.0f);

        auto shRow = CCNode::create();
        shLabel->setPosition({-30.0f, 0.0f});
        shRow->addChild(shLabel);
        shRow->addChild(shToggle);
        shRow->setPosition({0.0f, -16.0f});
        menu->addChild(shRow);

        // ----- TPS Bypass -----
        auto tpsLabel = CCLabelBMFont::create("TPS Bypass", "bigFont.fnt");
        tpsLabel->setScale(0.38f);
        tpsLabel->setColor({180, 220, 255});

        auto tpsToggle = makeToggle("TPS", g_cfg.tpsBypass,
            menu_selector(AIPauseLayer::onToggleTPS));
        tpsToggle->setPositionX(45.0f);

        auto tpsRow = CCNode::create();
        tpsLabel->setPosition({-30.0f, 0.0f});
        tpsRow->addChild(tpsLabel);
        tpsRow->addChild(tpsToggle);
        tpsRow->setPosition({0.0f, -48.0f});
        menu->addChild(tpsRow);

        this->addChild(menu);
    }

    void onToggleAI(CCObject* sender) {
        g_cfg.aiEnabled = !g_cfg.aiEnabled;
        Notification::create(
            g_cfg.aiEnabled ? "AI: ON" : "AI: OFF",
            NotificationIcon::Success
        )->show();
        if (!g_cfg.aiEnabled && g_buttonHeld) {
            auto* pl = PlayLayer::get();
            if (pl && pl->m_player1) pl->m_player1->releaseButton(PlayerButton::Jump);
            g_buttonHeld = false;
        }
    }

    void onToggleSafeMode(CCObject* sender) {
        g_cfg.safeMode = !g_cfg.safeMode;
        Notification::create(
            g_cfg.safeMode ? "Safe Mode: ON" : "Safe Mode: OFF",
            NotificationIcon::Success
        )->show();
    }

    void onToggleSpeedhack(CCObject* sender) {
        g_cfg.speedhack = !g_cfg.speedhack;
        if (g_cfg.speedhack) g_cfg.speedMult = 1.5f;
        Notification::create(
            g_cfg.speedhack ? "Speedhack: ON (1.5x)" : "Speedhack: OFF",
            NotificationIcon::Success
        )->show();
    }

    void onToggleTPS(CCObject* sender) {
        g_cfg.tpsBypass = !g_cfg.tpsBypass;
        Notification::create(
            g_cfg.tpsBypass ? "TPS Bypass: ON (240)" : "TPS Bypass: OFF",
            NotificationIcon::Success
        )->show();
    }
};

// ============================================================
//  ENTRADA
// ============================================================

$on_mod(Loaded) {
    log::info("AutoPlay AI v1.1.0 loaded.");
}
