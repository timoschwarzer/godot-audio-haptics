#include "AudioEffectControllerHaptics.h"
#include <godot_cpp/classes/engine.hpp>

#include "AudioEffectControllerHapticsInstance.h"

namespace hd_haptics {
    godot::Ref<godot::AudioEffectInstance> AudioEffectControllerHaptics::_instantiate() {
        godot::Ref<AudioEffectControllerHapticsInstance> instance;
        instance.instantiate();
        instance->base = godot::Ref(this);
        return instance;
    }

    void AudioEffectControllerHaptics::_bind_methods() {}
} // namespace hd_haptics
