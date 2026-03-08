// ============================================================
//  AutoPlay AI - Mod para Geometry Dash 2.2074 (Android)
//  Geode SDK | Desarrollado con C++20
// ============================================================

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/cocos/include/cocos2d.h>

using namespace geode::prelude;
using namespace cocos2d;

// ============================================================
//  ESTADO GLOBAL DE LA IA
// ============================================================

static bool g_aiEnabled   = false;   // ¿Está la IA activada?
static bool g_buttonHeld  = false;   // ¿Está el botón presionado actualmente?

// ============================================================
//  PARÁMETROS DE DETECCIÓN DE OBSTÁCULOS
// ============================================================

// Distancia horizontal (en unidades de juego) para buscar obstáculos
static constexpr float DETECTION_RANGE_X = 120.0f;

// Distancia vertical máxima para considerar un obstáculo relevante
static constexpr float DETECTION_RANGE_Y = 80.0f;

// IDs de objetos peligrosos (spikes, saws, etc.)
static const std::vector<int> HAZARD_IDS = {
    8, 39, 40, 49,        // Spikes básicos
    148, 149, 150,        // Saws
    184, 185,             // Sawblades giratorias
    1, 2, 3, 4,           // Bloques peligrosos
    395, 396,             // Spikes invertidos
    1331, 1332,           // Obstáculos especiales
};

// ============================================================
//  UTILIDAD: ¿Es un objeto peligroso?
// ============================================================

bool isHazardObject(GameObject* obj) {
    if (!obj) return false;

    // Decoraciones nunca son peligrosas
    if (obj->m_objectType == GameObjectType::Decoration) return false;

    int objID = obj->m_objectID;
    for (int id : HAZARD_IDS) {
        if (objID == id) return true;
    }

    return false;
}

// ============================================================
//  HOOK EN PlayLayer
// ============================================================

class $modify(AIPlayLayer, PlayLayer) {

    void update(float dt) {
        // Ejecutar el update original SIEMPRE primero
        PlayLayer::update(dt);

        if (!g_aiEnabled) return;
        if (!m_player1 || !m_isAlive) return;

        CCPoint playerPos = m_player1->getPosition();

        // --------------------------------------------------------
        //  DETECCIÓN DE OBSTÁCULOS
        // --------------------------------------------------------
        bool shouldPress = false;

        if (m_objects) {
            for (unsigned int i = 0; i < m_objects->count(); i++) {
                auto* obj = static_cast<GameObject*>(m_objects->objectAtIndex(i));
                if (!obj) continue;

                if (obj->m_objectType == GameObjectType::Decoration) continue;

                CCPoint objPos = obj->getPosition();

                // Solo objetos ADELANTE del jugador
                float deltaX = objPos.x - playerPos.x;
                if (deltaX < 0.0f || deltaX > DETECTION_RANGE_X) continue;

                // Rango vertical
                float deltaY = std::abs(objPos.y - playerPos.y);
                if (deltaY > DETECTION_RANGE_Y) continue;

                if (isHazardObject(obj)) {
                    shouldPress = true;
                    break;
                }

                // Saltar sobre bloques sólidos a la misma altura
                if (obj->m_objectType == GameObjectType::Solid) {
                    if (objPos.y >= playerPos.y - 20.0f && deltaX < 80.0f) {
                        shouldPress = true;
                        break;
                    }
                }
            }
        }

        // --------------------------------------------------------
        //  CONTROL DEL BOTÓN
        // --------------------------------------------------------
        if (shouldPress && !g_buttonHeld) {
            this->pushButton(0, true);
            g_buttonHeld = true;
        } else if (!shouldPress && g_buttonHeld) {
            this->releaseButton(0, true);
            g_buttonHeld = false;
        }
    }

    void resetLevel() {
        // FIX: llamar al original correctamente con Geode $modify
        AIPlayLayer::resetLevel();
        g_buttonHeld = false;
        if (m_player1) {
            this->releaseButton(0, true);
        }
    }

    void onQuit() {
        g_buttonHeld = false;
        AIPlayLayer::onQuit();
    }
};

// ============================================================
//  GUI: BOTÓN EN PauseLayer
// ============================================================

class $modify(AIPauseLayer, PauseLayer) {

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // FIX: usar CCLabelBMFont para archivos .fnt (NO CCLabelTTF)
        auto labelOn  = CCLabelBMFont::create("AI: ON",  "bigFont.fnt");
        auto labelOff = CCLabelBMFont::create("AI: OFF", "bigFont.fnt");

        labelOn->setScale(0.6f);
        labelOff->setScale(0.6f);

        auto itemOn  = CCMenuItemLabel::create(labelOn,  this,
            menu_selector(AIPauseLayer::onAIToggle));
        auto itemOff = CCMenuItemLabel::create(labelOff, this,
            menu_selector(AIPauseLayer::onAIToggle));

        auto toggleBtn = CCMenuItemToggle::createWithTarget(
            this,
            menu_selector(AIPauseLayer::onAIToggle),
            itemOff, itemOn, nullptr
        );

        toggleBtn->setSelectedIndex(g_aiEnabled ? 1 : 0);
        toggleBtn->setTag(100);

        // Color según estado
        labelOn->setColor({0, 255, 100});
        labelOff->setColor({255, 80, 80});

        auto menu = CCMenu::create();
        menu->addChild(toggleBtn);
        menu->setPosition({winSize.width - 60.0f, 30.0f});
        menu->setZOrder(10);
        this->addChild(menu);

        // Etiqueta descriptiva
        auto descLabel = CCLabelBMFont::create("AutoPlay AI", "chatFont.fnt");
        descLabel->setScale(0.5f);
        descLabel->setPosition({winSize.width - 60.0f, 52.0f});
        descLabel->setColor({255, 255, 255});
        descLabel->setOpacity(180);
        this->addChild(descLabel, 10);
    }

    void onAIToggle(CCObject* sender) {
        g_aiEnabled = !g_aiEnabled;

        std::string msg = g_aiEnabled
            ? "AutoPlay AI: ACTIVADO"
            : "AutoPlay AI: DESACTIVADO";

        Notification::create(msg, NotificationIcon::Success)->show();

        if (!g_aiEnabled && g_buttonHeld) {
            auto* pl = PlayLayer::get();
            if (pl) {
                pl->releaseButton(0, true);
            }
            g_buttonHeld = false;
        }

        log::info("AutoPlay AI toggled: {}", g_aiEnabled ? "ON" : "OFF");
    }
};

// ============================================================
//  PUNTO DE ENTRADA
// ============================================================

$on_mod(Loaded) {
    log::info("AutoPlay AI v1.0.0 cargado correctamente.");
    log::info("Abre PauseLayer para activar/desactivar la IA.");
}

