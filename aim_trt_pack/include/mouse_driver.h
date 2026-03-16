#pragma once
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

class MouseController {
public:
    MouseController();
    ~MouseController();

    bool Initialize();
    void MoveTo(unsigned short x, unsigned short y);
    void MoveRelative(int dx, int dy);
    bool SupportsRelativeMove() const;

private:
    HINSTANCE hinstLib;
    typedef void (*MoveToFunctionType)(unsigned short, unsigned short);
    typedef void (*MoveRFunctionType)(int, int);
    typedef int (*OpenDeviceFunctionType)();
    MoveToFunctionType MoveToFunction;
    MoveRFunctionType MoveRFunction;

    static std::string dirname_of(const std::string& path);
};

inline std::string MouseController::dirname_of(const std::string& path)
{
    const size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

inline MouseController::MouseController() : hinstLib(NULL), MoveToFunction(NULL), MoveRFunction(NULL) {}

inline MouseController::~MouseController()
{
    if (hinstLib) {
        FreeLibrary(hinstLib);
    }
}

inline bool MouseController::Initialize()
{
    std::vector<std::string> dll_candidates;
    dll_candidates.push_back("ddll64.dll");

    char exe_path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
        const std::string exe_dir = dirname_of(std::string(exe_path));
        dll_candidates.push_back(exe_dir + "\\ddll64.dll");
        dll_candidates.push_back(exe_dir + "\\..\\..\\runtime\\ddll64.dll");
        dll_candidates.push_back(exe_dir + "\\..\\runtime\\ddll64.dll");
    }

    for (const auto& dll_path : dll_candidates) {
        hinstLib = LoadLibraryA(dll_path.c_str());
        if (hinstLib != NULL) {
            std::cout << "Loaded mouse dll: " << dll_path << std::endl;
            break;
        }
    }

    if (hinstLib == NULL) {
        std::cout << "Failed to load ddll64.dll" << std::endl;
        return false;
    }

    OpenDeviceFunctionType OpenDevice = (OpenDeviceFunctionType)GetProcAddress(hinstLib, "OpenDevice");
    if (OpenDevice == NULL || OpenDevice() == 0) {
        std::cout << "Mouse device not ready." << std::endl;
        return false;
    }

    MoveToFunction = (MoveToFunctionType)GetProcAddress(hinstLib, "MoveTo");
    if (MoveToFunction == NULL) {
        std::cout << "Failed to get MoveTo function" << std::endl;
        return false;
    }

    MoveRFunction = (MoveRFunctionType)GetProcAddress(hinstLib, "MoveR");
    if (MoveRFunction != NULL) {
        std::cout << "MoveR available (relative mode supported)." << std::endl;
    }
    else {
        std::cout << "MoveR not found, only MoveTo available." << std::endl;
    }

    return true;
}

inline void MouseController::MoveTo(unsigned short x, unsigned short y)
{
    if (MoveToFunction) {
        MoveToFunction(x, y);
    }
}

inline void MouseController::MoveRelative(int dx, int dy)
{
    if (MoveRFunction) {
        MoveRFunction(dx, dy);
    }
}

inline bool MouseController::SupportsRelativeMove() const
{
    return MoveRFunction != NULL;
}
