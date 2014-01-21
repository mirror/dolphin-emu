// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// ADD_CONSTANT(type, name, elements, shadername, register)

ADD_CONSTANT(float4, colors, 4, I_COLORS, C_COLORS)
ADD_CONSTANT(float4, kcolors, 4, I_KCOLORS, C_KCOLORS)
ADD_CONSTANT(float4, alpha, 1, I_ALPHA, C_ALPHA)
ADD_CONSTANT(float4, texdims, 8, I_TEXDIMS, C_TEXDIMS)
ADD_CONSTANT(float4, zbias, 2, I_ZBIAS, C_ZBIAS)
ADD_CONSTANT(float4, indtexscale, 2, I_INDTEXSCALE, C_INDTEXSCALE)
ADD_CONSTANT(float4, indtexmtx, 6, I_INDTEXMTX, C_INDTEXMTX)
ADD_CONSTANT(float4, fog, 3, I_FOG, C_FOG)

ADD_CONSTANT(float4, posnormalmatrix, 6, I_POSNORMALMATRIX, C_POSNORMALMATRIX)
ADD_CONSTANT(float4, projection, 4, I_PROJECTION, C_PROJECTION)
ADD_CONSTANT(float4, materials, 4, I_MATERIALS, C_MATERIALS)
ADD_CONSTANT(float4, lights, 40, I_LIGHTS, C_LIGHTS)
ADD_CONSTANT(float4, texmatrices, 24, I_TEXMATRICES, C_TEXMATRICES)
ADD_CONSTANT(float4, transformmatrices, 64, I_TRANSFORMMATRICES, C_TRANSFORMMATRICES)
ADD_CONSTANT(float4, normalmatrices, 32, I_NORMALMATRICES, C_NORMALMATRICES)
ADD_CONSTANT(float4, posttransformmatrices, 64, I_POSTTRANSFORMMATRICES, C_POSTTRANSFORMMATRICES)
