// Click Assist para Geometry Dash (Geode)
//
// - Si haces click demasiado pronto, lo retrasa hasta el primer frame (0..max-delay)
//   en el que una simulación indica que sobrevives.
// - Si sueltas antes de tiempo, el soltar se retrasa igual y se garantiza que el
//   click dure al menos 1 frame: fire = max(S + d, R + d + 1).
// - Funciona con cualquier vehículo porque usa la física real del juego.
// - Se activa automáticamente desde el intento N de cada nivel (por defecto 3).
//   Cuenta también los respawns del modo práctica.
//
// Nota: los nombres exactos de miembros/métodos pueden variar según la versión
// del SDK de Geode. Si algo no compila, revisa los bindings de tu versión.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <deque>

using namespace geode::prelude;

namespace {

struct Event {
    int fireFrame;
    bool down;
};

// Estado compartido entre los hooks de GJBaseGameLayer y PlayLayer.
struct State {
    std::deque<Event> queue;
    int frame = 0;
    int attempt = 1;
    int activeDelay = 0;   // d elegido para el hold actual
    int pressFire = 0;     // frame en que se ejecuta el click retrasado
    bool holding = false;
    bool injecting = false; // evita reinterceptar nuestros propios eventos
    bool simulating = false;
    bool simDied = false;

    void resetRun() {
        queue.clear();
        holding = false;
        frame = 0;
        activeDelay = 0;
        pressFire = 0;
    }
};

State g;

bool assistActive() {
    auto mod = Mod::get();
    if (!mod->getSettingValue<bool>("enabled")) return false;
    return g.attempt >= mod->getSettingValue<int64_t>("start-attempt");
}

} // namespace

class $modify(AssistBaseLayer, GJBaseGameLayer) {

    // Simula hacia delante con el click ejecutado en el frame `delay`.
    // Devuelve true si el jugador sobrevive todo el horizonte.
    bool survives(int delay, int horizon) {
        auto pl = PlayLayer::get();
        if (!pl) return true;

        auto cp = pl->createCheckpointObject(); // guardar estado
        g.simulating = true;
        g.simDied = false;

        for (int f = 0; f < horizon && !g.simDied; f++) {
            if (f == delay) {
                g.injecting = true;
                GJBaseGameLayer::handleButton(true, 1, true);
                g.injecting = false;
            }
            GJBaseGameLayer::update(1.f / 240.f); // paso fijo
        }

        g.simulating = false;
        pl->loadFromCheckpoint(cp); // restaurar estado
        return !g.simDied;
    }

    void handleButton(bool down, int button, bool isP1) {
        auto pl = PlayLayer::get();
        bool isMainLayer = pl && static_cast<GJBaseGameLayer*>(pl) == this;

        if (g.injecting || g.simulating || !isP1 || !isMainLayer || !assistActive()) {
            return GJBaseGameLayer::handleButton(down, button, isP1);
        }

        if (down) {
            int maxD = (int)Mod::get()->getSettingValue<int64_t>("max-delay");

            int d = 0;
            bool found = false;
            for (; d <= maxD; d++) {
                if (survives(d, maxD + 14)) { found = true; break; }
            }
            if (!found) d = 0; // ningún retraso <= maxD salva: se respeta tu click

            g.activeDelay = d;
            g.pressFire = g.frame + d;
            g.holding = true;
            g.queue.push_back({g.pressFire, true});
        } else if (g.holding) {
            // Soltar: conserva el hold y exige mínimo 1 frame tras el click.
            int fire = std::max(g.frame + g.activeDelay, g.pressFire + 1);
            g.queue.push_back({fire, false});
            g.holding = false;
        } else {
            // Soltar sin click pendiente: pasa directo.
            GJBaseGameLayer::handleButton(down, button, isP1);
        }
        // Click y soltar con asistencia salen por la cola, no directos.
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        bool isMainLayer = pl && static_cast<GJBaseGameLayer*>(pl) == this;

        if (isMainLayer && !g.simulating) {
            g.frame++;
            while (!g.queue.empty() && g.queue.front().fireFrame <= g.frame) {
                auto e = g.queue.front();
                g.queue.pop_front();
                g.injecting = true;
                GJBaseGameLayer::handleButton(e.down, 1, true);
                g.injecting = false;
            }
        }
        GJBaseGameLayer::update(dt);
    }
};

class $modify(AssistPlayLayer, PlayLayer) {

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        g = State(); // contador de intentos y cola desde cero en cada nivel
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (g.simulating) return; // un reset causado por la simulación no cuenta
        g.resetRun();
        g.attempt++; // cuenta también en modo práctica
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g.simulating) {
            g.simDied = true; // muerte simulada: no matar de verdad
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }
};
