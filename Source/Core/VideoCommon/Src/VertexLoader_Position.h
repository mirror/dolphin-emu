// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef VERTEXLOADER_POSITION_H
#define VERTEXLOADER_POSITION_H

#include "NativeVertexFormat.h"

class VertexLoader_Position {
public:

	// Init
	static void Init(void);

	// GetSize
	static unsigned int GetSize(unsigned int _type, unsigned int _format, unsigned int _elements, unsigned int _frac);

	// GetFunction
	static TPipelineFunction GetFunction(unsigned int _type, unsigned int _format, unsigned int _elements, unsigned int _frac);

	// GetGLType
	static int GetGLType(unsigned int _type, unsigned int _format, unsigned int _elements, unsigned int _frac);

	// GetGLSize
	static int GetGLSize(unsigned int _type, unsigned int _format, unsigned int _elements, unsigned int _frac);
};

#endif
