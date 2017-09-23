-- The goal of this project is to draw the entire Vice City in only 1 draw call.
--
-- As a side effect, this turns out to be my playground for experimenting with
-- AZDO OpenGL, including bindless textures, glMultiDrawElementsIndirect() and
-- it also serves a testing ground for various optimization ideas.

PROJ_DIR = path.getabsolute(".")


newoption {
	trigger = "with-lzham",
	description = "Enable LZHAM compression"
}


workspace "ViceCity_OneDrawCall"
	configurations { "Debug", "Release" }
	platforms { "Win32" }
	location "build/"
	startproject "vicerender"


	includedirs {
		"3rdparty/glm-0.9.6.3",
		"3rdparty/glew-1.13.0/include"
	}


	-- The application for baking Vice City assets
	project "vicebaker"
		kind "ConsoleApp"
		language "C++"
		location "build/%{prj.name}"
		debugdir "data/"

		defines {
			"GLM_FORCE_RADIANS",
		}
		includedirs {
			"3rdparty/rwtools/include/"
		}
		files {
			"source/config.h",
			"source/main_baker.cpp",
			"3rdparty/rwtools/src/*.cpp"
		}

		filter "system:not windows"
			links { "lzhamcomp" }
		filter { "system:windows", "Debug" }
			links { "lzhamcomp_x86D" }
		filter { "system:windows", "Release" }
			links { "lzhamcomp_x86" }


	-- Vice City One-Draw-Call renderer
	project "vicerender"
		kind "ConsoleApp"
		language "C++"
		location "build/%{prj.name}"
		debugdir "data/"

		defines {
			"GLM_FORCE_RADIANS",
			"GLEW_STATIC",
			"_REENTRANT" -- sdl2-config --cflags
		}
		includedirs {
			"/usr/include/SDL2",
		}
		files {
			"source/config.h",
			"source/main_renderer.cpp",
			"source/app_renderer.cpp",
			"source/util_gl.cpp",
			"source/util_file.cpp"
		}

		links {
			"SDL2",
			"SDL2main"
		}

		filter { "system:not windows" }
			buildoptions { os.outputof("sdl2-config --cflags") }
			links {
				"lzhamdecomp",
				"GL",
				"GLEW"
			}
		filter { "system:windows" }
			includedirs {
				"3rdparty/SDL2-2.0.4/include"
			}
			libdirs {
				"3rdparty/glew-1.13.0/lib/Release/Win32",
				"3rdparty/SDL2-2.0.4/lib/x86/"
			}
			links {
				"opengl32",
				"glew32s"
			}
		filter { "system:windows", "Debug" }
			links { "lzhamdecomp_x86D" }
		filter { "system:windows", "Release" }
			links { "lzhamdecomp_x86" }
