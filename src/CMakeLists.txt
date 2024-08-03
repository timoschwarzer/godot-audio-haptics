# SPDX-License-Identifier: Unlicense

target_sources( ${PROJECT_NAME}
    PRIVATE
        external/miniaudio.h
        external/miniaudio_init.h
        RegisterExtension.cpp
        AudioEffectControllerHaptics.cpp
        AudioEffectControllerHaptics.h
        AudioEffectControllerHapticsInstance.cpp
        AudioEffectControllerHapticsInstance.h
)

target_include_directories( ${PROJECT_NAME}
    PRIVATE
        "src"
)
