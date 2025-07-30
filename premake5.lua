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
        cppdialect "C++20"
        targetdir "bin/%{cfg.buildcfg}"
        objdir "bin-int/%{cfg.buildcfg}"
        location "."

        files {
            "src/**.cpp",
            "src/**.h",

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
            "src",
            "lua_src"
        }

        links {
            "Shlwapi",
        }

        filter "system:windows"
            toolset "msc"
            buildoptions {}
            flags { "MultiProcessorCompile" }
            -- fatalwarnings { "All" }
            defines {"_CRT_SECURE_NO_WARNINGS"}

        filter {}