#include "locator.h"

#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#else
#include <unistd.h>
#include <vector>
#endif

using std::filesystem::path;

namespace
{
    path exe_dir()
    {
#if defined(_WIN32)
        char buffer[MAX_PATH]{};
        const DWORD size = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (size == 0 || size >= sizeof(buffer)) return {};
        return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size + 1);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
        return std::filesystem::path(buffer.data()).parent_path();
#else
        std::vector<char> buffer(4096);
        const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (size <= 0) return {};
        buffer[static_cast<size_t>(size)] = '\0';
        return std::filesystem::path(buffer.data()).parent_path();
#endif
    }
}

static path get_env_path(const char *name)
{
    const char *v = std::getenv(name);
    if (!v || !*v) return {};
    path p = v;
    if (std::filesystem::exists(p)) return std::filesystem::canonical(p);
    return {};
}

static path find_upwards_containing(path start, const std::string &subdir, int maxDepth = 6)
{
    path cur = std::filesystem::weakly_canonical(start);
    for (int i = 0; i <= maxDepth; i++)
    {
        path candidate = cur / subdir;
        if (std::filesystem::exists(candidate)) return cur;
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return {};
}

AssetPaths AssetPaths::detect(const path &startDir)
{
    AssetPaths out{};

    if (auto shaders = get_env_path("VKG_SHADER_ROOT"); !shaders.empty())
    {
        out.shaders = shaders;
    }

    if (auto root = get_env_path("VKG_ASSET_ROOT"); !root.empty())
    {
        out.root = root;
        if (std::filesystem::exists(root / "assets")) out.assets = root / "assets";
        if (out.shaders.empty() && std::filesystem::exists(root / "bin" / "shaders")) out.shaders = root / "bin" / "shaders";
        if (out.shaders.empty() && std::filesystem::exists(root / "shaders")) out.shaders = root / "shaders";
        return out;
    }

    if (auto aroot = find_upwards_containing(startDir, "assets"); !aroot.empty())
    {
        out.assets = aroot / "assets";
        out.root = aroot;
    }
    if (auto sroot = find_upwards_containing(startDir, "shaders"); !sroot.empty())
    {
        if (std::filesystem::exists(sroot / "bin" / "shaders")) out.shaders = sroot / "bin" / "shaders";
        else out.shaders = sroot / "shaders";
        if (out.root.empty()) out.root = sroot;
    }

    if (out.assets.empty())
    {
        path p1 = startDir / "assets";
        path p2 = startDir / ".." / "assets";
        if (std::filesystem::exists(p1)) out.assets = p1;
        else if (std::filesystem::exists(p2)) out.assets = std::filesystem::weakly_canonical(p2);
    }
    if (out.shaders.empty())
    {
        path p1 = startDir / "shaders";
        path p2 = startDir / ".." / "shaders";
        path p3 = startDir / "bin" / "shaders";
        if (std::filesystem::exists(p1)) out.shaders = p1;
        else if (std::filesystem::exists(p2)) out.shaders = std::filesystem::weakly_canonical(p2);
        else if (std::filesystem::exists(p3)) out.shaders = p3;
    }

    return out;
}

void AssetLocator::init()
{
    _paths = AssetPaths::detect();
    if (!_paths.valid())
    {
        const path dir = exe_dir();
        if (!dir.empty())
        {
            _paths = AssetPaths::detect(dir);
        }
    }
}

bool AssetLocator::fileExists(const path &p)
{
    std::error_code ec;
    return !p.empty() && std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec);
}

std::string AssetLocator::resolveIn(const path &base, std::string_view name)
{
    if (name.empty()) return {};
    path in = base / std::string(name);
    if (fileExists(in)) return in.string();
    return {};
}

std::string AssetLocator::shaderPath(std::string_view name) const
{
    if (name.empty()) return {};
    path np = std::string(name);

    if (np.is_absolute() && fileExists(np)) return np.string();
    if (fileExists(np)) return np.string();

    if (auto shaders = get_env_path("VKG_SHADER_ROOT"); !shaders.empty())
    {
        if (auto r = resolveIn(shaders, name); !r.empty()) return r;
    }

    if (const path dir = exe_dir(); !dir.empty())
    {
        if (auto r = resolveIn(dir / "shaders", name); !r.empty()) return r;
    }

    if (!_paths.shaders.empty())
    {
        if (auto r = resolveIn(_paths.shaders, name); !r.empty()) return r;
    }

    if (auto r = resolveIn(std::filesystem::current_path() / "shaders", name); !r.empty()) return r;
    if (auto r = resolveIn(std::filesystem::current_path() / ".." / "shaders", name); !r.empty()) return r;

    return np.string();
}

std::string AssetLocator::assetPath(std::string_view name) const
{
    if (name.empty()) return {};
    path np = std::string(name);
    if (np.is_absolute() && fileExists(np)) return np.string();
    if (fileExists(np)) return np.string();

    if (!_paths.assets.empty())
    {
        if (auto r = resolveIn(_paths.assets, name); !r.empty()) return r;
    }

    if (auto r = resolveIn(std::filesystem::current_path() / "assets", name); !r.empty()) return r;
    if (auto r = resolveIn(std::filesystem::current_path() / ".." / "assets", name); !r.empty()) return r;

    return np.string();
}
