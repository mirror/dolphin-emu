// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CONSTANTMANAGER_H
#define _CONSTANTMANAGER_H

// all constant buffer attributes must be 16 bytes aligned, so this are the only allowed components:
typedef float float4[4];
typedef u32 uint4[4];
typedef s32 int4[4];

struct Constants
{
	float4 colors[4];
	float4 kcolors[4];
	float4 alpha;
	float4 texdims[8];
	float4 zbias[2];
	float4 indtexscale[2];
	float4 indtexmtx[6];
	float4 fog[3];

	// For pixel lighting
	float4 plights[40];
	float4 pmaterials[4];
};

struct VertexShaderConstants
{
	float4 posnormalmatrix[6];
	float4 projection[4];
	float4 materials[4];
	float4 lights[40];
	float4 texmatrices[24];
	float4 transformmatrices[64];
	float4 normalmatrices[32];
	float4 posttransformmatrices[64];
	float4 depthparams;
};


// shader variables
#define I_COLORS      "color"
#define I_KCOLORS     "k"
#define I_ALPHA       "alphaRef"
#define I_TEXDIMS     "texdim"
#define I_ZBIAS       "czbias"
#define I_INDTEXSCALE "cindscale"
#define I_INDTEXMTX   "cindmtx"
#define I_FOG         "cfog"
#define I_PLIGHTS     "cPLights"
#define I_PMATERIALS  "cPmtrl"

// TODO: get rid of them as they aren't used
#define C_COLORMATRIX	0						// 0
#define C_COLORS		0						// 0
#define C_KCOLORS		(C_COLORS + 4)			// 4
#define C_ALPHA			(C_KCOLORS + 4)			// 8
#define C_TEXDIMS		(C_ALPHA + 1)			// 9
#define C_ZBIAS			(C_TEXDIMS + 8)			//17
#define C_INDTEXSCALE	(C_ZBIAS + 2)			//19
#define C_INDTEXMTX		(C_INDTEXSCALE + 2)		//21
#define C_FOG			(C_INDTEXMTX + 6)		//27

#define C_PLIGHTS		(C_FOG + 3)
#define C_PMATERIALS	(C_PLIGHTS + 40)
#define C_PENVCONST_END (C_PMATERIALS + 4)

#define I_POSNORMALMATRIX       "cpnmtx"
#define I_PROJECTION            "cproj"
#define I_MATERIALS             "cmtrl"
#define I_LIGHTS                "clights"
#define I_TEXMATRICES           "ctexmtx"
#define I_TRANSFORMMATRICES     "ctrmtx"
#define I_NORMALMATRICES        "cnmtx"
#define I_POSTTRANSFORMMATRICES "cpostmtx"
#define I_DEPTHPARAMS           "cDepth" // farZ, zRange

//TODO: get rid of them, they aren't used at all
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

class ConstantManager
{
public:
	static void Init();
	static void Dirty();
	static void Shutdown();
	static void DoState(PointerWrap &p);

	static void SetConstants(); // sets pixel shader constants

	// constant management, should be called after memory is committed
	static void SetColorChanged(int type, int index);
	static void SetAlpha();
	static void SetDestAlpha();
	static void SetTexDims(int texmapid, u32 width, u32 height, u32 wraps, u32 wrapt);
	static void SetZTextureBias();
	static void SetViewportChanged();
	static void SetIndMatrixChanged(int matrixidx);
	static void SetTevKSelChanged(int id);
	static void SetZTextureTypeChanged();
	static void SetIndTexScaleChanged(bool high);
	static void SetTexCoordChanged(u8 texmapid);
	static void SetFogColorChanged();
	static void SetFogParamChanged();
	static void SetFogRangeAdjustChanged();
	static void InvalidateXFRange(int start, int end);
	static void SetMaterialColorChanged(int index, u32 color);

	static Constants constants;
	static bool dirty;
};

#endif
