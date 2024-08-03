#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "godot_cpp/classes/audio_effect.hpp"

namespace hd_haptics {
    class AudioEffectControllerHaptics : public godot::AudioEffect {
        GDCLASS(AudioEffectControllerHaptics, AudioEffect)

    public:
        godot::Ref<godot::AudioEffectInstance> _instantiate() override;

    protected:
        static void _bind_methods();
    };
} // namespace hd_haptics
