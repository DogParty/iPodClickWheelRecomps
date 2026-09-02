// The host's clock and battery, one implementation per platform. See device.h for why.
#include "ipod/platform/device.h"

#include <ctime>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <cstdio>
#include <dirent.h>
#include <string>
#endif

namespace ipod::platform {

namespace {

struct Pinned {
    int hour = -1;  // < 0: use the host's clock
    int minute = 0;
    int battery = -1;  // < 0: use the host's charge
};

Pinned& pinned() {
    static Pinned state;
    return state;
}

// The one thread-safe local-time conversion every toolchain has, under two spellings.
void to_local(std::time_t when, std::tm& out) {
#if defined(_WIN32)
    localtime_s(&out, &when);
#else
    localtime_r(&when, &out);
#endif
}

#if defined(__APPLE__)
// IOKit's power-source API. `IOPSCopyPowerSourcesInfo` returns a blob describing every source and
// `IOPSCopyPowerSourcesList` the sources in it; each is a dictionary with a current and a maximum
// capacity. Both are plain C, so this needs no Objective-C — only the two frameworks the shared
// library links on Apple (../../../CMakeLists.txt).
int host_battery_percent() {
    const CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    if (blob == nullptr) {
        return -1;
    }
    const CFArrayRef sources = IOPSCopyPowerSourcesList(blob);
    int answer = -1;
    if (sources != nullptr) {
        for (CFIndex i = 0; i < CFArrayGetCount(sources) && answer < 0; ++i) {
            const CFDictionaryRef source =
                IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, i));
            if (source == nullptr) {
                continue;
            }
            const auto number = [&](CFStringRef key) -> int {
                const auto value =
                    static_cast<CFNumberRef>(CFDictionaryGetValue(source, key));
                int out = 0;
                if (value == nullptr || !CFNumberGetValue(value, kCFNumberIntType, &out)) {
                    return -1;
                }
                return out;
            };
            const int current = number(CFSTR(kIOPSCurrentCapacityKey));
            const int maximum = number(CFSTR(kIOPSMaxCapacityKey));
            if (current >= 0 && maximum > 0) {
                answer = (current * 100 + maximum / 2) / maximum;
            }
        }
        CFRelease(sources);
    }
    CFRelease(blob);
    return answer;
}
#elif defined(_WIN32)
int host_battery_percent() {
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status) == 0 || status.BatteryLifePercent > 100) {
        return -1;  // 255 is Windows' "unknown"
    }
    return status.BatteryLifePercent;
}
#else
// Linux and the BSDs expose it as a file. The first battery that reports a capacity answers.
int host_battery_percent() {
    DIR* directory = opendir("/sys/class/power_supply");
    if (directory == nullptr) {
        return -1;
    }
    int answer = -1;
    while (const dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.rfind("BAT", 0) != 0) {
            continue;
        }
        const std::string path = "/sys/class/power_supply/" + name + "/capacity";
        if (std::FILE* file = std::fopen(path.c_str(), "r")) {
            int percent = 0;
            if (std::fscanf(file, "%d", &percent) == 1 && percent >= 0 && percent <= 100) {
                answer = percent;
            }
            std::fclose(file);
        }
        if (answer >= 0) {
            break;
        }
    }
    closedir(directory);
    return answer;
}
#endif

}  // namespace

LocalTime local_time_now() {
    std::tm host{};
    to_local(std::time(nullptr), host);
    LocalTime out;
    out.second = host.tm_sec;
    out.minute = host.tm_min;
    out.hour = host.tm_hour;
    out.day = host.tm_mday;
    out.month = host.tm_mon + 1;
    out.year = host.tm_year + 1900;
    if (pinned().hour >= 0) {
        out.hour = pinned().hour;
        out.minute = pinned().minute;
        out.second = 0;
    }
    return out;
}

void set_fixed_local_time(int hour, int minute) {
    pinned().hour = hour >= 0 && hour <= 23 ? hour : -1;
    pinned().minute = minute >= 0 && minute <= 59 ? minute : 0;
}

unsigned battery_percent() {
    if (pinned().battery >= 0) {
        return static_cast<unsigned>(pinned().battery);
    }
    const int host = host_battery_percent();
    // No battery is not an error: a desktop is a device that is always on the charger, and that
    // is what a full gauge means. Reporting 0 there would put every game into its low-battery
    // behaviour on a machine that has no such state.
    return host < 0 ? 100u : static_cast<unsigned>(host);
}

void set_fixed_battery_percent(int percent) {
    pinned().battery = percent >= 0 && percent <= 100 ? percent : -1;
}

}  // namespace ipod::platform
