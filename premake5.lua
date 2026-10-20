-- worldsea -- a relationship manager for the worlds in novels.
-- Generate: premake5 gmake && make -C build -j8 config=release

-- The build number and name come from git (scripts/version.sh) into a header,
-- rewritten only when it changes, at generation time and as a prebuild step.
local version_header = "build/generated/worldsea_version.h"
os.mkdir("build/generated")
os.execute("./scripts/version.sh header " .. version_header)

local function refresh_version()
    prebuildcommands { '"%{wks.location}/../scripts/version.sh" header "%{wks.location}/generated/worldsea_version.h"' }
end

-- Qt and SQLite through pkg-config: their paths move between distributions,
-- and a wrong guess fails at link time naming nothing useful.
local function pkg(what, mods)
    local out = os.outputof("pkg-config " .. what .. " " .. mods .. " 2>/dev/null") or ""
    -- system headers, so their warnings (Qt trips -Wsfinae-incomplete) stay theirs
    return (out:gsub("%-I/", "-isystem /"))
end
local CORE_PKGS = "Qt6Core Qt6Gui sqlite3"
local APP_PKGS = CORE_PKGS .. " Qt6Widgets"

workspace "worldsea"
    configurations { "Debug", "Release" }
    architecture "x86_64"
    location "build"
    startproject "worldsea"

    language "C++"
    cppdialect "C++23"
    multiprocessorcompile "On"

    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"

    includedirs { "src", "build/generated", "build/moc" }
    -- Qt6 exports its type_info with protected visibility, which a -fPIE object
    -- cannot copy-relocate against ("copy relocation against non-copyable
    -- protected symbol _ZTI7QObject").
    pic "On"

    filter "action:gmake*"
        buildoptions { "-Wall", "-Wextra", "-Wno-unused-parameter" }

    filter "configurations:Debug"
        defines { "WORLDSEA_DEBUG" }
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
        symbols "On"
    filter {}

-- Everything that decides something: the world file, types and sheets, values
-- along the timeline, links, undo, search. No widgets, so the tests link the
-- real code.
project "worldsea-core"
    kind "StaticLib"
    files { "src/core/**.h", "src/core/**.cpp" }
    -- moc output, generated into build/moc and #included by the matching .cpp
    prebuildcommands { '"%{wks.location}/../scripts/moc.sh" "%{wks.location}/moc"' }
    buildoptions { pkg("--cflags", CORE_PKGS) }
    refresh_version()

project "worldsea"
    kind "WindowedApp"
    files { "src/app/**.h", "src/app/**.cpp" }
    links { "worldsea-core" }
    prebuildcommands { '"%{wks.location}/../scripts/moc.sh" "%{wks.location}/moc"' }
    buildoptions { pkg("--cflags", APP_PKGS) }
    linkoptions { pkg("--libs", APP_PKGS) }
    refresh_version()

project "worldsea-tests"
    kind "ConsoleApp"
    files { "tests/**.h", "tests/**.cpp" }
    links { "worldsea-core", "gtest" }
    buildoptions { pkg("--cflags", CORE_PKGS) }
    linkoptions { pkg("--libs", CORE_PKGS) }
    refresh_version()
