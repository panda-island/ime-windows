#include <windows.h>

#include <cstdio>
#include <cstddef>
#include <vector>

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::fputs("Expected the absolute path to the TSF DLL.\n", stderr);
        return 1;
    }

    // Resolve dependencies without the build/installation directory, PATH, or
    // the current directory. AppContainer TSF hosts cannot rely on those paths.
    // This deliberately tests loading only; it never registers the text service.
    const HMODULE module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        const DWORD error = GetLastError();
        std::fprintf(stderr, "TSF DLL failed to load with system dependencies only: error %lu\n", error);
        return 1;
    }
    FreeLibrary(module);
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(argv[1], &ignored);
    std::vector<std::byte> bytes(size);
    if (size == 0 || !GetFileVersionInfoW(argv[1], 0, size, bytes.data())) {
        std::fputs("TSF DLL is missing Windows file-version metadata.\n", stderr);
        return 1;
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT info_size = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &info_size) ||
        !info || info_size < sizeof(VS_FIXEDFILEINFO) ||
        HIWORD(info->dwFileVersionMS) != LLAVON_EXPECTED_VERSION_MAJOR ||
        LOWORD(info->dwFileVersionMS) != LLAVON_EXPECTED_VERSION_MINOR ||
        HIWORD(info->dwFileVersionLS) != LLAVON_EXPECTED_VERSION_PATCH) {
        std::fputs("TSF DLL file version does not match the product version.\n", stderr);
        return 1;
    }
    return 0;
}
