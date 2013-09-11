if(NOT USE_GLES OR USE_GLES3)
	add_subdirectory(Backend_OGL)
endif()
add_subdirectory(Backend_Software)
# TODO: Add other backends here!
