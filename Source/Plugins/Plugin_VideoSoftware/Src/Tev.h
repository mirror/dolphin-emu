// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _TEV_H_
#define _TEV_H_

#include "BPMemLoader.h"
#include "ChunkFile.h"

class Tev
{ 
	struct InputRegType
	{
		unsigned a : 8;
		unsigned b : 8;
		unsigned c : 8;
		signed   d : 11;
	};

	struct TextureCoordinateType
	{
		signed s : 24;
		signed t : 24;
	};

	// color order: ABGR
	s32 GC_ALIGNED16(Reg[4][4]);
	s32 GC_ALIGNED16(KonstantColors[4][4]);
	s32 GC_ALIGNED16(TexColor[4]);
	s32 GC_ALIGNED16(RasColor[4]);
	s32 GC_ALIGNED16(StageKonst[4]);
	s32 GC_ALIGNED16(Zero16[4]);

	s32 GC_ALIGNED16(FixedConstants[9]);
	u8  AlphaBump;
	u8  GC_ALIGNED16(IndirectTex[4][4]);
	TextureCoordinateType TexCoord;

	const s32 *m_ColorInputLUT[16][3];
	const s32 *m_AlphaInputLUT[8];        // values must point to ABGR color
	const s32 *m_KonstLUT[32][4];
	const u8 *m_RasColorLUT[8];
	s32 GC_ALIGNED16(m_BiasLUT[4]);
	u8 m_ScaleLShiftLUT[4];
	u8 m_ScaleRShiftLUT[4];

	// enumeration for color input LUT
	enum
	{
		BLU_INP,
		GRN_INP,
		RED_INP		
	};

	enum BufferBase
	{
		DIRECT = 0,
		DIRECT_TFETCH = 16,
		INDIRECT = 32
	};

	void SetRasColor(int colorChan, int swaptable);

	void DrawColorRegular(const TevStageCombiner::ColorCombiner cc);
	void DrawColorCompare(const TevStageCombiner::ColorCombiner cc);
	void DrawAlphaRegular(const TevStageCombiner::AlphaCombiner ac);
	void DrawAlphaCompare(const TevStageCombiner::AlphaCombiner ac);

	void Indirect(unsigned int stageNum, s32 s, s32 t);

public:
	s32 GC_ALIGNED16(Position[3]);
	u8 Color[2][4]; // must be RGBA for correct swap table ordering
	TextureCoordinateType Uv[8];
	s32  GC_ALIGNED16(IndirectLod[4]);
	bool GC_ALIGNED16(IndirectLinear[4]);
	s32  GC_ALIGNED16(TextureLod[16]);
	bool GC_ALIGNED16(TextureLinear[16]);

	void Init();

	void Draw();

	void SetRegColor(int reg, int comp, bool konst, s32 color);

	enum { ALP_C, BLU_C, GRN_C, RED_C };

	void DoState(PointerWrap &p);
};

#endif
