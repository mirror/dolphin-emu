set(CMAKE_FAKELANG_CREATE_STATIC_LIBRARY "touch <TARGET>")
# giant depend hack for Ninja only
# By way of explanation:
#
# With PCH, every .cpp file pulls in every .h file in the project,
# which is no big deal when compiling (it's precompiled), but would
# normally require every .cpp file to be recompiled when any .h
# file changes.
#
# PCH_SLOW_BUT_SAFE does exactly this.  Without PCH_SLOW_BUT_SAFE, only .cpp
# files which would normally depend on that .h file are recompiled.  With
# Makefiles, cmake uses a builtin makedepend equivalent anyway, so this is
# easy to accomplish.  With ninja, cmake always uses the compiler for
# depfiles, so we need to do it manually.
#
# If compilers were not completely braindead, none of this would
# be necessary.

if(("${CMAKE_GENERATOR}" STREQUAL "Ninja") AND (NOT PCH_SLOW_BUT_SAFE))
	# the proper way would be -MMD -MT etc., but makedepend is way faster
	set(CMAKE_CXXMD_COMPILE_OBJECT "${CMAKE_CXX_COMPILE_OBJECT} && (makedepend -- <DEFINES> <FLAGS> -- <SOURCE> -f- 2>/dev/null | sed 's!^.*:!<OBJECT>:!' | egrep -v ': \\/|pch\\.h|^#' > \"$DEP_FILE\")")
	set(CMAKE_CXXMD_ARCHIVE_CREATE "${CMAKE_CXX_ARCHIVE_CREATE}")
	set(CMAKE_CXXMD_ARCHIVE_FINISH "${CMAKE_CXX_ARCHIVE_FINISH}")
	set(CMAKE_DEPFILE_FLAGS_CXXMD "")
	set(CMAKE_INCLUDE_FLAG_CXXMD ${CMAKE_INCLUDE_FLAG_CXX})
	set(CMAKE_INCLUDE_FLAG_SEP_CXXMD ${CMAKE_INCLUDE_FLAG_SEP_CXX})
	set(CMAKE_CXXMD_FLAGS "${CMAKE_CXX_FLAGS}")
	set(CMAKE_CXXMD_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")
	set(CMAKE_CXXMD_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
	set(CMAKE_CXXMD_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
	set(CMAKE_CXXMD_OUTPUT_EXTENSION "${CMAKE_CXX_OUTPUT_EXTENSION}")
endif()

macro(add_dolphin_library lib srcs libs)
	add_library(${lib} STATIC ${srcs})
	target_link_libraries(${lib} ${libs})
	if(ENABLE_PCH)
		file(GLOB_RECURSE all_header_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.h)
		set(header_deps)

		# Parse out the list of all includes - actually not too slow, and only
		# needs to be done once.
		set(everything "")
		set(extra_include_dirs)
		foreach(src ${srcs})
			string(REGEX REPLACE "\\/[^\\/]*$" "" basename "${src}")
			list(APPEND extra_include_dirs "${CMAKE_CURRENT_SOURCE_DIR}/${basename}")
			file(READ ${src} contents LIMIT 15000)
			# This is extremely conservative, but since it only affects
			# includes in .cpp files, rather than .h files,it's not that bad.
			string(REGEX REPLACE "#if.*#endif" "" contents "${contents}")
			string(REGEX REPLACE "#if.*" "" contents "${contents}")
			set(everything "${everything} ${contents}")
		endforeach()
		list(REMOVE_DUPLICATES extra_include_dirs)
		include_directories(BEFORE ${extra_include_dirs})
		string(REGEX MATCHALL "(^|\n)#include +[\"<][^\">]+[\">]" include_directives "${everything}")
		list(REMOVE_DUPLICATES include_directives)
		string(REPLACE ";" "" include_directives "${include_directives}")
		set(pch_h "// Includes extracted from other source files.\n// Not a source file, do not edit!\n${include_directives}")
		# This is actually a .h file, but trick cmake into compiling it as a source file
		set(pch_out_filename "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${lib}_pch.dir/pch.h")
		file(WRITE "pch.h" "${pch_h}")
		file(WRITE "${pch_out_filename}" "${pch_h}")

		add_custom_command(
			OUTPUT "${pch_out_filename}"
			DEPENDS "${pch_out_filename}.o"
			COMMAND ln -fs "${pch_out_filename}.o" "${pch_out_filename}.gch"
			COMMAND ln -fs "${pch_out_filename}.o" "${pch_out_filename}.pch"
			COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/pch.h" "${pch_out_filename}")

		set_source_files_properties(
			${srcs} PROPERTIES
			COMPILE_FLAGS "-include ${pch_out_filename}")

		if(("${CMAKE_GENERATOR}" STREQUAL "Ninja") AND (NOT PCH_SLOW_BUT_SAFE))
			include(Compiler/GNU-CXX)
			set_source_files_properties(
				${srcs} PROPERTIES
				LANGUAGE CXXMD)
		endif()

		if(PCH_SLOW_BUT_SAFE)
			set_source_files_properties(
				${srcs} PROPERTIES
				OBJECT_DEPENDS "{$pch_out_filename}")
		endif()

		if(APPLE)
			set(type objective-c++-header)
		else()
			set(type c++-header)
		endif()

		set_source_files_properties(
			pch.h PROPERTIES
			COMPILE_FLAGS "-x ${type}"
			HEADER_FILE_ONLY 0
			LANGUAGE CXX)

		set_property(
			SOURCE pch.h
			PROPERTY INCLUDE_DIRECTORIES ${extra_include_dirs})
		add_library(${lib}_pch STATIC pch.h)
		set_target_properties(
			${lib}_pch PROPERTIES
			LINKER_LANGUAGE FAKELANG)
		add_dependencies(${lib} ${lib}_pch)
	endif(ENABLE_PCH)

endmacro(add_dolphin_library)

add_subdirectory(Core)
add_subdirectory(Plugins)

if (DSPTOOL)
	add_subdirectory(DSPTool)
endif()

if (UNITTESTS)
	add_subdirectory(UnitTests)
endif()




# TODO: Add DSPSpy and TestSuite. Preferrably make them option()s and cpack components
