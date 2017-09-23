filter { "action:vs*" }
		buildoptions {
			--"/wd4100", -- warning C4100: 'xxx' : unreferenced formal parameter
			--"/wd4127", -- warning C4127: conditional expression is constant
			"/wd4244", -- warning C4244: 'argument' : conversion from 'xxx' to 'yyy', possible loss of data
			"/wd4800", -- warning C4800: 'xxx': forcing value to bool 'true' or 'false' (performance warning)
		}

filter { "platforms:Win32" }
	system "windows"
	architecture "x32"
	defines {
		"NOMINMAX",
		"_CRT_SECURE_NO_WARNINGS"
	}

filter "configurations:Debug"
	defines { "DEBUG" }
	symbols "On"
	libdirs {
		"3rdparty/lzham_codec/lib/x86D"
	}

filter "configurations:Release"
	defines { "NDEBUG" }
	optimize "On"
	libdirs {
		"3rdparty/lzham_codec/lib/x86"
	}
