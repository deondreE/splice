workspace "TextEditor"
    architecture "x64"
    startproject "Editor"

    configurations { "Debug", "Release" }
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
    filter {}

    project "Editor"
        kind "ConsoleApp"
        language "C++"
        targetdir "bin/%{cfg.buildcfg}"
        objdir "bin-int/%{cfg.buildcfg}"
        location "."

        files {
            "main.cpp",
            "editor.cpp",
            "win_console_utils.cpp",
            "common.h",
            "editor.h",
            "win_console_utils.h",

            "lua_src/**.c", 
            -- Add Lua header files for dependency tracking (still good practice)
            "lua_src/**.h"
        }

        removefiles {
            "lua_src/lua.c",
            "lua_src/luac.c"
        }

        includedirs {
            ".",
            "lua_src"
        }

        links {
            "Shlwapi",
        }

        buildoptions {
            "-std=c++17",
            "-static-libgcc",
            "-static-libstdc++",
        }

        filter "system:windows"
            toolset "msc"

        filter {}