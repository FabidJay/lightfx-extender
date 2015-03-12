#ifndef LFXE_EXPORTS
#define LFXE_EXPORTS
#endif

#include "DeviceLogitech.h"

// Windows includes
#include "../Common/Windows.h"
#include <Windows.h> // Windows.h is needed for some declarations inside LogitechLEDLib.h

// 3rd party includes
#include "LogitechLEDLib.h"

// Project includes
#include "../LightFXExtender.h"
#include "../Managers/LogManager.h"


#define LOG(logLevel, message) if (this->GetManager() != nullptr) { LOG_(this->GetManager()->GetLightFXExtender()->GetLogManager(), logLevel, wstring(L"Device ") + this->GetDeviceName() + L" - " + message) }

using namespace std;
using namespace lightfx::managers;
using namespace lightfx::timelines;

namespace lightfx {
    namespace devices {

        LFXE_API void DeviceLogitech::SetRange(const int outMin, const int outMax, const int inMin, const int inMax) {
            this->rangeOutMin = outMin;
            this->rangeOutMax = outMax;
            this->rangeInMin = inMin;
            this->rangeInMax = inMax;
        }

        LFXE_API bool DeviceLogitech::Initialize() {
            if (!this->IsInitialized()) {
                if (Device::Initialize()) {
                    // Just do an initial pass to set how many LEDs there are available
                    // TODO: Support more Logitech customization (e.g. Logitech G910 single-key colors)
                    this->SetNumberOfLights(1);
                    this->SetLightData(0, LightData());

                    this->Reset();
                    return true;
                }
            }
            this->SetInitialized(false);
            return false;
        }

        LFXE_API bool DeviceLogitech::Enable() {
            if (!this->IsEnabled()) {
                if (Device::Enable()) {
                    if (LogiLedInit()) {
                        this->Reset();
                        return true;
                    } else {
                        LOG(LogLevel::Error, L"Could not enable Logitech, make sure that Logitech Gaming Software is running and that it's at least at version 8.57.145");
                    }
                }
            }
            this->SetEnabled(false);
            return false;
        }

        LFXE_API bool DeviceLogitech::Disable() {
            if (this->IsEnabled()) {
                if (Device::Disable()) {
                    LogiLedShutdown();
                    return true;
                }
            }
            this->SetEnabled(true);
            return false;
        }

        LFXE_API bool DeviceLogitech::PushColorToDevice(const vector<LightColor>& colors) {
            double divider = (this->rangeOutMax - this->rangeOutMin) / ((this->rangeInMax - this->rangeInMin) / 100.0) / 100.0;
            double brightness = colors[0].brightness / 255.0;
            double red = (colors[0].red - this->rangeOutMin) / divider * brightness + this->rangeInMin;
            double green = (colors[0].green - this->rangeOutMin) / divider * brightness + this->rangeInMin;
            double blue = (colors[0].blue - this->rangeOutMin) / divider * brightness + this->rangeInMin;

            LOG(LogLevel::Debug, L"Update color to (" + to_wstring(red) + L"," + to_wstring(green) + L"," + to_wstring(blue) + L")");

            return LogiLedSetLighting((int)red, (int)green, (int)blue);
        }

    }
}