#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// ============================================================
//  CONFIG GLOBAL
// ============================================================

struct AIConfig {
    // AI
    bool  aiEnabled     = false;

    // Safe Mode (noclip sin registrar verificacion)
    bool  safeMode      = false;

    // Speedhack
    bool  speedhack       = false;
    float speedMult       = 1.0f;
    bool  speedMusic      = false;   // aplicar speed a la musica
    bool  speedProgress   = false;   // aplicar speed al porcentaje real

    // TPS Bypass
    bool  tpsBypass     = false;
    float tpsValue      = 240.0f;
};

static AIConfig g_cfg;
static bool     g_buttonHeld = false;

// IDs de objetos peligrosos - SOLO los que tienen hitbox mortal
static const std::unordered_set<int> HAZARD_IDS = {
    8, 39, 40, 49,           // spikes normales
    395, 396,                // spikes invertidos
    148, 149, 150,           // saws
    184, 185,                // sawblades
    1616, 1617,              // spikes esquina
    1619, 1620,              // spikes pared
    1331, 1332,              // especiales
    2015, 2016,              // portales peligrosos
    1705, 1706,              // spikes 2.2
};

static constexpr float   LOOKAHEAD_X  = 160.0f;
static constexpr float   LOOKAHEAD_Y  = 95.0f;
static constexpr float   SOLID_X      = 95.0f;
static constexpr float   SOLID_Y      = 28.0f;
// Maximo objetos por frame - evita crash en niveles grandes
static constexpr unsigned int MAX_OBJ = 250;

// ============================================================
//  UTILIDADES
// ============================================================

static bool isHazard(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType == GameObjectType::Decoration) return false;
    // Ignorar objetos con NoTouch (no colisionan con el jugador)
    if (obj->m_noTouch) return false;
    return HAZARD_IDS.count(obj->m_objectID) > 0;
}

static bool isSolidObstacle(GameObject* obj) {
    if (!obj) return false;
    if (obj->m_objectType != GameObjectType::Solid) return false;
    // Ignorar NoTouch
    if (obj->m_noTouch) return false;
    return true;
}

// ============================================================
//  HOOK: CCScheduler - TPS Bypass
// ============================================================

class $modify(AIScheduler, CCScheduler) {
    void update(float dt) {
        if (g_cfg.tpsBypass && PlayLayer::get()) {
            CCScheduler::update(1.0f / g_cfg.tpsValue);
        } else {
            CCScheduler::update(dt);
        }
    }
};

// ============================================================
//  HOOK: FMODAudioEngine - Speedhack musica
// ============================================================

class $modify(AIAudio, FMODAudioEngine) {
    void update(float dt) {
        if (g_cfg.speedhack && g_cfg.speedMusic && PlayLayer::get()) {
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
        float useDt = (g_cfg.speedhack && !g_cfg.speedProgress)
                      ? dt * g_cfg.speedMult
                      : dt;
        PlayLayer::update(useDt);

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

        // ---- Deteccion segura de objetos ----
        auto* objects = m_objects;
        if (!objects || objects->count() == 0) return;

        CCPoint playerPos = player->getPosition();
        bool shouldPress  = false;

        unsigned int total   = objects->count();
        unsigned int checked = 0;

        for (unsigned int i = 0; i < total; i++) {
            // Salir si el array cambio de tamano (nivel reiniciado, etc.)
            if (!m_objects || m_objects->count() != total) break;
            if (checked >= MAX_OBJ) break;

            CCObject* raw = objects->objectAtIndex(i);
            if (!raw) continue;

            auto* obj = typeinfo_cast<GameObject*>(raw);
            if (!obj) continue;
            if (!obj->isVisible()) continue;
            if (obj->m_objectType == GameObjectType::Decoration) continue;
            if (obj->m_noTouch) continue;   // ignorar NoTouch

            checked++;

            CCPoint objPos = obj->getPosition();
            float   deltaX = objPos.x - playerPos.x;

            // Solo adelante del jugador
            if (deltaX < -10.0f || deltaX > LOOKAHEAD_X) continue;

            float deltaY = std::abs(objPos.y - playerPos.y);
            if (deltaY > LOOKAHEAD_Y) continue;

            if (isHazard(obj)) {
                shouldPress = true;
                break;
            }

            if (isSolidObstacle(obj)) {
                if (deltaX < SOLID_X && objPos.y >= playerPos.y - SOLID_Y) {
                    shouldPress = true;
                    break;
                }
            }
        }

        // ---- Control boton ----
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

    // Safe Mode = noclip (no registra verificacion)
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_cfg.safeMode) return;
        AIPlayLayer::destroyPlayer(player, obj);
    }

    // Speedhack en progreso: escalar tiempo interno
    void updateProgressbar() {
        if (g_cfg.speedhack && g_cfg.speedProgress) {
            // El porcentaje se calcula normal, sin modificar
        }
        AIPlayLayer::updateProgressbar();
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

// Input node helper - recibe texto y llama callback
class TextInputDelegate : public CCTextFieldDelegate {
public:
    std::function<void(const std::string&)> onTextChanged;

    bool onTextFieldInsertText(CCTextFieldTTF* field, const char* text, int len) override {
        return false;
    }

    bool onTextFieldDeleteBackward(CCTextFieldTTF* field, const char* deleteText, int len) override {
        return false;
    }

    bool onTextFieldDetachWithIME(CCTextFieldTTF* field) override {
        if (onTextChanged) onTextChanged(field->getString());
        return false;
    }
};

static TextInputDelegate* g_tpsDelegate   = nullptr;
static TextInputDelegate* g_speedDelegate = nullptr;

class $modify(AIPauseLayer, PauseLayer) {

    CCMenuItemToggle* makeToggle(const char* on, const char* off,
                                  bool state, SEL_MenuHandler sel) {
        auto lOn  = CCLabelBMFont::create(on,  "bigFont.fnt");
        auto lOff = CCLabelBMFont::create(off, "bigFont.fnt");
        lOn->setScale(0.4f);  lOff->setScale(0.4f);
        lOn->setColor({0, 230, 80}); lOff->setColor({230, 60, 60});

        auto iOn  = CCMenuItemLabel::create(lOn,  this, sel);
        auto iOff = CCMenuItemLabel::create(lOff, this, sel);

        auto t = CCMenuItemToggle::createWithTarget(this, sel, iOff, iOn, nullptr);
        t->setSelectedIndex(state ? 1 : 0);
        return t;
    }

    CCLabelBMFont* makeLabel(const char* text) {
        auto l = CCLabelBMFont::create(text, "bigFont.fnt");
        l->setScale(0.36f);
        l->setColor({200, 220, 255});
        return l;
    }

    CCTextInputNode* makeInput(const std::string& placeholder, float width) {
        auto input = CCTextInputNode::create(width, 20.0f, placeholder.c_str(), "bigFont.fnt");
        input->setMaxLabelScale(0.4f);
        input->setAllowedChars("0123456789.");
        return input;
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        float panelX = winSize.width - 90.0f;
        float panelY = winSize.height / 2.0f;

        // Fondo del panel
        auto bg = CCScale9Sprite::create("square02_small.png");
        bg->setContentSize({172.0f, 200.0f});
        bg->setPosition({panelX, panelY});
        bg->setOpacity(200);
        bg->setColor({10, 10, 20});
        bg->setZOrder(9);
        this->addChild(bg);

        // Titulo
        auto title = CCLabelBMFont::create("AutoPlay AI", "goldFont.fnt");
        title->setScale(0.48f);
        title->setPosition({panelX, panelY + 88.0f});
        this->addChild(title, 11);

        auto menu = CCMenu::create();
        menu->setPosition({panelX, panelY});
        menu->setZOrder(10);

        float rowY = 70.0f;
        float rowH = 38.0f;

        // ---- Activate AI ----
        {
            auto lbl = makeLabel("Activate AI");
            lbl->setPosition({-40.0f, rowY});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.aiEnabled,
                menu_selector(AIPauseLayer::onToggleAI));
            tog->setPosition({58.0f, rowY});
            menu->addChild(tog);
            rowY -= rowH;
        }

        // ---- Safe Mode ----
        {
            auto lbl = makeLabel("Safe Mode");
            lbl->setPosition({-40.0f, rowY});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.safeMode,
                menu_selector(AIPauseLayer::onToggleSafeMode));
            tog->setPosition({58.0f, rowY});
            menu->addChild(tog);
            rowY -= rowH;
        }

        // ---- Speedhack ----
        {
            auto lbl = makeLabel("Speedhack");
            lbl->setPosition({-40.0f, rowY});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.speedhack,
                menu_selector(AIPauseLayer::onToggleSpeedhack));
            tog->setPosition({58.0f, rowY});
            menu->addChild(tog);
            rowY -= 22.0f;

            // Input velocidad
            auto speedInput = makeInput(
                std::to_string((int)(g_cfg.speedMult * 100)) + "%", 60.0f);
            speedInput->setPosition({-30.0f, rowY});
            speedInput->setTag(201);
            menu->addChild(speedInput);

            // Subtoggle: musica
            auto lblM = makeLabel("Music");
            lblM->setScale(0.3f);
            lblM->setPosition({20.0f, rowY});
            menu->addChild(lblM);

            auto togM = makeToggle("Y", "N", g_cfg.speedMusic,
                menu_selector(AIPauseLayer::onToggleSpeedMusic));
            togM->setScale(0.7f);
            togM->setPosition({50.0f, rowY});
            menu->addChild(togM);

            rowY -= 22.0f;

            // Subtoggle: progreso
            auto lblP = makeLabel("Progress");
            lblP->setScale(0.3f);
            lblP->setPosition({-20.0f, rowY});
            menu->addChild(lblP);

            auto togP = makeToggle("Y", "N", g_cfg.speedProgress,
                menu_selector(AIPauseLayer::onToggleSpeedProgress));
            togP->setScale(0.7f);
            togP->setPosition({50.0f, rowY});
            menu->addChild(togP);

            rowY -= rowH;
        }

        // ---- TPS Bypass ----
        {
            auto lbl = makeLabel("TPS Bypass");
            lbl->setPosition({-40.0f, rowY});
            menu->addChild(lbl);

            auto tog = makeToggle("ON", "OFF", g_cfg.tpsBypass,
                menu_selector(AIPauseLayer::onToggleTPS));
            tog->setPosition({58.0f, rowY});
            menu->addChild(tog);
            rowY -= 22.0f;

            // Input TPS
            auto tpsInput = makeInput(
                std::to_string((int)g_cfg.tpsValue) + " fps", 70.0f);
            tpsInput->setPosition({0.0f, rowY});
            tpsInput->setTag(202);
            menu->addChild(tpsInput);
        }

        this->addChild(menu);

        // Guardar referencias a inputs para leer valores
        // Usamos Schedule para revisar inputs cada 0.5s
        this->schedule(schedule_selector(AIPauseLayer::checkInputs), 0.5f);
    }

    void checkInputs(float dt) {
        auto* menu = this->getChildByTag(10);  // no aplica, buscar por posicion

        // Leer input de velocidad
        auto* speedNode = this->getChildByTag(201);
        if (speedNode) {
            auto* input = typeinfo_cast<CCTextInputNode*>(speedNode);
            if (input && strlen(input->getString()) > 0) {
                float val = std::atof(std::string(input->getString()).c_str());
                if (val > 0.0f && val <= 1000.0f) {
                    g_cfg.speedMult = val / 100.0f;
                }
            }
        }

        // Leer input TPS
        auto* tpsNode = this->getChildByTag(202);
        if (tpsNode) {
            auto* input = typeinfo_cast<CCTextInputNode*>(tpsNode);
            if (input && strlen(input->getString()) > 0) {
                float val = std::atof(std::string(input->getString()).c_str());
                if (val >= 30.0f && val <= 1000.0f) {
                    g_cfg.tpsValue = val;
                }
            }
        }
    }

    void onToggleAI(CCObject*) {
        g_cfg.aiEnabled = !g_cfg.aiEnabled;
        Notification::create(
            g_cfg.aiEnabled ? "AI: ON" : "AI: OFF",
            NotificationIcon::Success)->show();
        if (!g_cfg.aiEnabled && g_buttonHeld) {
            auto* pl = PlayLayer::get();
            if (pl && pl->m_player1) pl->m_player1->releaseButton(PlayerButton::Jump);
            g_buttonHeld = false;
        }
    }

    void onToggleSafeMode(CCObject*) {
        g_cfg.safeMode = !g_cfg.safeMode;
        Notification::create(
            g_cfg.safeMode ? "Safe Mode: ON (Noclip)" : "Safe Mode: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleSpeedhack(CCObject*) {
        g_cfg.speedhack = !g_cfg.speedhack;
        Notification::create(
            g_cfg.speedhack ? "Speedhack: ON" : "Speedhack: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleSpeedMusic(CCObject*) {
        g_cfg.speedMusic = !g_cfg.speedMusic;
        Notification::create(
            g_cfg.speedMusic ? "Speed Music: ON" : "Speed Music: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleSpeedProgress(CCObject*) {
        g_cfg.speedProgress = !g_cfg.speedProgress;
        Notification::create(
            g_cfg.speedProgress ? "Speed Progress: ON" : "Speed Progress: OFF",
            NotificationIcon::Success)->show();
    }

    void onToggleTPS(CCObject*) {
        g_cfg.tpsBypass = !g_cfg.tpsBypass;
        Notification::create(
            g_cfg.tpsBypass ? "TPS Bypass: ON" : "TPS Bypass: OFF",
            NotificationIcon::Success)->show();
    }
};

// ============================================================
//  ENTRADA
// ============================================================

$on_mod(Loaded) {
    log::info("AutoPlay AI v1.2.0 loaded.");
}
