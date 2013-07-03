// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef GCOGL_VERTEXSHADER_H
#define GCOGL_VERTEXSHADER_H

#include <stdarg.h>
#include "XFMemory.h"
#include "VideoCommon.h"
#include "ShaderGenCommon.h"

// TODO should be reordered
#define SHADER_POSITION_ATTRIB  0
#define SHADER_POSMTX_ATTRIB    1
#define SHADER_NORM0_ATTRIB     2
#define SHADER_NORM1_ATTRIB     3
#define SHADER_NORM2_ATTRIB     4
#define SHADER_COLOR0_ATTRIB    5
#define SHADER_COLOR1_ATTRIB    6

#define SHADER_TEXTURE0_ATTRIB  8
#define SHADER_TEXTURE1_ATTRIB  9
#define SHADER_TEXTURE2_ATTRIB  10
#define SHADER_TEXTURE3_ATTRIB  11
#define SHADER_TEXTURE4_ATTRIB  12
#define SHADER_TEXTURE5_ATTRIB  13
#define SHADER_TEXTURE6_ATTRIB  14
#define SHADER_TEXTURE7_ATTRIB  15



// shader variables
#define I_POSNORMALMATRIX       "cpnmtx"
#define I_PROJECTION            "cproj"
#define I_MATERIALS             "cmtrl"
#define I_LIGHTS                "clights"
#define I_TEXMATRICES           "ctexmtx"
#define I_TRANSFORMMATRICES     "ctrmtx"
#define I_NORMALMATRICES        "cnmtx"
#define I_POSTTRANSFORMMATRICES "cpostmtx"
#define I_DEPTHPARAMS           "cDepth" // farZ, zRange, scaled viewport width, scaled viewport height

#define C_POSNORMALMATRIX        0
#define C_PROJECTION            (C_POSNORMALMATRIX + 6)
#define C_MATERIALS             (C_PROJECTION + 4)
#define C_LIGHTS                (C_MATERIALS + 4)
#define C_TEXMATRICES           (C_LIGHTS + 40)
#define C_TRANSFORMMATRICES     (C_TEXMATRICES + 24)
#define C_NORMALMATRICES        (C_TRANSFORMMATRICES + 64)
#define C_POSTTRANSFORMMATRICES (C_NORMALMATRICES + 32)
#define C_DEPTHPARAMS           (C_POSTTRANSFORMMATRICES + 64)
#define C_VENVCONST_END			(C_DEPTHPARAMS + 1)

const s_svar VSVar_Loc[] = {  {I_POSNORMALMATRIX, C_POSNORMALMATRIX, 6 },
						{I_PROJECTION , C_PROJECTION, 4  },
						{I_MATERIALS, C_MATERIALS, 4 },
						{I_LIGHTS, C_LIGHTS, 40 },
						{I_TEXMATRICES, C_TEXMATRICES, 24 },
						{I_TRANSFORMMATRICES , C_TRANSFORMMATRICES, 64  },
						{I_NORMALMATRICES , C_NORMALMATRICES, 32  },
						{I_POSTTRANSFORMMATRICES, C_POSTTRANSFORMMATRICES, 64 },
						{I_DEPTHPARAMS, C_DEPTHPARAMS, 1 },
						};

#pragma pack(1)

struct vertex_shader_uid_data
{
	u32 NumValues() const { return sizeof(vertex_shader_uid_data); }

	u32 components;
	u32 numColorChans : 2;
	u32 numTexGens : 4;

	u32 dualTexTrans_enabled : 1;
	u32 pixel_lighting : 1;

	u32 texMtxInfo_n_projection : 16; // Stored separately to guarantee that the texMtxInfo struct is 8 bits wide
	struct {
		u32 inputform : 2;
		u32 texgentype : 3;
		u32 sourcerow : 5;
		u32 embosssourceshift : 3;
		u32 embosslightshift : 3;
	} texMtxInfo[8];

	struct {
		u32 index : 6;
		u32 normalize : 1;
		u32 pad : 1;
	} postMtxInfo[8];

	LightingUidData lighting;
};
#pragma pack()

typedef ShaderUid<vertex_shader_uid_data> VertexShaderUid;
typedef ShaderCode VertexShaderCode; // TODO: Obsolete..

void GetVertexShaderUid(VertexShaderUid& object, u32 components, API_TYPE api_type);
void GenerateVertexShaderCode(VertexShaderCode& object, u32 components, API_TYPE api_type);
void GenerateVSOutputStructForGS(ShaderCode& object, u32 components, API_TYPE api_type);

#endif // GCOGL_VERTEXSHADER_H
