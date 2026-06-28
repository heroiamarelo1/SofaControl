#include "DeviceHider.h"

#include "HidHideIoctl.h"
#include "HidHidePaths.h"

#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <XInput.h>

#include <filesystem>
#include <map>
#include <set>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "xinput.lib")

namespace {

using StringList = std::vector<std::wstring>;

std::filesystem::path AppDataDir() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }

    std::filesystem::path dir = std::filesystem::path(buffer) / L"SofaControl";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path ConfigPath() {
    const std::filesystem::path dir = AppDataDir();
    if (dir.empty()) {
        return {};
    }
    return dir / L"config.ini";
}

bool RememberControllerPathsEnabled() {
    const std::filesystem::path path = ConfigPath();
    if (path.empty()) {
        return true;
    }
    return GetPrivateProfileIntW(L"SofaControl", L"RememberControllerPaths", 1, path.c_str()) != 0;
}

bool PrimaryXInputControllerConnected() {
    XINPUT_STATE state{};
    return XInputGetState(0, &state) == ERROR_SUCCESS;
}

StringList LoadRememberedControllerPaths() {
    const std::filesystem::path path = ConfigPath();
    if (path.empty() || !RememberControllerPathsEnabled()) {
        return {};
    }

    const int count = GetPrivateProfileIntW(L"RememberedControllerPaths", L"Count", 0, path.c_str());
    StringList paths;
    for (int i = 0; i < count && i < 128; ++i) {
        wchar_t key[32]{};
        swprintf_s(key, L"Path%d", i);

        wchar_t value[1024]{};
        GetPrivateProfileStringW(L"RememberedControllerPaths", key, L"", value, 1024, path.c_str());
        if (value[0] != L'\0') {
            paths.emplace_back(value);
        }
    }
    return paths;
}

void SaveRememberedControllerPaths(const StringList& paths) {
    const std::filesystem::path path = ConfigPath();
    if (path.empty() || !RememberControllerPathsEnabled() || paths.empty()) {
        return;
    }

    std::set<std::wstring> unique(paths.begin(), paths.end());
    WritePrivateProfileStringW(L"RememberedControllerPaths", nullptr, nullptr, path.c_str());

    wchar_t count[16]{};
    swprintf_s(count, L"%zu", unique.size());
    WritePrivateProfileStringW(L"RememberedControllerPaths", L"Count", count, path.c_str());

    int index = 0;
    for (const auto& controllerPath : unique) {
        wchar_t key[32]{};
        swprintf_s(key, L"Path%d", index++);
        WritePrivateProfileStringW(L"RememberedControllerPaths", key, controllerPath.c_str(), path.c_str());
    }
}

StringList MergeStringLists(const StringList& first, const StringList& second) {
    std::set<std::wstring> merged(first.begin(), first.end());
    merged.insert(second.begin(), second.end());
    return StringList(merged.begin(), merged.end());
}

StringList ParseMultiString(const wchar_t* data, size_t charCount) {
    StringList result;
    if (!data || charCount == 0) {
        return result;
    }

    const wchar_t* end = data + charCount;
    for (const wchar_t* cursor = data; cursor < end && *cursor != L'\0';) {
        const std::wstring entry(cursor);
        if (!entry.empty()) {
            result.push_back(entry);
        }
        cursor += entry.size() + 1;
    }
    return result;
}

std::vector<wchar_t> BuildMultiString(const StringList& strings) {
    std::vector<wchar_t> buffer;
    for (const auto& entry : strings) {
        buffer.insert(buffer.end(), entry.begin(), entry.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    return buffer;
}

std::wstring ExeFullImageNameLegacy(const std::filesystem::path& fullPath) {
    if (!fullPath.is_absolute()) {
        return {};
    }

    const std::wstring native = fullPath.wstring();
    if (native.size() < 3 || native[1] != L':' || native[2] != L'\\') {
        return {};
    }

    const wchar_t driveLetter = native[0];
    wchar_t driveRoot[] = {driveLetter, L':', L'\\', L'\0'};

    wchar_t targetPath[512]{};
    if (QueryDosDeviceW(driveRoot, targetPath, static_cast<DWORD>(std::size(targetPath))) == 0) {
        return {};
    }

    return std::wstring(targetPath) + L"\\" + native.substr(3);
}

bool IsDevicePresent(const std::wstring& instanceId) {
    DEVINST devInst{};
    return CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) ==
           CR_SUCCESS;
}

bool AreAnyPathsPresent(const StringList& paths) {
    for (const auto& path : paths) {
        if (IsDevicePresent(path)) {
            return true;
        }
    }
    return false;
}

bool IsGamingHidUsage(USHORT usagePage, USHORT usage) {
    if (usagePage == 0x05) {
        return true;
    }
    return usagePage == 0x01 && (usage == 0x04 || usage == 0x05);
}

bool IsMicrosoftControllerInstanceId(const std::wstring& id) {
    return id.find(L"VID_045E") != std::wstring::npos;
}

bool IsMicrosoftUsbInstanceId(const std::wstring& id) {
    return id.rfind(L"USB\\", 0) == 0 && IsMicrosoftControllerInstanceId(id);
}

void AddParentInstancePaths(std::set<std::wstring>& paths, const std::wstring& instanceId) {
    DEVINST devInst{};
    if (CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
        CR_SUCCESS) {
        return;
    }

    for (int depth = 0; depth < 4; ++depth) {
        DEVINST parent{};
        if (CM_Get_Parent(&parent, devInst, 0) != CR_SUCCESS) {
            break;
        }
        devInst = parent;

        wchar_t buffer[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(devInst, buffer, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
            break;
        }

        const std::wstring parentId(buffer);
        if (parentId.empty() || parentId.rfind(L"ROOT\\", 0) == 0) {
            break;
        }
        paths.insert(parentId);
    }
}

std::wstring GuidToKey(const GUID& guid) {
    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
        guid.Data1,
        guid.Data2,
        guid.Data3,
        guid.Data4[0],
        guid.Data4[1],
        guid.Data4[2],
        guid.Data4[3],
        guid.Data4[4],
        guid.Data4[5],
        guid.Data4[6],
        guid.Data4[7]);
    return buffer;
}

std::wstring ContainerKeyForDevice(const std::wstring& instanceId) {
    DEVINST devInst{};
    if (CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
        CR_SUCCESS) {
        return instanceId;
    }

    DEVPROPTYPE propertyType{};
    GUID containerId{};
    ULONG size = sizeof(containerId);
    if (CM_Get_DevNode_PropertyW(
            devInst,
            &DEVPKEY_Device_ContainerId,
            &propertyType,
            reinterpret_cast<PBYTE>(&containerId),
            &size,
            0) == CR_SUCCESS &&
        propertyType == DEVPROP_TYPE_GUID) {
        return GuidToKey(containerId);
    }

    return instanceId;
}

void AddControllerGroupCandidate(
    std::map<std::wstring, std::set<std::wstring>>& groups,
    const std::wstring& instanceId) {
    auto& group = groups[ContainerKeyForDevice(instanceId)];
    group.insert(instanceId);
    AddParentInstancePaths(group, instanceId);
}

StringList EnumeratePresentDeviceInstanceIds() {
    ULONG listLength = 0;
    if (CM_Get_Device_ID_List_SizeW(&listLength, nullptr, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS ||
        listLength == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(listLength);
    if (CM_Get_Device_ID_ListW(nullptr, buffer.data(), listLength, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS) {
        return {};
    }

    StringList result;
    for (const wchar_t* cursor = buffer.data(); *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
        result.emplace_back(cursor);
    }
    return result;
}

void CollectHidGamingPaths(std::map<std::wstring, std::set<std::wstring>>& groups) {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfo =
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        return;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);

    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &hidGuid, index, &interfaceData); ++index) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);

        std::vector<BYTE> detailBuffer(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData{};
        devInfoData.cbSize = sizeof(devInfoData);
        if (!SetupDiGetDeviceInterfaceDetailW(
                deviceInfo, &interfaceData, detail, requiredSize, nullptr, &devInfoData)) {
            continue;
        }

        wchar_t instanceId[MAX_DEVICE_ID_LEN]{};
        if (SetupDiGetDeviceInstanceIdW(deviceInfo, &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr) == FALSE) {
            continue;
        }

        HANDLE deviceHandle = CreateFileW(
            detail->DevicePath,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            continue;
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(deviceHandle, &preparsed)) {
            CloseHandle(deviceHandle);
            continue;
        }

        HIDP_CAPS caps{};
        const bool gaming =
            (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) && IsGamingHidUsage(caps.UsagePage, caps.Usage);

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (gaming && HidD_GetAttributes(deviceHandle, &attributes) && attributes.VendorID == 0x045E) {
            AddControllerGroupCandidate(groups, instanceId);
        }

        HidD_FreePreparsedData(preparsed);
        CloseHandle(deviceHandle);
    }

    SetupDiDestroyDeviceInfoList(deviceInfo);
}

std::set<std::wstring> CollectControllerInstancePaths() {
    std::map<std::wstring, std::set<std::wstring>> groups;
    const StringList presentIds = EnumeratePresentDeviceInstanceIds();

    for (const auto& id : presentIds) {
        if (!IsDevicePresent(id)) {
            continue;
        }

        const bool isMicrosoft = IsMicrosoftControllerInstanceId(id);
        const bool isXusb = id.rfind(L"XUSB\\", 0) == 0;
        const bool isUsb = IsMicrosoftUsbInstanceId(id);
        // XInput-class interface marker; covers the node the Windows shell reads
        // for gamepad navigation even when the HID collection is already hidden.
        const bool isXInputIface = id.find(L"IG_") != std::wstring::npos;

        if (isMicrosoft || isXusb || isUsb || isXInputIface) {
            AddControllerGroupCandidate(groups, id);
        }
    }

    // Include the gaming HID collection for the same physical controller group;
    // this is the device the Windows shell can still read for menu navigation.
    CollectHidGamingPaths(groups);

    if (groups.empty()) {
        return {};
    }

    return groups.begin()->second;
}

class HidHideClient {
public:
    HidHideClient() = default;

    ~HidHideClient() { Close(); }

    HidHideClient(const HidHideClient&) = delete;
    HidHideClient& operator=(const HidHideClient&) = delete;

    bool Open() {
        Close();
        handle_ = CreateFileW(
            kHidHideControlDevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            lastError_ = GetLastError();
            return false;
        }

        lastError_ = ERROR_SUCCESS;
        return true;
    }

    void Close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    DWORD LastError() const { return lastError_; }

    static bool IsDriverPresent() {
        HidHideClient probe;
        if (probe.Open()) {
            return true;
        }
        return probe.LastError() == ERROR_ACCESS_DENIED;
    }

    bool GetActive(bool& active) {
        BOOLEAN value = FALSE;
        DWORD bytesReturned = 0;
        if (DeviceIoControl(
                handle_, IOCTL_HIDHIDE_GET_ACTIVE, nullptr, 0, &value, sizeof(value), &bytesReturned, nullptr)) {
            active = (value != FALSE);
            return true;
        }
        lastError_ = GetLastError();
        return false;
    }

    bool SetActive(bool active) {
        BOOLEAN value = active ? TRUE : FALSE;
        DWORD bytesReturned = 0;
        if (DeviceIoControl(
                handle_, IOCTL_HIDHIDE_SET_ACTIVE, &value, sizeof(value), nullptr, 0, &bytesReturned, nullptr)) {
            return true;
        }
        lastError_ = GetLastError();
        return false;
    }

    bool EnsureCurrentProcessWhitelisted() {
        const std::filesystem::path exePath = CurrentExecutablePath();

        StringList whitelist;
        if (!GetMultiString(IOCTL_HIDHIDE_GET_WHITELIST, whitelist)) {
            return false;
        }

        if (IsExecutableWhitelisted(whitelist, exePath)) {
            return true;
        }

        std::wstring fullImageName = FileNameToFullImageName(exePath);
        if (fullImageName.empty()) {
            fullImageName = ExeFullImageNameLegacy(exePath);
        }

        if (!fullImageName.empty()) {
            whitelist.push_back(fullImageName);
            if (SetMultiString(IOCTL_HIDHIDE_SET_WHITELIST, whitelist)) {
                return true;
            }
        }

        // Registration failed — accept manual whitelist entries that already cover this exe.
        StringList refreshed;
        if (!GetMultiString(IOCTL_HIDHIDE_GET_WHITELIST, refreshed)) {
            return false;
        }

        return IsExecutableWhitelisted(refreshed, exePath);
    }

    bool MergeBlacklist(const StringList& addPaths, const StringList& removePaths) {
        StringList blacklist;
        if (!GetMultiString(IOCTL_HIDHIDE_GET_BLACKLIST, blacklist)) {
            return false;
        }

        std::set<std::wstring> merged(blacklist.begin(), blacklist.end());
        for (const auto& path : removePaths) {
            merged.erase(path);
        }
        for (const auto& path : addPaths) {
            merged.insert(path);
        }

        return SetMultiString(IOCTL_HIDHIDE_SET_BLACKLIST, StringList(merged.begin(), merged.end()));
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD lastError_ = 0;

    bool GetMultiString(DWORD ioctl, StringList& out) {
        DWORD needed = 0;
        DeviceIoControl(handle_, ioctl, nullptr, 0, nullptr, 0, &needed, nullptr);

        if (needed == 0) {
            out.clear();
            return true;
        }

        std::vector<wchar_t> buffer(needed / sizeof(wchar_t));
        if (!DeviceIoControl(handle_, ioctl, nullptr, 0, buffer.data(), needed, &needed, nullptr)) {
            lastError_ = GetLastError();
            return false;
        }

        out = ParseMultiString(buffer.data(), buffer.size());
        return true;
    }

    bool SetMultiString(DWORD ioctl, const StringList& strings) {
        const auto buffer = BuildMultiString(strings);
        const DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        DWORD needed = 0;
        if (DeviceIoControl(
                handle_,
                ioctl,
                const_cast<wchar_t*>(buffer.data()),
                bytes,
                nullptr,
                0,
                &needed,
                nullptr)) {
            return true;
        }
        lastError_ = GetLastError();
        return false;
    }
};

std::set<std::wstring> MergeTrackedAndDiscoveredPaths(
    const std::vector<std::wstring>& trackedPaths,
    const std::vector<std::wstring>& discoveredPaths) {
    std::set<std::wstring> merged(trackedPaths.begin(), trackedPaths.end());
    for (const auto& path : discoveredPaths) {
        merged.insert(path);
    }
    return merged;
}

void SyncHidingActiveFromDriver(HidHideClient& client, bool& hidingActive) {
    bool driverActive = false;
    if (client.GetActive(driverActive)) {
        hidingActive = driverActive;
    }
}

bool EnsureBlacklist(HidHideClient& client, const std::set<std::wstring>& paths, std::wstring& statusMessage) {
    if (paths.empty()) {
        statusMessage = L"HidHide: no controller device paths found.";
        return false;
    }

    if (!client.MergeBlacklist(StringList(paths.begin(), paths.end()), {})) {
        statusMessage = L"HidHide: failed to update device blacklist.";
        return false;
    }

    return true;
}

bool EnableHiding(
    HidHideClient& client,
    const std::vector<std::wstring>& paths,
    const std::vector<std::wstring>& removePaths,
    std::vector<std::wstring>& hiddenPaths,
    bool& hidingActive,
    std::wstring& statusMessage) {
    if (paths.empty()) {
        statusMessage = L"HidHide: no controller device paths found.";
        return false;
    }

    const std::filesystem::path exePath = CurrentExecutablePath();
    if (!client.EnsureCurrentProcessWhitelisted()) {
        statusMessage = L"HidHide: SofaControl.exe is not whitelisted (add it in HidHide or retry). Path: ";
        statusMessage += exePath.wstring();
        return false;
    }

    const auto pathsToRemove = MergeTrackedAndDiscoveredPaths(hiddenPaths, removePaths);
    if (!client.MergeBlacklist(paths, StringList(pathsToRemove.begin(), pathsToRemove.end()))) {
        statusMessage = L"HidHide: failed to update device blacklist.";
        return false;
    }

    if (!client.SetActive(true)) {
        statusMessage = L"HidHide: failed to enable device hiding.";
        SyncHidingActiveFromDriver(client, hidingActive);
        return false;
    }

    hiddenPaths.assign(paths.begin(), paths.end());
    SyncHidingActiveFromDriver(client, hidingActive);
    statusMessage = L"HidHide: hiding ON — controller hidden (mouse mode).";
    return hidingActive;
}

bool DisableHiding(
    HidHideClient& client,
    bool& hidingActive,
    std::wstring& statusMessage) {
    if (!client.SetActive(false)) {
        statusMessage = L"HidHide: failed to reveal controller.";
        SyncHidingActiveFromDriver(client, hidingActive);
        return false;
    }
    SyncHidingActiveFromDriver(client, hidingActive);
    statusMessage = L"HidHide: hiding OFF — controller visible (XInput mode).";
    return !hidingActive;
}

}  // namespace

static bool OpenClient(HidHideClient& client, std::wstring& statusMessage) {
    if (!client.Open()) {
        switch (client.LastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            statusMessage = L"HidHide driver not installed. Get it at github.com/nefarius/HidHide/releases";
            break;
        case ERROR_ACCESS_DENIED:
            statusMessage = L"HidHide: driver busy (close HidHide Configuration or any app using the driver).";
            break;
        default:
            statusMessage = L"HidHide: could not open driver control device.";
            break;
        }
        return false;
    }
    return true;
}

bool DeviceHider::IsDriverAvailable() const {
    return HidHideClient::IsDriverPresent();
}

std::vector<std::wstring> DeviceHider::FindConnectedControllerPaths() {
    pathsToForget_.clear();

    const StringList remembered = LoadRememberedControllerPaths();
    if (!PrimaryXInputControllerConnected()) {
        return remembered;
    }

    if (RememberControllerPathsEnabled() && !remembered.empty() && AreAnyPathsPresent(remembered)) {
        return remembered;
    }

    const auto discoveredSet = CollectControllerInstancePaths();
    StringList discovered(discoveredSet.begin(), discoveredSet.end());
    if (RememberControllerPathsEnabled() && !discovered.empty()) {
        SaveRememberedControllerPaths(discovered);
    }

    if (!discovered.empty()) {
        pathsToForget_ = remembered;
        hiddenPaths_.clear();
        return discovered;
    }

    return remembered;
}

bool DeviceHider::ApplyHide() {
    if (!IsDriverAvailable()) {
        statusMessage_ = L"HidHide driver not installed. Get it at github.com/nefarius/HidHide/releases";
        return false;
    }

    HidHideClient client;
    if (!OpenClient(client, statusMessage_)) {
        return false;
    }

    return EnableHiding(client, FindConnectedControllerPaths(), pathsToForget_, hiddenPaths_, hidingActive_, statusMessage_);
}

bool DeviceHider::ApplyReveal() {
    if (!IsDriverAvailable()) {
        statusMessage_ = L"HidHide driver not installed. Get it at github.com/nefarius/HidHide/releases";
        return false;
    }

    HidHideClient client;
    if (!OpenClient(client, statusMessage_)) {
        return false;
    }

    return DisableHiding(client, hidingActive_, statusMessage_);
}

bool DeviceHider::Hide() {
    return ApplyHide();
}

bool DeviceHider::Reveal() {
    return ApplyReveal();
}

bool DeviceHider::RefreshHidden() {
    return ApplyHide();
}

void DeviceHider::RestoreOnExit() {
    ApplyReveal();
}
