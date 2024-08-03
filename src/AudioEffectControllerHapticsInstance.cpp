#include "AudioEffectControllerHapticsInstance.h"

#include <format>
#include <godot_cpp/classes/engine.hpp>

#define MINIAUDIO_IMPLEMENTATION

#if defined(DEBUG_ENABLED)
    #define MA_DEBUG_OUTPUT
#endif

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wduplicated-branches"
#endif
#include "external/miniaudio.h"
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif


#include "godot_cpp/classes/audio_server.hpp"

#define HANDLE_MA_ERROR(ma_result)                                                                                                                             \
    if (ma_result != MA_SUCCESS) {                                                                                                                             \
        uninitialize_miniaudio();                                                                                                                              \
        ERR_FAIL_MSG(std::format("miniaudio: {}", ma_result_description(result)).c_str());                                                                     \
    }

namespace hd_haptics {
    constexpr int INPUT_CHANNELS = 2;
    constexpr int OUTPUT_CHANNELS = 4;

    auto channel_weights = new float* [INPUT_CHANNELS] { new float[OUTPUT_CHANNELS]{0.f, 0.f, 1.f, 0.f}, new float[OUTPUT_CHANNELS]{0.f, 0.f, 0.f, 1.f}, };

    std::optional<ma_device_id> get_dualsense_audio_device_id() {
        ma_context context;
        ma_device_info* playback_device_infos;
        ma_uint32 playback_device_count;
        ma_device_info* capture_device_infos;
        ma_uint32 capture_device_count;

        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
            ma_context_uninit(&context);
            ERR_FAIL_V_MSG(std::nullopt, "Failed to initialize context.");
        }

        ma_result result = ma_context_get_devices(&context, &playback_device_infos, &playback_device_count, &capture_device_infos, &capture_device_count);
        if (result != MA_SUCCESS) {
            ma_context_uninit(&context);
            ERR_FAIL_V_MSG(std::nullopt, "Failed to retrieve device information");
        }

        for (ma_uint32 i_device = 0; i_device < playback_device_count; ++i_device) {
            const auto playback_device_info = playback_device_infos[i_device];
            std::string name(playback_device_info.name);
            if (name.contains("DualSense")) {
                return playback_device_info.id;
            }
        }

        ma_context_uninit(&context);
        return std::nullopt;
    }

    void AudioEffectControllerHapticsInstance::output_data_callback(ma_device* p_device, void* output, const void* input, ma_uint32 p_frame_count) {
        const auto instance = static_cast<AudioEffectControllerHapticsInstance*>(p_device->pUserData);

        ma_uint32 frames_written = 0;

        while (frames_written < p_frame_count) {
            void* p_mapped_buffer;
            ma_uint32 frames_to_write = p_frame_count - frames_written;

            ma_result result = ma_pcm_rb_acquire_read(&*instance->m_ring_buffer, &frames_to_write, &p_mapped_buffer);

            if (result != MA_SUCCESS) {
                break;
            }

            if (frames_to_write == 0) {
                break;
            }

            ma_channel_converter_process_pcm_frames(&*instance->m_channel_converter, output, p_mapped_buffer, frames_to_write);

            result = ma_pcm_rb_commit_read(&*instance->m_ring_buffer, frames_to_write);
            if (result != MA_SUCCESS) {
                break;
            }

            frames_written += frames_to_write;
        }
    }

    AudioEffectControllerHapticsInstance::AudioEffectControllerHapticsInstance() {
        auto device_id = get_dualsense_audio_device_id();

        ERR_FAIL_COND_MSG(!device_id.has_value(), "Did not find DualSense device");

        auto sample_rate = static_cast<uint32_t>(godot::AudioServer::get_singleton()->get_mix_rate());

        m_ring_buffer = std::make_optional<ma_pcm_rb>();

        ma_result result;

        result = ma_pcm_rb_init(ma_format_f32, INPUT_CHANNELS, 1024 * 5000, nullptr, nullptr, &*m_ring_buffer);
        HANDLE_MA_ERROR(result);

        m_ring_buffer->sampleRate = sample_rate;

        ma_channel device_output_map[OUTPUT_CHANNELS] = {MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT};

        ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
        device_config.sampleRate = sample_rate;
        device_config.performanceProfile = ma_performance_profile_low_latency;
        device_config.noPreSilencedOutputBuffer = MA_TRUE;
        device_config.noClip = MA_TRUE;
        device_config.noFixedSizedCallback = MA_TRUE;
        device_config.playback.format = ma_format_f32;
        device_config.playback.channels = OUTPUT_CHANNELS;
        device_config.playback.pChannelMap = device_output_map;
        device_config.playback.shareMode = ma_share_mode_shared;
        device_config.pUserData = this;
        device_config.dataCallback = output_data_callback;
        device_config.playback.pDeviceID = &device_id.value();

#if defined(MA_HAS_PULSEAUDIO)
        device_config.pulse.pChannelMap = MA_PA_CHANNEL_MAP_ALSA;
#endif

        m_device = std::make_optional<ma_device>();
        result = ma_device_init(nullptr, &device_config, &*m_device);
        HANDLE_MA_ERROR(result);

        constexpr ma_channel input_map[INPUT_CHANNELS] = {MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT};
        constexpr ma_channel output_map[OUTPUT_CHANNELS] = {MA_CHANNEL_FRONT_LEFT, MA_CHANNEL_FRONT_RIGHT, MA_CHANNEL_BACK_LEFT, MA_CHANNEL_BACK_RIGHT};
        ma_channel_converter_config converter_config = ma_channel_converter_config_init(
            ma_format_f32, INPUT_CHANNELS, input_map, OUTPUT_CHANNELS, output_map, ma_channel_mix_mode_simple
        );

        m_channel_converter = std::make_optional<ma_channel_converter>();
        result = ma_channel_converter_init(&converter_config, nullptr, &*m_channel_converter);
        HANDLE_MA_ERROR(result);

        ma_device_start(&*m_device);
    }

    AudioEffectControllerHapticsInstance::~AudioEffectControllerHapticsInstance() {
        uninitialize_miniaudio();
    }

    void AudioEffectControllerHapticsInstance::_process(const void* p_src_buffer, godot::AudioFrame* p_dst_buffer, int32_t p_frame_count) {
        /* We need to write to the ring buffer. Need to do this in a loop. */
        int32_t frames_written = 0;

        while (frames_written < p_frame_count) {
            void* p_mapped_buffer;
            ma_uint32 frames_to_write = p_frame_count - frames_written;

            ma_result result = ma_pcm_rb_acquire_write(&*m_ring_buffer, &frames_to_write, &p_mapped_buffer);
            if (result != MA_SUCCESS) {
                break;
            }

            if (frames_to_write == 0) {
                break;
            }

            /* Copy the data from the capture buffer to the ring buffer. */
            ma_copy_pcm_frames(
                p_mapped_buffer,
                ma_offset_pcm_frames_const_ptr_f32(static_cast<const float*>(p_src_buffer), frames_written, INPUT_CHANNELS),
                frames_to_write,
                ma_format_f32,
                INPUT_CHANNELS
            );

            result = ma_pcm_rb_commit_write(&*m_ring_buffer, frames_to_write);

            if (result != MA_SUCCESS) {
                break;
            }

            frames_written += static_cast<int32_t>(frames_to_write);
        }
    }

    bool AudioEffectControllerHapticsInstance::_process_silence() const {
        return true;
    }

    void AudioEffectControllerHapticsInstance::uninitialize_miniaudio() {
        if (m_device.has_value()) {
            ma_device_uninit(&*m_device);
            m_device = std::nullopt;
        }

        if (m_channel_converter.has_value()) {
            ma_channel_converter_uninit(&*m_channel_converter, nullptr);
            m_channel_converter = std::nullopt;
        }

        if (m_ring_buffer.has_value()) {
            ma_pcm_rb_uninit(&*m_ring_buffer);
            m_ring_buffer = std::nullopt;
        }
    }

    void AudioEffectControllerHapticsInstance::_bind_methods() {}
} // namespace hd_haptics
