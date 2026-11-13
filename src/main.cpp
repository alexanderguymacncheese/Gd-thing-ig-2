#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    bool bufferActive() {
        return Mod::get()->getSettingValue<bool>("enabled") && PlayLayer::get() != nullptr;
    }

    Clock::duration bufferWindow() {
        auto frames = Mod::get()->getSettingValue<int64_t>("buffer-frames");
        // frames of a 60 FPS recording -> real time
        return std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::milli>(static_cast<double>(frames) * 1000.0 / 60.0)
        );
    }
}

class $modify(BufferPlayer, PlayerObject) {
    struct Fields {
        bool armed = false;          // a click is waiting to be applied
        Clock::time_point deadline;  // when the waiting click expires
        bool held = false;           // jump is physically held right now
        bool internal = false;       // true while this mod calls the game itself
        bool pendingRelease = false; // release our fake press on the next update
    };

    // Can a jump press do something right now?
    bool canActivate() {
        auto mod = Mod::get();
        if (mod->getSettingValue<bool>("buffer-orbs") && m_touchedRing) {
            return true;
        }
        if (mod->getSettingValue<bool>("buffer-ground") && m_isOnGround
            && !m_isShip && !m_isBird && !m_isDart && !m_isSwing) {
            return true;
        }
        return false;
    }

    void clearBuffer() {
        auto f = m_fields.self();
        f->armed = false;
        f->held = false;
        f->internal = false;
        f->pendingRelease = false;
    }

    bool pushButton(PlayerButton button) {
        auto f = m_fields.self();
        if (f->internal || button != PlayerButton::Jump || !bufferActive()) {
            return PlayerObject::pushButton(button);
        }

        f->held = true;
        f->pendingRelease = false;

        // If the click can act right now the game handles it normally.
        // Otherwise remember it for a few frames.
        bool couldAct = this->canActivate();
        bool result = PlayerObject::pushButton(button);

        if (couldAct) {
            f->armed = false;
        } else {
            f->armed = true;
            f->deadline = Clock::now() + bufferWindow();
        }
        return result;
    }

    bool releaseButton(PlayerButton button) {
        auto f = m_fields.self();
        if (!f->internal && button == PlayerButton::Jump) {
            f->held = false;
        }
        return PlayerObject::releaseButton(button);
    }

    void update(float dt) {
        PlayerObject::update(dt);

        auto f = m_fields.self();

        // Let go of a buffered press one step after it was applied,
        // if the player had already released the button.
        if (f->pendingRelease) {
            f->pendingRelease = false;
            if (!f->held) {
                f->internal = true;
                PlayerObject::releaseButton(PlayerButton::Jump);
                f->internal = false;
            }
        }

        if (!f->armed) return;

        if (!bufferActive() || Clock::now() > f->deadline) {
            f->armed = false;
            return;
        }

        if (!this->canActivate()) return;

        // Landed or touched an orb inside the window: apply the early click.
        f->armed = false;
        f->internal = true;
        PlayerObject::pushButton(PlayerButton::Jump);
        f->internal = false;
        f->pendingRelease = !f->held;
    }
};

class $modify(BufferPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        if (m_player1) static_cast<BufferPlayer*>(m_player1)->clearBuffer();
        if (m_player2) static_cast<BufferPlayer*>(m_player2)->clearBuffer();
    }
};
