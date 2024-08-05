#include "AudioEffectControllerHapticsInstance.h"

#include <format>
#include <godot_cpp/classes/engine.hpp>

#define MINIAUDIO_IMPLEMENTATION
#include "external/miniaudio_init.h"


#include "godot_cpp/classes/audio_server.hpp"

#define HANDLE_MA_ERROR(ma_result)                                                                                                                             \
    if (ma_result != MA_SUCCESS) {                                                                                                                             \
        uninitialize_miniaudio();                                                                                                                              \
        ERR_FAIL_MSG(std::format("miniaudio: {}", ma_result_description(result)).c_str());                                                                     \
    }

namespace hd_haptics {
    constexpr int INPUT_CHANNELS = 2;
    constexpr int OUTPUT_CHANNELS = 4;

    std::optional<ma_context> context;

    std::optional<ma_device_id> get_dualsense_audio_device_id() {
        ma_device_info* playback_device_infos;
        ma_uint32 playback_device_count;

        constexpr std::array backends = {
            ma_backend_pulseaudio,
            ma_backend_wasapi,
        };

        if (!context.has_value()) {
            context = std::make_optional<ma_context>();

            if (ma_context_init(backends.data(), static_cast<ma_uint32>(backends.size()), nullptr, &*context) != MA_SUCCESS) {
                ma_context_uninit(&*context);
                context = std::nullopt;
                ERR_FAIL_V_MSG(std::nullopt, "Failed to initialize context.");
            }
        }

        ma_result result = ma_context_get_devices(&*context, &playback_device_infos, &playback_device_count, nullptr, nullptr);
        if (result != MA_SUCCESS) {
            ma_context_uninit(&*context);
            context = std::nullopt;
            ERR_FAIL_V_MSG(std::nullopt, "Failed to retrieve device information");
        }

        for (ma_uint32 i_device = 0; i_device < playback_device_count; ++i_device) {
            const auto playback_device_info = playback_device_infos[i_device];
            std::string name(playback_device_info.name);
            if (name.contains("DualSense")) {
                return playback_device_info.id;
            }
        }

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

            result = ma_channel_converter_process_pcm_frames(&*instance->m_channel_converter, output, p_mapped_buffer, frames_to_write);
            if (result != MA_SUCCESS) {
                break;
            }

            result = ma_pcm_rb_commit_read(&*instance->m_ring_buffer, frames_to_write);
            if (result != MA_SUCCESS) {
                break;
            }

            frames_written += frames_to_write;
        }
    }

    void AudioEffectControllerHapticsInstance::device_notification_callback(const ma_device_notification* notification) {
        const auto instance = static_cast<AudioEffectControllerHapticsInstance*>(notification->pDevice->pUserData);

        if (notification->type == ma_device_notification_type_stopped) {
            instance->uninitialize_miniaudio();
        }
    }

    AudioEffectControllerHapticsInstance::AudioEffectControllerHapticsInstance() {
        // First time initialization
        {
            auto device_id = get_dualsense_audio_device_id();

            if (device_id.has_value()) {
                try_initialize_miniaudio(*device_id);
            } else {
                WARN_PRINT("Did not find a compatible Audio Haptics device");
            }
        }

        m_device_availability_check = std::thread([this] {
            while (!m_stop_device_availability_check) {
                {
                    std::unique_lock lock(m_initialization_mutex, std::try_to_lock);

                    if (lock.owns_lock()) {
                        auto device_id = get_dualsense_audio_device_id();

                        if (m_device.has_value()) {
                            // This memcmp is safe: https://github.com/mackron/miniaudio/issues/866#issuecomment-2207374206
                            if (!device_id.has_value() ||
                                std::memcmp(&m_device->playback.id, &*device_id, 256) != 0) { // NOLINT(*-suspicious-memory-comparison)
                                uninitialize_miniaudio();
                                WARN_PRINT("Audio Haptics device disconnected");
                            }
                        } else if (device_id.has_value()) {
                            try_initialize_miniaudio(*device_id);
                            WARN_PRINT("Audio Haptics device connected");
                        }
                    }
                }

                // Sleep for 1s
                for (int i = 0; i < 100 && !m_stop_device_availability_check; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        });
    }

    AudioEffectControllerHapticsInstance::~AudioEffectControllerHapticsInstance() {
        m_stop_device_availability_check = true;
        m_device_availability_check.join();

        uninitialize_miniaudio();
    }

    void AudioEffectControllerHapticsInstance::_process(const void* p_src_buffer, godot::AudioFrame* p_dst_buffer, int32_t p_frame_count) {
        if (!m_ring_buffer.has_value()) {
            return;
        }

        std::unique_lock lock(m_initialization_mutex, std::try_to_lock);

        if (!lock.owns_lock()) {
            return;
        }

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

    void AudioEffectControllerHapticsInstance::try_initialize_miniaudio(const ma_device_id& device_id) {
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
        device_config.notificationCallback = device_notification_callback;
        device_config.playback.pDeviceID = &device_id;

#if defined(MA_HAS_PULSEAUDIO)
        device_config.pulse.channelMap = MA_PA_CHANNEL_MAP_ALSA;
        device_config.pulse.blockingMainLoop = MA_FALSE;
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
