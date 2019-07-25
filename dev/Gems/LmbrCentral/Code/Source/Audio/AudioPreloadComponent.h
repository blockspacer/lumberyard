/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
#pragma once

#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/string/string.h>
#include <AzCore/Component/Component.h>
#include <LmbrCentral/Audio/AudioPreloadComponentBus.h>

#include <IAudioSystem.h>

namespace LmbrCentral
{
    /*!
     * AudioPreloadComponent
     *  Allows loading and unloading ATL Preloads (Soundbanks).
     *  A Preload name can be serialized with the component, or it can be manually specified
     *  at runtime for use in scripting.
     */
    class AudioPreloadComponent
        : public AZ::Component
        , public AudioPreloadComponentRequestBus::Handler
        , public Audio::AudioPreloadNotificationBus::MultiHandler
    {
    public:
        enum class LoadType : AZ::u32
        {
            Auto,       // Automatically loads / unloads when the component activates / deactivates
            Manual,     // Loading and unloading is triggered manually
        };

        /*!
         * AZ::Component
         */
        AZ_COMPONENT(AudioPreloadComponent, "{CBBB1234-4DCA-427E-80FF-E2BB0866EEB1}");
        void Activate() override;
        void Deactivate() override;

        AudioPreloadComponent() = default;
        AudioPreloadComponent(AudioPreloadComponent::LoadType loadType, const AZStd::string& preloadName);

        /*!
         * AudioPreloadComponentRequestBus::Handler Required Interface
         */
        void Load() override;
        void Unload() override;
        void LoadPreload(const char* preloadName) override;
        void UnloadPreload(const char* preloadName) override;
        bool IsLoaded(const char* preloadName) override;

        /*!
         * Audio::AudioPreloadNotificationBus::Handler interface
         */
        void OnAudioPreloadCached() override;
        void OnAudioPreloadUncached() override;

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
        {
            provided.push_back(AZ_CRC("AudioPreloadService", 0x20c917d8));
        }

        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
        {
        }

        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent)
        {
            dependent.push_back(AZ_CRC("AudioProxyService", 0x7da4c79c));
        }

        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
        {
            incompatible.push_back(AZ_CRC("AudioPreloadService", 0x20c917d8));
        }

        static void Reflect(AZ::ReflectContext* context);

    private:
        //! Utility functions
        void LoadPreloadById(Audio::TAudioPreloadRequestID preloadId);
        void UnloadPreloadById(Audio::TAudioPreloadRequestID preloadId);

        //! Transient data
        AZStd::unordered_set<Audio::TAudioPreloadRequestID> m_loadedPreloadIds;
        AZStd::mutex m_loadMutex;

        //! Serialized data
        AZStd::string m_defaultPreloadName;
        AudioPreloadComponent::LoadType m_loadType = AudioPreloadComponent::LoadType::Auto;
    };

} // namespace LmbrCentral

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(LmbrCentral::AudioPreloadComponent::LoadType, "{084969E9-65AB-42FD-8EA2-C1DDDCB7B676}");
} // namespace AZ
