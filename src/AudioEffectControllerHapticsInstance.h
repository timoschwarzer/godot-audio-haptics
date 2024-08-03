#pragma once

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/wrapped.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <optional>
#include "AudioEffectControllerHaptics.h"

#include <thread>
#include "external/miniaudio_init.h"

namespace hd_haptics {
    class AudioEffectControllerHapticsInstance : public godot::AudioEffectInstance {
        GDCLASS(AudioEffectControllerHapticsInstance, AudioEffectInstance)

        friend class AudioEffectControllerHaptics;
        godot::Ref<AudioEffectControllerHaptics> base;

    public:
        AudioEffectControllerHapticsInstance();
        ~AudioEffectControllerHapticsInstance() override;
        void _process(const void* p_src_buffer, godot::AudioFrame* p_dst_buffer, int32_t p_frame_count) override;
        bool _process_silence() const override;

    protected:
        std::optional<ma_pcm_rb> m_ring_buffer = std::nullopt;
        std::optional<ma_device> m_device = std::nullopt;
        std::optional<ma_channel_converter> m_channel_converter = std::nullopt;

        std::atomic<bool> m_stop_device_availability_check = false;
        std::thread m_device_availability_check;
        std::mutex m_initialization_mutex;

        void try_initialize_miniaudio(const ma_device_id& device_id);
        void uninitialize_miniaudio();

        static void output_data_callback(ma_device* p_device, void* output, const void* input, ma_uint32 p_frame_count);
        static void device_notification_callback(const ma_device_notification* notification);
        static void _bind_methods();
    };
} // namespace hd_haptics
