set(QT_USE_QTNETWORK FALSE)
set(QT_USE_QTOPENGL FALSE)
set(QT_USE_QTSQL FALSE)
set(QT_USE_QTXML FALSE)
set(QT_USE_QTSVG FALSE)
set(QT_USE_QTTEST FALSE)
set(QT_USE_QTDBUS FALSE)
set(QT_USE_QTSCRIPT FALSE)
set(QT_USE_QTWEBKIT FALSE)
set(QT_USE_QTXMLPATTERNS FALSE)
set(QT_USE_PHONON FALSE)

set(SRCS		Src/Config/ConfigAudio.cpp
			Src/Config/ConfigGeneral.cpp
			Src/Config/ConfigGfx.cpp
			Src/Config/ConfigMain.cpp
			Src/Config/ConfigPad.cpp
			Src/Config/ConfigWiimote.cpp
			Src/Util/Util.cpp
			Src/Util/Resources.cpp
			Src/GameList.cpp
			Src/ISOFile.cpp
			Src/IssueReporter.cpp
			Src/LogWindow.cpp
			Src/main.cpp
			Src/MainWindow.cpp
			Src/MainWindowEvents.cpp
			Src/RenderWindow.cpp)

if(USE_EGL)
	set(SRCS ${SRCS} ../DolphinWX/Src/GLInterface/Platform.cpp
		../DolphinWX/Src/GLInterface/EGL.cpp)
	if(USE_WAYLAND)
		set(SRCS ${SRCS} ../DolphinWX/Src/GLInterface/Wayland_Util.cpp)
	endif()
	if(USE_X11)
		set(SRCS ${SRCS} ../DolphinWX/Src/GLInterface/X11_Util.cpp)
	endif()
else()
	if(WIN32)
		set(SRCS ${SRCS} ../DolphinWX/Src/GLInterface/WGL.cpp)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
		set(SRCS ${SRCS} ../DolphinWX/Src/GLInterface/AGL.cpp)
	else()
		set(SRCS ${SRCS} ../DolphinWX/Src/GLInterface/GLX.cpp
			../DolphinWX/Src/GLInterface/X11_Util.cpp)

	endif()
endif()

set(LIBS		core
			${LZO}
			discio
			bdisasm
			inputcommon
			common
			audiocommon
			z
			sfml-network
			rt
			${OPENGL_LIBRARIES}
			${X11_LIBRARIES}
			${XINPUT2_LIBRARIES}
			${XRANDR_LIBRARIES})

if(SDL2_FOUND)
	# Using shared SDL2
	set(LIBS	${LIBS} ${SDL2_LIBRARY})
else(SDL2_FOUND)
	if(SDL_FOUND)
		# Using shared SDL
		set(LIBS	${LIBS} ${SDL_LIBRARY})
	else(SDL_FOUND)
		# Using static SDL from Externals
		set(LIBS	${LIBS} SDL)
	endif()
endif()

set(EXEGUI dolphin-emu-qt4)

qt_wrap_cpp(${EXEGUI} MOC_SRCS	Src/Config/ConfigAudio.h
				Src/Config/ConfigGeneral.h
				Src/Config/ConfigGfx.h
				Src/Config/ConfigMain.h
				Src/Config/ConfigPad.h
				Src/Config/ConfigWiimote.h
				Src/Util/Util.h
				Src/IssueReporter.h
				Src/GameList.h
				Src/LogWindow.h
				Src/MainWindow.h
				Src/RenderWindow.h)
add_executable(${EXEGUI} ${SRCS} ${MOC_SRCS})
target_link_libraries(${EXEGUI} ${LIBS} ${QT_LIBRARIES})
install(TARGETS ${EXEGUI} RUNTIME DESTINATION ${bindir})
