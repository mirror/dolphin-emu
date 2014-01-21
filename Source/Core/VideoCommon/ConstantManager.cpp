// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <cmath>

#include "Common.h"
#include "Statistics.h"
#include "ConstantManager.h"
#include "VideoCommon.h"
#include "VideoConfig.h"
#include "BPMemory.h"
#include "XFMemory.h"
#include "MathUtil.h"
#include "CPMemory.h"
#include "VertexManagerBase.h"

#include "RenderBase.h"
static bool s_bFogRangeAdjustChanged;

float GC_ALIGNED16(g_fProjectionMatrix[16]);

// track changes
static bool bTexMatricesChanged[2], bPosNormalMatrixChanged, bProjectionChanged, bViewportChanged;
static int nTransformMatricesChanged[2]; // min,max
static int nNormalMatricesChanged[2]; // min,max
static int nPostTransformMatricesChanged[2]; // min,max
static int nLightsChanged[2]; // min,max

static Matrix44 s_viewportCorrection;
static Matrix33 s_viewRotationMatrix;
static Matrix33 s_viewInvRotationMatrix;
static float s_fViewTranslationVector[3];
static float s_fViewRotation[2];

Constants ConstantManager::constants;
bool ConstantManager::dirty;

struct ProjectionHack
{
	float sign;
	float value;
	ProjectionHack() { }
	ProjectionHack(float new_sign, float new_value)
		: sign(new_sign), value(new_value) {}
};

namespace
{
// Control Variables
static ProjectionHack g_ProjHack1;
static ProjectionHack g_ProjHack2;
static bool g_ProjHack3;
} // Namespace

float PHackValue(std::string sValue)
{
	float f = 0;
	bool fp = false;
	const char *cStr = sValue.c_str();
	char *c = new char[strlen(cStr)+1];
	std::istringstream sTof("");

	for (unsigned int i=0; i<=strlen(cStr); ++i)
	{
		if (i == 20)
		{
			c[i] = '\0';
			break;
		}

		c[i] = (cStr[i] == ',') ? '.' : *(cStr+i);
		if (c[i] == '.')
			fp = true;
	}

	cStr = c;
	sTof.str(cStr);
	sTof >> f;

	if (!fp)
		f /= 0xF4240;

	delete [] c;
	return f;
}

void UpdateProjectionHack(int iPhackvalue[], std::string sPhackvalue[])
{
	float fhackvalue1 = 0, fhackvalue2 = 0;
	float fhacksign1 = 1.0, fhacksign2 = 1.0;
	bool bProjHack3 = false;
	const char *sTemp[2];

	if (iPhackvalue[0] == 1)
	{
		NOTICE_LOG(VIDEO, "\t\t--- Orthographic Projection Hack ON ---");

		fhacksign1 *= (iPhackvalue[1] == 1) ? -1.0f : fhacksign1;
		sTemp[0] = (iPhackvalue[1] == 1) ? " * (-1)" : "";
		fhacksign2 *= (iPhackvalue[2] == 1) ? -1.0f : fhacksign2;
		sTemp[1] = (iPhackvalue[2] == 1) ? " * (-1)" : "";

		fhackvalue1 = PHackValue(sPhackvalue[0]);
		NOTICE_LOG(VIDEO, "- zNear Correction = (%f + zNear)%s", fhackvalue1, sTemp[0]);

		fhackvalue2 = PHackValue(sPhackvalue[1]);
		NOTICE_LOG(VIDEO, "- zFar Correction =  (%f + zFar)%s", fhackvalue2, sTemp[1]);

		sTemp[0] = "DISABLED";
		bProjHack3 = (iPhackvalue[3] == 1) ? true : bProjHack3;
		if (bProjHack3)
			sTemp[0] = "ENABLED";
		NOTICE_LOG(VIDEO, "- Extra Parameter: %s", sTemp[0]);
	}

	// Set the projections hacks
	g_ProjHack1 = ProjectionHack(fhacksign1, fhackvalue1);
	g_ProjHack2 = ProjectionHack(fhacksign2, fhackvalue2);
	g_ProjHack3 = bProjHack3;
}


// Viewport correction:
// In D3D, the viewport rectangle must fit within the render target.
// Say you want a viewport at (ix, iy) with size (iw, ih),
// but your viewport must be clamped at (ax, ay) with size (aw, ah).
// Just multiply the projection matrix with the following to get the same
// effect:
// [   (iw/aw)         0     0    ((iw - 2*(ax-ix)) / aw - 1)   ]
// [         0   (ih/ah)     0   ((-ih + 2*(ay-iy)) / ah + 1)   ]
// [         0         0     1                              0   ]
// [         0         0     0                              1   ]
static void ViewportCorrectionMatrix(Matrix44& result)
{
	int scissorXOff = bpmem.scissorOffset.x * 2;
	int scissorYOff = bpmem.scissorOffset.y * 2;

	// TODO: ceil, floor or just cast to int?
	// TODO: Directly use the floats instead of rounding them?
	float intendedX = xfregs.viewport.xOrig - xfregs.viewport.wd - scissorXOff;
	float intendedY = xfregs.viewport.yOrig + xfregs.viewport.ht - scissorYOff;
	float intendedWd = 2.0f * xfregs.viewport.wd;
	float intendedHt = -2.0f * xfregs.viewport.ht;
	
        if (intendedWd < 0.f)
        {
                intendedX += intendedWd;
                intendedWd = -intendedWd;
        }
        if (intendedHt < 0.f)
        {
                intendedY += intendedHt;
                intendedHt = -intendedHt;
        }

	// fit to EFB size
        float X = (intendedX >= 0.f) ? intendedX : 0.f;
        float Y = (intendedY >= 0.f) ? intendedY : 0.f;
        float Wd = (X + intendedWd <= EFB_WIDTH) ? intendedWd : (EFB_WIDTH - X);
        float Ht = (Y + intendedHt <= EFB_HEIGHT) ? intendedHt : (EFB_HEIGHT - Y);
	
	Matrix44::LoadIdentity(result);
	if (Wd == 0 || Ht == 0)
		return;
	
	result.data[4*0+0] = intendedWd / Wd;
	result.data[4*0+3] = (intendedWd - 2.f * (X - intendedX)) / Wd - 1.f;
	result.data[4*1+1] = intendedHt / Ht;
	result.data[4*1+3] = (-intendedHt + 2.f * (Y - intendedY)) / Ht + 1.f;
}

void UpdateViewport();

void ConstantManager::Init()
{
	memset(&constants, 0, sizeof(constants));

	memset(&xfregs, 0, sizeof(xfregs));
	memset(xfmem, 0, sizeof(xfmem));
	memset(&constants, 0 , sizeof(constants));
	ResetView();

	// TODO: should these go inside ResetView()?
	Matrix44::LoadIdentity(s_viewportCorrection);
	memset(g_fProjectionMatrix, 0, sizeof(g_fProjectionMatrix));
	for (int i = 0; i < 4; ++i)
		g_fProjectionMatrix[i*5] = 1.0f;
	
	Dirty();
}

void ConstantManager::Dirty()
{
	s_bFogRangeAdjustChanged = true;
	bViewportChanged = true;
	
	nTransformMatricesChanged[0] = 0;
	nTransformMatricesChanged[1] = 256;

	nNormalMatricesChanged[0] = 0;
	nNormalMatricesChanged[1] = 96;

	nPostTransformMatricesChanged[0] = 0;
	nPostTransformMatricesChanged[1] = 256;

	nLightsChanged[0] = 0;
	nLightsChanged[1] = 0x80;

	bPosNormalMatrixChanged = true;
	bTexMatricesChanged[0] = true;
	bTexMatricesChanged[1] = true;

	bProjectionChanged = true;

	SetColorChanged(0, 0);
	SetColorChanged(0, 1);
	SetColorChanged(0, 2);
	SetColorChanged(0, 3);
	SetColorChanged(1, 0);
	SetColorChanged(1, 1);
	SetColorChanged(1, 2);
	SetColorChanged(1, 3);
	SetAlpha();
	SetDestAlpha();
	SetZTextureBias();
	SetViewportChanged();
	SetIndTexScaleChanged(false);
	SetIndTexScaleChanged(true);
	SetIndMatrixChanged(0);
	SetIndMatrixChanged(1);
	SetIndMatrixChanged(2);
	SetZTextureTypeChanged();
	SetTexCoordChanged(0);
	SetTexCoordChanged(1);
	SetTexCoordChanged(2);
	SetTexCoordChanged(3);
	SetTexCoordChanged(4);
	SetTexCoordChanged(5);
	SetTexCoordChanged(6);
	SetTexCoordChanged(7);
	SetFogColorChanged();
	SetFogParamChanged();
}

void ConstantManager::Shutdown()
{

}

void ConstantManager::SetConstants()
{
	if (s_bFogRangeAdjustChanged)
	{
		// set by two components, so keep changed flag here
		// TODO: try to split both registers and move this logic to the shader
		if(!g_ActiveConfig.bDisableFog && bpmem.fogRange.Base.Enabled == 1)
		{
			//bpmem.fogRange.Base.Center : center of the viewport in x axis. observation: bpmem.fogRange.Base.Center = realcenter + 342;
			int center = ((u32)bpmem.fogRange.Base.Center) - 342;
			// normalize center to make calculations easy
			float ScreenSpaceCenter = center / (2.0f * xfregs.viewport.wd);
			ScreenSpaceCenter = (ScreenSpaceCenter * 2.0f) - 1.0f;
			//bpmem.fogRange.K seems to be  a table of precalculated coefficients for the adjust factor
			//observations: bpmem.fogRange.K[0].LO appears to be the lowest value and bpmem.fogRange.K[4].HI the largest
			// they always seems to be larger than 256 so my theory is :
			// they are the coefficients from the center to the border of the screen
			// so to simplify I use the hi coefficient as K in the shader taking 256 as the scale
			constants.fog[2][0] = ScreenSpaceCenter;
			constants.fog[2][1] = (float)Renderer::EFBToScaledX((int)(2.0f * xfregs.viewport.wd));
			constants.fog[2][2] = bpmem.fogRange.K[4].HI / 256.0f;
		}
		else
		{
			constants.fog[2][0] = 0;
			constants.fog[2][1] = 1;
			constants.fog[2][2] = 1;
		}
		dirty = true;

		s_bFogRangeAdjustChanged = false;
	}
	
	if (nTransformMatricesChanged[0] >= 0)
	{
		int startn = nTransformMatricesChanged[0] / 4;
		int endn = (nTransformMatricesChanged[1] + 3) / 4;
		memcpy(constants.transformmatrices[startn], &xfmem[startn * 4], (endn - startn) * 16);
		dirty = true;
		nTransformMatricesChanged[0] = nTransformMatricesChanged[1] = -1;
	}

	if (nNormalMatricesChanged[0] >= 0)
	{
		int startn = nNormalMatricesChanged[0] / 3;
		int endn = (nNormalMatricesChanged[1] + 2) / 3;
		for(int i=startn; i<endn; i++)
		{
			memcpy(constants.normalmatrices[i], &xfmem[XFMEM_NORMALMATRICES + 3*i], 12);
		}
		dirty = true;
		nNormalMatricesChanged[0] = nNormalMatricesChanged[1] = -1;
	}

	if (nPostTransformMatricesChanged[0] >= 0)
	{
		int startn = nPostTransformMatricesChanged[0] / 4;
		int endn = (nPostTransformMatricesChanged[1] + 3 ) / 4;
		memcpy(constants.posttransformmatrices[startn], &xfmem[XFMEM_POSTMATRICES + startn * 4], (endn - startn) * 16);
		dirty = true;
		nPostTransformMatricesChanged[0] = nPostTransformMatricesChanged[1] = -1;
	}

	if (nLightsChanged[0] >= 0)
	{
		// lights don't have a 1 to 1 mapping, the color component needs to be converted to 4 floats
		int istart = nLightsChanged[0] / 0x10;
		int iend = (nLightsChanged[1] + 15) / 0x10;
		const float* xfmemptr = (const float*)&xfmem[0x10 * istart + XFMEM_LIGHTS];

		for (int i = istart; i < iend; ++i)
		{
			u32 color = *(const u32*)(xfmemptr + 3);
			constants.lights[5*i][0] = ((color >> 24) & 0xFF) / 255.0f;
			constants.lights[5*i][1] = ((color >> 16) & 0xFF) / 255.0f;
			constants.lights[5*i][2] = ((color >> 8)  & 0xFF) / 255.0f;
			constants.lights[5*i][3] = ((color)       & 0xFF) / 255.0f;
			xfmemptr += 4;

			for (int j = 0; j < 4; ++j, xfmemptr += 3)
			{
				if (j == 1 &&
					fabs(xfmemptr[0]) < 0.00001f &&
					fabs(xfmemptr[1]) < 0.00001f &&
					fabs(xfmemptr[2]) < 0.00001f)
				{
					// dist attenuation, make sure not equal to 0!!!
					constants.lights[5*i+j+1][0] = 0.00001f;
				}
				else
					constants.lights[5*i+j+1][0] = xfmemptr[0];
				constants.lights[5*i+j+1][1] = xfmemptr[1];
				constants.lights[5*i+j+1][2] = xfmemptr[2];
			}
		}
		dirty = true;

		nLightsChanged[0] = nLightsChanged[1] = -1;
	}

	if (bPosNormalMatrixChanged)
	{
		bPosNormalMatrixChanged = false;

		const float *pos = (const float *)xfmem + MatrixIndexA.PosNormalMtxIdx * 4;
		const float *norm = (const float *)xfmem + XFMEM_NORMALMATRICES + 3 * (MatrixIndexA.PosNormalMtxIdx & 31);

		memcpy(constants.posnormalmatrix, pos, 3*16);
		memcpy(constants.posnormalmatrix[3], norm, 12);
		memcpy(constants.posnormalmatrix[4], norm+3, 12);
		memcpy(constants.posnormalmatrix[5], norm+6, 12);
		dirty = true;
	}

	if (bTexMatricesChanged[0])
	{
		bTexMatricesChanged[0] = false;
		const float *fptrs[] =
		{
			(const float *)xfmem + MatrixIndexA.Tex0MtxIdx * 4, (const float *)xfmem + MatrixIndexA.Tex1MtxIdx * 4,
			(const float *)xfmem + MatrixIndexA.Tex2MtxIdx * 4, (const float *)xfmem + MatrixIndexA.Tex3MtxIdx * 4
		};

		for (int i = 0; i < 4; ++i)
		{
			memcpy(constants.texmatrices[3*i], fptrs[i], 3*16);
		}
		dirty = true;
	}

	if (bTexMatricesChanged[1])
	{
		bTexMatricesChanged[1] = false;
		const float *fptrs[] = {
			(const float *)xfmem + MatrixIndexB.Tex4MtxIdx * 4, (const float *)xfmem + MatrixIndexB.Tex5MtxIdx * 4,
			(const float *)xfmem + MatrixIndexB.Tex6MtxIdx * 4, (const float *)xfmem + MatrixIndexB.Tex7MtxIdx * 4
		};

		for (int i = 0; i < 4; ++i)
		{
			memcpy(constants.texmatrices[3*i+12], fptrs[i], 3*16);
		}
		dirty = true;
	}

	if (bViewportChanged)
	{
		bViewportChanged = false;
		constants.zbias[1][0] = xfregs.viewport.farZ / 16777216.0f;
		constants.zbias[1][1] = xfregs.viewport.zRange / 16777216.0f;
		
		dirty = true;
		// This is so implementation-dependent that we can't have it here.
		UpdateViewport();
		
		// Update projection if the viewport isn't 1:1 useable
		if(!g_ActiveConfig.backend_info.bSupportsOversizedViewports)
		{
			ViewportCorrectionMatrix(s_viewportCorrection);
			bProjectionChanged = true;			
		}
	}

	if (bProjectionChanged)
	{
		bProjectionChanged = false;

		float *rawProjection = xfregs.projection.rawProjection;

		switch(xfregs.projection.type)
		{
		case GX_PERSPECTIVE:

			g_fProjectionMatrix[0] = rawProjection[0] * g_ActiveConfig.fAspectRatioHackW;
			g_fProjectionMatrix[1] = 0.0f;
			g_fProjectionMatrix[2] = rawProjection[1];
			g_fProjectionMatrix[3] = 0.0f;

			g_fProjectionMatrix[4] = 0.0f;
			g_fProjectionMatrix[5] = rawProjection[2] * g_ActiveConfig.fAspectRatioHackH;
			g_fProjectionMatrix[6] = rawProjection[3];
			g_fProjectionMatrix[7] = 0.0f;

			g_fProjectionMatrix[8] = 0.0f;
			g_fProjectionMatrix[9] = 0.0f;
			g_fProjectionMatrix[10] = rawProjection[4];

			g_fProjectionMatrix[11] = rawProjection[5];

			g_fProjectionMatrix[12] = 0.0f;
			g_fProjectionMatrix[13] = 0.0f;
			// donkopunchstania: GC GPU rounds differently?
			// -(1 + epsilon) so objects are clipped as they are on the real HW
			g_fProjectionMatrix[14] = -1.00000011921f;
			g_fProjectionMatrix[15] = 0.0f;

			SETSTAT_FT(stats.gproj_0, g_fProjectionMatrix[0]);
			SETSTAT_FT(stats.gproj_1, g_fProjectionMatrix[1]);
			SETSTAT_FT(stats.gproj_2, g_fProjectionMatrix[2]);
			SETSTAT_FT(stats.gproj_3, g_fProjectionMatrix[3]);
			SETSTAT_FT(stats.gproj_4, g_fProjectionMatrix[4]);
			SETSTAT_FT(stats.gproj_5, g_fProjectionMatrix[5]);
			SETSTAT_FT(stats.gproj_6, g_fProjectionMatrix[6]);
			SETSTAT_FT(stats.gproj_7, g_fProjectionMatrix[7]);
			SETSTAT_FT(stats.gproj_8, g_fProjectionMatrix[8]);
			SETSTAT_FT(stats.gproj_9, g_fProjectionMatrix[9]);
			SETSTAT_FT(stats.gproj_10, g_fProjectionMatrix[10]);
			SETSTAT_FT(stats.gproj_11, g_fProjectionMatrix[11]);
			SETSTAT_FT(stats.gproj_12, g_fProjectionMatrix[12]);
			SETSTAT_FT(stats.gproj_13, g_fProjectionMatrix[13]);
			SETSTAT_FT(stats.gproj_14, g_fProjectionMatrix[14]);
			SETSTAT_FT(stats.gproj_15, g_fProjectionMatrix[15]);
			break;

		case GX_ORTHOGRAPHIC:

			g_fProjectionMatrix[0] = rawProjection[0];
			g_fProjectionMatrix[1] = 0.0f;
			g_fProjectionMatrix[2] = 0.0f;
			g_fProjectionMatrix[3] = rawProjection[1];

			g_fProjectionMatrix[4] = 0.0f;
			g_fProjectionMatrix[5] = rawProjection[2];
			g_fProjectionMatrix[6] = 0.0f;
			g_fProjectionMatrix[7] = rawProjection[3];

			g_fProjectionMatrix[8] = 0.0f;
			g_fProjectionMatrix[9] = 0.0f;
			g_fProjectionMatrix[10] = (g_ProjHack1.value + rawProjection[4]) * ((g_ProjHack1.sign == 0) ? 1.0f : g_ProjHack1.sign);
			g_fProjectionMatrix[11] = (g_ProjHack2.value + rawProjection[5]) * ((g_ProjHack2.sign == 0) ? 1.0f : g_ProjHack2.sign);

			g_fProjectionMatrix[12] = 0.0f;
			g_fProjectionMatrix[13] = 0.0f;

			/*
			projection hack for metroid other m...attempt to remove black projection layer from cut scenes.
			g_fProjectionMatrix[15] = 1.0f was the default setting before
			this hack was added...setting g_fProjectionMatrix[14] to -1 might make the hack more stable, needs more testing.
			Only works for OGL and DX9...this is not helping DX11
			*/

			g_fProjectionMatrix[14] = 0.0f;
			g_fProjectionMatrix[15] = (g_ProjHack3 && rawProjection[0] == 2.0f ? 0.0f : 1.0f);  //causes either the efb copy or bloom layer not to show if proj hack enabled

			SETSTAT_FT(stats.g2proj_0, g_fProjectionMatrix[0]);
			SETSTAT_FT(stats.g2proj_1, g_fProjectionMatrix[1]);
			SETSTAT_FT(stats.g2proj_2, g_fProjectionMatrix[2]);
			SETSTAT_FT(stats.g2proj_3, g_fProjectionMatrix[3]);
			SETSTAT_FT(stats.g2proj_4, g_fProjectionMatrix[4]);
			SETSTAT_FT(stats.g2proj_5, g_fProjectionMatrix[5]);
			SETSTAT_FT(stats.g2proj_6, g_fProjectionMatrix[6]);
			SETSTAT_FT(stats.g2proj_7, g_fProjectionMatrix[7]);
			SETSTAT_FT(stats.g2proj_8, g_fProjectionMatrix[8]);
			SETSTAT_FT(stats.g2proj_9, g_fProjectionMatrix[9]);
			SETSTAT_FT(stats.g2proj_10, g_fProjectionMatrix[10]);
			SETSTAT_FT(stats.g2proj_11, g_fProjectionMatrix[11]);
			SETSTAT_FT(stats.g2proj_12, g_fProjectionMatrix[12]);
			SETSTAT_FT(stats.g2proj_13, g_fProjectionMatrix[13]);
			SETSTAT_FT(stats.g2proj_14, g_fProjectionMatrix[14]);
			SETSTAT_FT(stats.g2proj_15, g_fProjectionMatrix[15]);
			SETSTAT_FT(stats.proj_0, rawProjection[0]);
			SETSTAT_FT(stats.proj_1, rawProjection[1]);
			SETSTAT_FT(stats.proj_2, rawProjection[2]);
			SETSTAT_FT(stats.proj_3, rawProjection[3]);
			SETSTAT_FT(stats.proj_4, rawProjection[4]);
			SETSTAT_FT(stats.proj_5, rawProjection[5]);
			break;

		default:
			ERROR_LOG(VIDEO, "Unknown projection type: %d", xfregs.projection.type);
		}

		PRIM_LOG("Projection: %f %f %f %f %f %f\n", rawProjection[0], rawProjection[1], rawProjection[2], rawProjection[3], rawProjection[4], rawProjection[5]);

		if ((g_ActiveConfig.bFreeLook || g_ActiveConfig.bAnaglyphStereo ) && xfregs.projection.type == GX_PERSPECTIVE)
		{
			Matrix44 mtxA;
			Matrix44 mtxB;
			Matrix44 viewMtx;

			Matrix44::Translate(mtxA, s_fViewTranslationVector);
			Matrix44::LoadMatrix33(mtxB, s_viewRotationMatrix);
			Matrix44::Multiply(mtxB, mtxA, viewMtx); // view = rotation x translation
			Matrix44::Set(mtxB, g_fProjectionMatrix);
			Matrix44::Multiply(mtxB, viewMtx, mtxA); // mtxA = projection x view
			Matrix44::Multiply(s_viewportCorrection, mtxA, mtxB); // mtxB = viewportCorrection x mtxA
			memcpy(constants.projection, mtxB.data, 4*16);
		}
		else
		{
			Matrix44 projMtx;
			Matrix44::Set(projMtx, g_fProjectionMatrix);

			Matrix44 correctedMtx;
			Matrix44::Multiply(s_viewportCorrection, projMtx, correctedMtx);
			memcpy(constants.projection, correctedMtx.data, 4*16);
		}
		dirty = true;
	}
}

// This one is high in profiles (0.5%).
// TODO: Move conversion out, only store the raw color value
// and update it when the shader constant is set, only.
// TODO: Conversion should be checked in the context of tev_fixes..
void ConstantManager::SetColorChanged(int type, int num)
{
	float4* c = type ? constants.kcolors : constants.colors;
	c[num][0] = bpmem.tevregs[num].low.a / 255.0f;
	c[num][3] = bpmem.tevregs[num].low.b / 255.0f;
	c[num][2] = bpmem.tevregs[num].high.a / 255.0f;
	c[num][1] = bpmem.tevregs[num].high.b / 255.0f;
	dirty = true;

	PRIM_LOG("pixel %scolor%d: %f %f %f %f\n", type?"k":"", num, c[num][0], c[num][1], c[num][2], c[num][3]);
}

void ConstantManager::SetAlpha()
{
	constants.alpha[0][0] = bpmem.alpha_test.ref0 / 255.0f;
	constants.alpha[0][1] = bpmem.alpha_test.ref1 / 255.0f;
	dirty = true;
}

void ConstantManager::SetDestAlpha()
{
	constants.alpha[0][3] = bpmem.dstalpha.alpha / 255.0f;
	dirty = true;
}

void ConstantManager::SetTexDims(int texmapid, u32 width, u32 height, u32 wraps, u32 wrapt)
{
	// TODO: move this check out to callee. There we could just call this function on texture changes
	// or better, use textureSize() in glsl
	if(constants.texdims[texmapid][0] != 1.0f/width || constants.texdims[texmapid][1] != 1.0f/height)
		dirty = true;

	constants.texdims[texmapid][0] = 1.0f/width;
	constants.texdims[texmapid][1] = 1.0f/height;
}

void ConstantManager::SetZTextureBias()
{
	constants.zbias[1][3] = bpmem.ztex1.bias/16777215.0f;
	dirty = true;
}

void ConstantManager::SetViewportChanged()
{
	bViewportChanged = true;
	s_bFogRangeAdjustChanged = true; // TODO: Shouldn't be necessary with an accurate fog range adjust implementation
}

void ConstantManager::SetIndTexScaleChanged(bool high)
{
	constants.indtexscale[high][0] = bpmem.texscale[high].getScaleS(0);
	constants.indtexscale[high][1] = bpmem.texscale[high].getScaleT(0);
	constants.indtexscale[high][2] = bpmem.texscale[high].getScaleS(1);
	constants.indtexscale[high][3] = bpmem.texscale[high].getScaleT(1);
	dirty = true;
}

void ConstantManager::SetIndMatrixChanged(int matrixidx)
{
	int scale = ((u32)bpmem.indmtx[matrixidx].col0.s0 << 0) |
			((u32)bpmem.indmtx[matrixidx].col1.s1 << 2) |
			((u32)bpmem.indmtx[matrixidx].col2.s2 << 4);
	float fscale = powf(2.0f, (float)(scale - 17)) / 1024.0f;

	// xyz - static matrix
	// TODO w - dynamic matrix scale / 256...... somehow / 4 works better
	// rev 2972 - now using / 256.... verify that this works
	constants.indtexmtx[2*matrixidx][0] = bpmem.indmtx[matrixidx].col0.ma * fscale;
	constants.indtexmtx[2*matrixidx][1] = bpmem.indmtx[matrixidx].col1.mc * fscale;
	constants.indtexmtx[2*matrixidx][2] = bpmem.indmtx[matrixidx].col2.me * fscale;
	constants.indtexmtx[2*matrixidx][3] = fscale * 4.0f;
	constants.indtexmtx[2*matrixidx+1][0] = bpmem.indmtx[matrixidx].col0.mb * fscale;
	constants.indtexmtx[2*matrixidx+1][1] = bpmem.indmtx[matrixidx].col1.md * fscale;
	constants.indtexmtx[2*matrixidx+1][2] = bpmem.indmtx[matrixidx].col2.mf * fscale;
	constants.indtexmtx[2*matrixidx+1][3] = fscale * 4.0f;
	dirty = true;

	PRIM_LOG("indmtx%d: scale=%f, mat=(%f %f %f; %f %f %f)\n",
			matrixidx, 1024.0f*fscale,
			bpmem.indmtx[matrixidx].col0.ma * fscale, bpmem.indmtx[matrixidx].col1.mc * fscale, bpmem.indmtx[matrixidx].col2.me * fscale,
		bpmem.indmtx[matrixidx].col0.mb * fscale, bpmem.indmtx[matrixidx].col1.md * fscale, bpmem.indmtx[matrixidx].col2.mf * fscale);

}

void ConstantManager::SetZTextureTypeChanged()
{
	switch (bpmem.ztex2.type)
	{
		case TEV_ZTEX_TYPE_U8:
			constants.zbias[0][0] = 0;
			constants.zbias[0][1] = 0;
			constants.zbias[0][2] = 0;
			constants.zbias[0][3] = 255.0f/16777215.0f;
			break;
		case TEV_ZTEX_TYPE_U16:
			constants.zbias[0][0] = 255.0f/16777215.0f;
			constants.zbias[0][1] = 0;
			constants.zbias[0][2] = 0;
			constants.zbias[0][3] = 65280.0f/16777215.0f;
			break;
		case TEV_ZTEX_TYPE_U24:
			constants.zbias[0][0] = 16711680.0f/16777215.0f;
			constants.zbias[0][1] = 65280.0f/16777215.0f;
			constants.zbias[0][2] = 255.0f/16777215.0f;
			constants.zbias[0][3] = 0;
			break;
		default:
			break;
        }
        dirty = true;
}

void ConstantManager::SetTexCoordChanged(u8 texmapid)
{
	TCoordInfo& tc = bpmem.texcoords[texmapid];
	constants.texdims[texmapid][2] = (float)(tc.s.scale_minus_1 + 1);
	constants.texdims[texmapid][3] = (float)(tc.t.scale_minus_1 + 1);
	dirty = true;
}

void ConstantManager::SetFogColorChanged()
{
	constants.fog[0][0] = bpmem.fog.color.r / 255.0f;
	constants.fog[0][1] = bpmem.fog.color.g / 255.0f;
	constants.fog[0][2] = bpmem.fog.color.b / 255.0f;
	dirty = true;
}

void ConstantManager::SetFogParamChanged()
{
	if(!g_ActiveConfig.bDisableFog)
	{
		constants.fog[1][0] = bpmem.fog.a.GetA();
		constants.fog[1][1] = (float)bpmem.fog.b_magnitude / 0xFFFFFF;
		constants.fog[1][2] = bpmem.fog.c_proj_fsel.GetC();
		constants.fog[1][3] = (float)(1 << bpmem.fog.b_shift);
	}
	else
	{
		constants.fog[1][0] = 0;
		constants.fog[1][1] = 1;
		constants.fog[1][2] = 0;
		constants.fog[1][3] = 1;
	}
	dirty = true;
}

void ConstantManager::SetFogRangeAdjustChanged()
{
	s_bFogRangeAdjustChanged = true;
}

void ConstantManager::InvalidateXFRange(int start, int end)
{
	if (((u32)start >= (u32)MatrixIndexA.PosNormalMtxIdx * 4 &&
		 (u32)start <  (u32)MatrixIndexA.PosNormalMtxIdx * 4 + 12) ||
		((u32)start >= XFMEM_NORMALMATRICES + ((u32)MatrixIndexA.PosNormalMtxIdx & 31) * 3 &&
		 (u32)start <  XFMEM_NORMALMATRICES + ((u32)MatrixIndexA.PosNormalMtxIdx & 31) * 3 + 9))
	{
		bPosNormalMatrixChanged = true;
	}

	if (((u32)start >= (u32)MatrixIndexA.Tex0MtxIdx*4 && (u32)start < (u32)MatrixIndexA.Tex0MtxIdx*4+12) ||
		((u32)start >= (u32)MatrixIndexA.Tex1MtxIdx*4 && (u32)start < (u32)MatrixIndexA.Tex1MtxIdx*4+12) ||
		((u32)start >= (u32)MatrixIndexA.Tex2MtxIdx*4 && (u32)start < (u32)MatrixIndexA.Tex2MtxIdx*4+12) ||
		((u32)start >= (u32)MatrixIndexA.Tex3MtxIdx*4 && (u32)start < (u32)MatrixIndexA.Tex3MtxIdx*4+12))
	{
		bTexMatricesChanged[0] = true;
	}

	if (((u32)start >= (u32)MatrixIndexB.Tex4MtxIdx*4 && (u32)start < (u32)MatrixIndexB.Tex4MtxIdx*4+12) ||
		((u32)start >= (u32)MatrixIndexB.Tex5MtxIdx*4 && (u32)start < (u32)MatrixIndexB.Tex5MtxIdx*4+12) ||
		((u32)start >= (u32)MatrixIndexB.Tex6MtxIdx*4 && (u32)start < (u32)MatrixIndexB.Tex6MtxIdx*4+12) ||
		((u32)start >= (u32)MatrixIndexB.Tex7MtxIdx*4 && (u32)start < (u32)MatrixIndexB.Tex7MtxIdx*4+12))
	{
		bTexMatricesChanged[1] = true;
	}

	if (start < XFMEM_POSMATRICES_END)
	{
		if (nTransformMatricesChanged[0] == -1)
		{
			nTransformMatricesChanged[0] = start;
			nTransformMatricesChanged[1] = end>XFMEM_POSMATRICES_END?XFMEM_POSMATRICES_END:end;
		}
		else
		{
			if (nTransformMatricesChanged[0] > start) nTransformMatricesChanged[0] = start;
			if (nTransformMatricesChanged[1] < end) nTransformMatricesChanged[1] = end>XFMEM_POSMATRICES_END?XFMEM_POSMATRICES_END:end;
		}
	}

	if (start < XFMEM_NORMALMATRICES_END && end > XFMEM_NORMALMATRICES)
	{
		int _start = start < XFMEM_NORMALMATRICES ? 0 : start-XFMEM_NORMALMATRICES;
		int _end = end < XFMEM_NORMALMATRICES_END ? end-XFMEM_NORMALMATRICES : XFMEM_NORMALMATRICES_END-XFMEM_NORMALMATRICES;

		if (nNormalMatricesChanged[0] == -1)
		{
			nNormalMatricesChanged[0] = _start;
			nNormalMatricesChanged[1] = _end;
		}
		else
		{
			if (nNormalMatricesChanged[0] > _start) nNormalMatricesChanged[0] = _start;
			if (nNormalMatricesChanged[1] < _end) nNormalMatricesChanged[1] = _end;
		}
	}

	if (start < XFMEM_POSTMATRICES_END && end > XFMEM_POSTMATRICES)
	{
		int _start = start < XFMEM_POSTMATRICES ? XFMEM_POSTMATRICES : start-XFMEM_POSTMATRICES;
		int _end = end < XFMEM_POSTMATRICES_END ? end-XFMEM_POSTMATRICES : XFMEM_POSTMATRICES_END-XFMEM_POSTMATRICES;

		if (nPostTransformMatricesChanged[0] == -1)
		{
			nPostTransformMatricesChanged[0] = _start;
			nPostTransformMatricesChanged[1] = _end;
		}
		else
		{
			if (nPostTransformMatricesChanged[0] > _start) nPostTransformMatricesChanged[0] = _start;
			if (nPostTransformMatricesChanged[1] < _end) nPostTransformMatricesChanged[1] = _end;
		}
	}

	if (start < XFMEM_LIGHTS_END && end > XFMEM_LIGHTS)
	{
		int _start = start < XFMEM_LIGHTS ? XFMEM_LIGHTS : start-XFMEM_LIGHTS;
		int _end = end < XFMEM_LIGHTS_END ? end-XFMEM_LIGHTS : XFMEM_LIGHTS_END-XFMEM_LIGHTS;

		if (nLightsChanged[0] == -1 )
		{
			nLightsChanged[0] = _start;
			nLightsChanged[1] = _end;
		}
		else
		{
			if (nLightsChanged[0] > _start) nLightsChanged[0] = _start;
			if (nLightsChanged[1] < _end)   nLightsChanged[1] = _end;
		}
	}
}

void ConstantManager::SetTexMatrixChangedA(u32 Value)
{
	if (MatrixIndexA.Hex != Value)
	{
		VertexManager::Flush();
		if (MatrixIndexA.PosNormalMtxIdx != (Value&0x3f))
			bPosNormalMatrixChanged = true;
		bTexMatricesChanged[0] = true;
		MatrixIndexA.Hex = Value;
	}
}

void ConstantManager::SetTexMatrixChangedB(u32 Value)
{
	if (MatrixIndexB.Hex != Value)
	{
		VertexManager::Flush();
		bTexMatricesChanged[1] = true;
		MatrixIndexB.Hex = Value;
	}
}
void ConstantManager::SetProjectionChanged()
{
	bProjectionChanged = true;
}

void ConstantManager::TranslateView(float x, float y, float z)
{
	float result[3];
	float vector[3] = { x,z,y };

	Matrix33::Multiply(s_viewInvRotationMatrix, vector, result);

	for (int i = 0; i < 3; i++)
		s_fViewTranslationVector[i] += result[i];

	bProjectionChanged = true;
}

void ConstantManager::RotateView(float x, float y)
{
	s_fViewRotation[0] += x;
	s_fViewRotation[1] += y;

	Matrix33 mx;
	Matrix33 my;
	Matrix33::RotateX(mx, s_fViewRotation[1]);
	Matrix33::RotateY(my, s_fViewRotation[0]);
	Matrix33::Multiply(mx, my, s_viewRotationMatrix);

	// reverse rotation
	Matrix33::RotateX(mx, -s_fViewRotation[1]);
	Matrix33::RotateY(my, -s_fViewRotation[0]);
	Matrix33::Multiply(my, mx, s_viewInvRotationMatrix);

	bProjectionChanged = true;
}

void ConstantManager::ResetView()
{
	memset(s_fViewTranslationVector, 0, sizeof(s_fViewTranslationVector));
	Matrix33::LoadIdentity(s_viewRotationMatrix);
	Matrix33::LoadIdentity(s_viewInvRotationMatrix);
	s_fViewRotation[0] = s_fViewRotation[1] = 0.0f;

	bProjectionChanged = true;
}


void ConstantManager::SetMaterialColorChanged(int index, u32 color)
{
	constants.materials[index][0] = ((color >> 24) & 0xFF) / 255.0f;
	constants.materials[index][1] = ((color >> 16) & 0xFF) / 255.0f;
	constants.materials[index][2] = ((color >>  8) & 0xFF) / 255.0f;
	constants.materials[index][3] = ( color        & 0xFF) / 255.0f;
	dirty = true;
}

void ConstantManager::DoState(PointerWrap &p)
{
	p.Do(g_fProjectionMatrix);
	p.Do(s_viewportCorrection);
	p.Do(s_viewRotationMatrix);
	p.Do(s_viewInvRotationMatrix);
	p.Do(s_fViewTranslationVector);
	p.Do(s_fViewRotation);
	p.Do(constants);
	p.Do(dirty);

	if (p.GetMode() == PointerWrap::MODE_READ)
	{
		Dirty();
	}
}
