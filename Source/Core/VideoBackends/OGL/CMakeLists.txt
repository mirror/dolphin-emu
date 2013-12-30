set(SRCS	Src/GLExtensions/GLExtensions.cpp
		Src/FramebufferManager.cpp
		Src/GLUtil.cpp
		Src/main.cpp
		Src/NativeVertexFormat.cpp
		Src/PerfQuery.cpp
		Src/PostProcessing.cpp
		Src/ProgramShaderCache.cpp
		Src/RasterFont.cpp
		Src/Render.cpp
		Src/SamplerCache.cpp
		Src/StreamBuffer.cpp
		Src/TextureCache.cpp
		Src/TextureConverter.cpp
		Src/VertexManager.cpp)

set(LIBS	videocommon
			SOIL
			common
			${X11_LIBRARIES})
if(USE_EGL)
	set(LIBS ${LIBS}
		EGL)
endif()

if(USE_GLES3)
	set(LIBS ${LIBS}
		GLESv2)
else()
	set(LIBS ${LIBS}
		${OPENGL_LIBRARIES})
endif()

if(wxWidgets_FOUND)
	set(LIBS	${LIBS} ${wxWidgets_LIBRARIES})
endif(wxWidgets_FOUND)

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD" OR
	${CMAKE_SYSTEM_NAME} MATCHES "NetBSD")
	set(LIBS	${LIBS} usbhid)
endif()

add_dolphin_library(videoogl "${SRCS}" "${LIBS}")
