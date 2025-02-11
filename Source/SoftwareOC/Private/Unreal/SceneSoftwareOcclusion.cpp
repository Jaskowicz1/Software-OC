// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneSoftwareOcclusion.cpp 
=============================================================================*/

#include "Unreal/SceneSoftwareOcclusion.h"
#include "EngineGlobals.h"
#include "CanvasTypes.h"
#include "SoftwareOCSubsystem.h"
#include "Async/TaskGraphInterfaces.h"
#include "Math/Vector.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "Unreal/FOcclusionFrameResults.h"

DECLARE_STATS_GROUP(TEXT("Software Occlusion"), STATGROUP_SoftwareOcclusion, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("(RT) Gather Time"),STAT_SoftwareOcclusionGather,STATGROUP_SoftwareOcclusion);
DECLARE_CYCLE_STAT(TEXT("(Task) Process Time"),STAT_SoftwareOcclusionProcess,STATGROUP_SoftwareOcclusion);
DECLARE_CYCLE_STAT(TEXT("(Task) Process Occluder Time"),STAT_SoftwareOcclusionProcessOccluder,STATGROUP_SoftwareOcclusion);
DECLARE_CYCLE_STAT(TEXT("(Task) Process Occludee Time"),STAT_SoftwareOcclusionProcessOccludee,STATGROUP_SoftwareOcclusion);
DECLARE_CYCLE_STAT(TEXT("(Task) Sort Time"),STAT_SoftwareOcclusionSort,STATGROUP_SoftwareOcclusion);
DECLARE_CYCLE_STAT(TEXT("(Task) Rasterize Time"),STAT_SoftwareOcclusionRasterize,STATGROUP_SoftwareOcclusion);

DECLARE_DWORD_COUNTER_STAT(TEXT("Culled"),STAT_SoftwareCulledPrimitives,STATGROUP_SoftwareOcclusion);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total occluders"),STAT_SoftwareOccluders,STATGROUP_SoftwareOcclusion);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total occludees"),STAT_SoftwareOccludees,STATGROUP_SoftwareOcclusion);
DECLARE_DWORD_COUNTER_STAT(TEXT("Total triangles"),STAT_SoftwareTriangles,STATGROUP_SoftwareOcclusion);
DECLARE_DWORD_COUNTER_STAT(TEXT("Rasterized occluder tris"),STAT_SoftwareOccluderTris,STATGROUP_SoftwareOcclusion);
DECLARE_DWORD_COUNTER_STAT(TEXT("Rasterized occludee tris"),STAT_SoftwareOccludeeTris,STATGROUP_SoftwareOcclusion);

float GSOMinScreenRadiusForOccluder = 0.075f;
static FAutoConsoleVariableRef CVarSOMinScreenRadiusForOccluder(
	TEXT("r.so.MinScreenRadiusForOccluder"),
	GSOMinScreenRadiusForOccluder,
	TEXT("Threshold below which meshes will be culled from beeing an occluder."),
	ECVF_RenderThreadSafe
	);

float GSOMaxDistanceForOccluder = 20000.f;
static FAutoConsoleVariableRef CVarSOMaxDistanceForOccluder(
	TEXT("r.so.MaxDistanceForOccluder"),
	GSOMaxDistanceForOccluder,
	TEXT("Max radius where to look for occluders."),
	ECVF_RenderThreadSafe
	);

int32 GSOMaxOccluderNum = 150;
static FAutoConsoleVariableRef CVarSOMaxOccluderNum(
	TEXT("r.so.MaxOccluderNum"),
	GSOMaxOccluderNum,
	TEXT("Maximum number of primitives that can be rendered as occluders"),
	ECVF_RenderThreadSafe
	);

static int32 GSOSIMD = 1;
static FAutoConsoleVariableRef CVarSOSIMD(
	TEXT("r.so.SIMD"),
	GSOSIMD,
	TEXT("Use SIMD routines in software occlusion"),
	ECVF_RenderThreadSafe
	);

static int32 GSOVisualizeBuffer = 0;
static FAutoConsoleVariableRef CVarSOVisualizeBuffer(
	TEXT("r.so.VisualizeBuffer"),
	GSOVisualizeBuffer,
	TEXT("Visualize rasterized occlusion buffer"),
	ECVF_RenderThreadSafe
	);



namespace EScreenVertexFlags
{
	const uint8 None = 0;
	const uint8 ClippedLeft		= 1 << 0;	// Vertex is clipped by left plane
	const uint8 ClippedRight	= 1 << 1;	// Vertex is clipped by right plane
	const uint8 ClippedTop		= 1 << 2;	// Vertex is clipped by top plane
	const uint8 ClippedBottom	= 1 << 3;	// Vertex is clipped by bottom plane
	const uint8 ClippedNear		= 1 << 4;   // Vertex is clipped by near plane
	const uint8 Discard			= 1 << 5;	// Polygon using this vertex should be discarded
}

struct FScreenPosition
{
	int32 X, Y;
};

struct FScreenTriangle
{
	FScreenPosition V[3];
};

struct FSortedIndexDepth
{
	int32 Index;
	float Depth;
};

struct FOcclusionSceneData
{
	FMatrix											ViewProj;
	TArray<FVector>									OccludeeBoxMinMax;
	TArray<FPrimitiveComponentId>					OccludeeBoxPrimId;
	TArray<FOcclusionMeshData>						OccluderData;
	int32											NumOccluderTriangles;
};

struct FOcclusionFrameData
{
	// binned tris
	TArray<FSortedIndexDepth>		SortedTriangles[BIN_NUM];
	
	// tris data	
	TArray<FScreenTriangle>			ScreenTriangles;
	TArray<FPrimitiveComponentId>	ScreenTrianglesPrimID;
	TArray<uint8>					ScreenTrianglesFlags;

	void ReserveBuffers(int32 NumTriangles)
	{
		const int32 NumTrianglesPerBin = NumTriangles/BIN_NUM + 1;
		for (int32 BinIdx = 0; BinIdx < BIN_NUM; ++BinIdx)
		{
			SortedTriangles[BinIdx].Reserve(NumTrianglesPerBin);
		}
				
		ScreenTriangles.Reserve(NumTriangles);
		ScreenTrianglesPrimID.Reserve(NumTriangles);
		ScreenTrianglesFlags.Reserve(NumTriangles);
	}
};

inline uint64 ComputeBinRowMask(int32 BinMinX, float fX0, float fX1)
{
	int32 X0 = FMath::RoundToInt(fX0) - BinMinX;
	int32 X1 = FMath::RoundToInt(fX1) - BinMinX;
	if (X0 >= BIN_WIDTH || X1 < 0)
	{
		// not in bin
		return 0ull;
	}
	else
	{
		X0 = FMath::Max(0, X0);
		X1 = FMath::Min(BIN_WIDTH-1, X1);
		int32 Num = (X1 - X0) + 1;
		return (Num == BIN_WIDTH) ? ~0ull : ((1ull << Num) - 1) << X0;
	}
}

inline void RasterizeHalf(float X0, float X1, float DX0, float DX1, int32 Row0, int32 Row1, uint64* BinData, int32 BinMinX)
{
	checkSlow(Row0 <= Row1);
	checkSlow(Row0 >= 0 && Row1 < FRAMEBUFFER_HEIGHT);
	
	for (int32 Row = Row0; Row <= Row1; Row++, X0+=DX0, X1+=DX1)
	{
		uint64 FrameBufferMask = BinData[Row];
		if (FrameBufferMask != ~0ull) // whether this row is already fully rasterized
		{
			uint64 RowMask = ComputeBinRowMask(BinMinX, X0, X1);
			if (RowMask)
			{
				BinData[Row] = (FrameBufferMask | RowMask);
			}
		}
	}
}

inline void RasterizeOccluderTri(const FScreenTriangle& Tri, uint64* BinData, int32 BinMinX)
{
	FScreenPosition A = Tri.V[0];
	FScreenPosition B = Tri.V[1];
	FScreenPosition C = Tri.V[2];

	int32 RowMin = FMath::Max<int32>(A.Y, 0);
	int32 RowMax = FMath::Min<int32>(FRAMEBUFFER_HEIGHT-1, C.Y);

	bool bRasterized = false;

	int32 RowS = RowMin;
	if ((B.Y - RowMin) > 0)
	{
		// A -> B
		int32 RowE = FMath::Min<int32>(RowMax, B.Y);
		// Edge gradients
		float dX0 = float(B.X - A.X)/(B.Y - A.Y);
		float dX1 = float(C.X - A.X)/(C.Y - A.Y);
		if (dX0 > dX1)
		{
			Swap(dX0, dX1);
		}
		float X0 = A.X + dX0*(RowS - A.Y);
		float X1 = A.X + dX1*(RowS - A.Y);
		ensure(X0 <= X1);
		RasterizeHalf(X0, X1, dX0, dX1, RowS, RowE, BinData, BinMinX);
		bRasterized|= true;
		RowS = RowE + 1;
	}
		
	if ((RowMax - RowS) > 0)
	{
		// B -> C
		// Edge gradients
		float dX0 = float(C.X - A.X)/(C.Y - A.Y);
		float dX1 = float(C.X - B.X)/(C.Y - B.Y);
		float X0 = A.X + dX0*(RowS - A.Y);
		float X1 = B.X + dX1*(RowS - B.Y);
		if (X0 > X1) 
		{
			Swap(X0, X1);
			Swap(dX0, dX1);
		}
		RasterizeHalf(X0, X1, dX0, dX1, RowS, RowMax, BinData, BinMinX);
		bRasterized|= true;
	}

	// one line triangle
	if (!bRasterized)
	{
		float X0 = FMath::Min3(A.X, B.X, C.X);
		float X1 = FMath::Max3(A.X, B.X, C.X);
		RasterizeHalf(X0, X1, 0.0f, 0.0f, RowS, RowS, BinData, BinMinX);
	}
}

bool RasterizeOccludeeQuad(const FScreenTriangle& Tri, uint64* BinData, int32 BinMinX)
{
	int32 RowMin = Tri.V[0].Y; // Quad MinY
	int32 RowMax = Tri.V[2].Y; // Quad MaxY
	// occludee expected to be clipped to screen
	checkSlow(RowMin >= 0);
	checkSlow(RowMax < FRAMEBUFFER_HEIGHT);

	// clip X to bin bounds
	int32 X0 =  FMath::Max(Tri.V[0].X - BinMinX, 0);
	int32 X1 =  FMath::Min(Tri.V[1].X - BinMinX, BIN_WIDTH - 1);
	checkSlow(X0 <= X1);
	
	int32 NumBits = (X1 - X0) + 1;
	uint64 RowMask = (NumBits == BIN_WIDTH) ? ~0ull : ((1ull << NumBits) - 1) << X0;

	for (int32 Row = RowMin; Row <= RowMax; ++Row)
	{
		uint64 FrameBufferMask = BinData[Row];
		if ((~FrameBufferMask & RowMask))
		{
			return true;
		}
	}
	
	return false;
}

inline bool TestFrontface(const FScreenTriangle& Tri)
{
	if ((Tri.V[2].X - Tri.V[0].X) * (Tri.V[1].Y - Tri.V[0].Y) >= (Tri.V[2].Y - Tri.V[0].Y) * (Tri.V[1].X - Tri.V[0].X))
	{
		return false; 
	}
	return true;
}

inline bool AddTriangle(FScreenTriangle& Tri, float TriDepth, FPrimitiveComponentId PrimitiveId, uint8 MeshFlags, FOcclusionFrameData& InData)
{
	if (MeshFlags == 1) // occluder tri
	{
		// Sort vertices by Y, assumed in rasterization
		if (Tri.V[0].Y > Tri.V[1].Y) Swap(Tri.V[0], Tri.V[1]);
		if (Tri.V[1].Y > Tri.V[2].Y) Swap(Tri.V[1], Tri.V[2]);
		if (Tri.V[0].Y > Tri.V[1].Y) Swap(Tri.V[0], Tri.V[1]);
	
		if (Tri.V[0].Y >= FRAMEBUFFER_HEIGHT || Tri.V[2].Y < 0)
		{
			return false;
		}
	}

	const int32 TriangleID = InData.ScreenTriangles.Add(Tri);
	InData.ScreenTrianglesPrimID.Add(PrimitiveId);
	InData.ScreenTrianglesFlags.Add(MeshFlags);
	
	// bin
	const int32 MinX = FMath::Min3(Tri.V[0].X, Tri.V[1].X, Tri.V[2].X) / BIN_WIDTH; 
	const int32 MaxX = FMath::Max3(Tri.V[0].X, Tri.V[1].X, Tri.V[2].X) / BIN_WIDTH;
	const int32 BinMin = FMath::Max(MinX, 0);
	const int32 BinMax = FMath::Min(MaxX, BIN_NUM-1);
	
	FSortedIndexDepth SortedIndexDepth;
	SortedIndexDepth.Index = TriangleID;
	SortedIndexDepth.Depth = TriDepth;
			
	for (int32 BinIdx = BinMin; BinIdx <= BinMax; ++BinIdx)
	{
		InData.SortedTriangles[BinIdx].Add(SortedIndexDepth);
	}

	return true;
}

static const VectorRegister vFramebufferBounds = MakeVectorRegister(FRAMEBUFFER_WIDTH-1, FRAMEBUFFER_HEIGHT-1, 1.0f, 1.0f);
static const VectorRegister vXYHalf = MakeVectorRegister(0.5f, 0.5f, 0.0f, 0.0f);

// BEGIN Intel
static const int32 NUM_CUBE_VTX = 8;
// 0 = min corner, 1 = max corner
static const uint32 sBBxInd[NUM_CUBE_VTX] = { 1, 0, 0, 1, 1, 1, 0, 0 };
static const uint32 sBByInd[NUM_CUBE_VTX] = { 1, 1, 1, 1, 0, 0, 0, 0 };
static const uint32 sBBzInd[NUM_CUBE_VTX] = { 1, 1, 0, 0, 0, 1, 1, 0 };
// END Intel

static void ProcessOccludeeGeomSIMD(const FMatrix& InMat, const FVector* InMinMax, int32 Num, int32* RESTRICT OutQuads, float* RESTRICT OutQuadDepth, int32* RESTRICT OutQuadClipped)
{
	const float W_CLIP = InMat.M[3][2];
	VectorRegister vClippingW = VectorLoadFloat1(&W_CLIP);
	VectorRegister mRow0  = VectorLoadAligned(InMat.M[0]);
	VectorRegister mRow1  = VectorLoadAligned(InMat.M[1]);
	VectorRegister mRow2  = VectorLoadAligned(InMat.M[2]);
	VectorRegister mRow3  = VectorLoadAligned(InMat.M[3]);
	VectorRegister xRow[2], yRow[2], zRow[2];
	
	for (int32 k = 0; k < Num; ++k)
	{
		FVector BoxMin = *(InMinMax++);
		FVector BoxMax = *(InMinMax++);

// BEGIN Intel
		// Project primitive bounding box to screen
		xRow[0] = VectorMultiply(VectorLoadFloat1(&BoxMin.X), mRow0);
		xRow[1] = VectorMultiply(VectorLoadFloat1(&BoxMax.X), mRow0);
		yRow[0] = VectorMultiply(VectorLoadFloat1(&BoxMin.Y), mRow1);
		yRow[1] = VectorMultiply(VectorLoadFloat1(&BoxMax.Y), mRow1);
		zRow[0] = VectorMultiply(VectorLoadFloat1(&BoxMin.Z), mRow2);
		zRow[1] = VectorMultiply(VectorLoadFloat1(&BoxMax.Z), mRow2);
				
		VectorRegister vClippedFlag = VectorZero();	
		VectorRegister vScreenMin = GlobalVectorConstants::BigNumber;
		VectorRegister vScreenMax = VectorNegate(vScreenMin);
			
		for (int32 i = 0; i < NUM_CUBE_VTX; ++i)
		{
			VectorRegister V;
			V = VectorAdd(mRow3,	xRow[sBBxInd[i]]);
			V = VectorAdd(V,		yRow[sBByInd[i]]);
			V = VectorAdd(V,		zRow[sBBzInd[i]]);
		
			VectorRegister W = VectorReplicate(V, 3);
			vClippedFlag = VectorBitwiseOr(vClippedFlag, VectorCompareLT(W, vClippingW));
			V = VectorDivide(V, W);
				
			vScreenMin = VectorMin(vScreenMin, V);
			vScreenMax = VectorMax(vScreenMax, V);
		}
// END Intel

		// For pixel snapping
		vScreenMin = VectorAdd(vScreenMin, vXYHalf);
		vScreenMax = VectorAdd(vScreenMax, vXYHalf); 
	
		// Clip against screen rect
		vScreenMin = VectorMax(vScreenMin, VectorZero());
		vScreenMax = VectorMin(vScreenMax, vFramebufferBounds); // Z should be unaffected
		
		// Make: MinX, MinY, MaxX, MaxY
		VectorRegisterInt IntMinMax = VectorFloatToInt(VectorCombineLow(vScreenMin, vScreenMax));
			
		// Store
		VectorIntStoreAligned(IntMinMax, OutQuads);
		VectorStoreFloat1(vClippedFlag, OutQuadClipped);
		*OutQuadDepth = VectorGetComponent(vScreenMax, 2);
		
		OutQuads+=4;
		OutQuadDepth++;
		OutQuadClipped++;
	}
}

static void ProcessOccludeeGeomScalar(const FMatrix& InMat, const FVector* InMinMax, int32 Num, int32* RESTRICT OutQuads, float* RESTRICT OutQuadDepth, int32* RESTRICT OutQuadClipped)
{
	const float W_CLIP =  InMat.M[3][2];
	FVector4 AX = FVector4(InMat.M[0][0], InMat.M[0][1], InMat.M[0][2], InMat.M[0][3]);
	FVector4 AY = FVector4(InMat.M[1][0], InMat.M[1][1], InMat.M[1][2], InMat.M[1][3]);
	FVector4 AZ = FVector4(InMat.M[2][0], InMat.M[2][1], InMat.M[2][2], InMat.M[2][3]);
	FVector4 AW = FVector4(InMat.M[3][0], InMat.M[3][1], InMat.M[3][2], InMat.M[3][3]);
	FVector4 xRow[2], yRow[2], zRow[2];

	for (int32 k = 0; k < Num; ++k)
	{
		FVector BoxMin = *(InMinMax++);
		FVector BoxMax = *(InMinMax++);
		// Project primitive bounding box to screen
		xRow[0] = FVector4(BoxMin.X, BoxMin.X, BoxMin.X, BoxMin.X) * AX;
		xRow[1] = FVector4(BoxMax.X, BoxMax.X, BoxMax.X, BoxMax.X) * AX;
		yRow[0] = FVector4(BoxMin.Y, BoxMin.Y, BoxMin.Y, BoxMin.Y) * AY;
		yRow[1] = FVector4(BoxMax.Y, BoxMax.Y, BoxMax.Y, BoxMax.Y) * AY;
		zRow[0] = FVector4(BoxMin.Z, BoxMin.Z, BoxMin.Z, BoxMin.Z) * AZ;
		zRow[1] = FVector4(BoxMax.Z, BoxMax.Z, BoxMax.Z, BoxMax.Z) * AZ;
			
		FVector2D MinXY = FVector2D(MAX_flt, MAX_flt);
		FVector2D MaxXY = FVector2D(-MAX_flt, -MAX_flt);
		float Depth = 0.f;
		bool bClippedNear = false;
	
		for (int32 i = 0; i < NUM_CUBE_VTX; i++)
		{
			FVector4 V = AW;
			V = V + xRow[sBBxInd[i]];
			V = V + yRow[sBByInd[i]];
			V = V + zRow[sBBzInd[i]];
			
			if (V.W < W_CLIP)
			{
				bClippedNear = true;
				break;
			}

			V = V / V.W;

			MinXY.X = FMath::Min(MinXY.X, V.X);
			MinXY.Y = FMath::Min(MinXY.Y, V.Y);
			MaxXY.X = FMath::Max(MaxXY.X, V.X);
			MaxXY.Y = FMath::Max(MaxXY.Y, V.Y);
			Depth = FMath::Max(Depth, V.Z);
		}

		if (bClippedNear)
		{
			OutQuadClipped[0] = 1;
		}
		else
		{
			// For pixel snapping
			MinXY = MinXY + FVector2D(0.5f, 0.5f);
			MaxXY = MaxXY + FVector2D(0.5f, 0.5f);

			// Clip against screen rect
			MinXY.X = FMath::Max(0.f, MinXY.X);
			MinXY.Y = FMath::Max(0.f, MinXY.Y);
			MaxXY.X = FMath::Min(FRAMEBUFFER_WIDTH-1.f, MaxXY.X);
			MaxXY.Y = FMath::Min(FRAMEBUFFER_HEIGHT-1.f, MaxXY.Y);

			// Make MinX, MinY, MaxX, MaxY
			OutQuads[0] = (int32)MinXY.X;
			OutQuads[1] = (int32)MinXY.Y;
			OutQuads[2] = (int32)MaxXY.X;
			OutQuads[3] = (int32)MaxXY.Y;
		
			OutQuadDepth[0] = Depth;
			OutQuadClipped[0] = 0;
		}
			
		OutQuads+=4;
		OutQuadDepth++;
		OutQuadClipped++;
	}
}

const FMatrix FramebufferMat(
			FVector(0.5f*(float)FRAMEBUFFER_WIDTH,  0.0f,							0.0f),
			FVector(0.0f,							0.5f*(float)FRAMEBUFFER_HEIGHT, 0.0f),
			FVector(0.0f,							0.0f,							1.0f),
			FVector(0.5f*(float)FRAMEBUFFER_WIDTH,	0.5f*(float)FRAMEBUFFER_HEIGHT, 0.0f)
		);

static bool ProcessOccludeeGeom(const FOcclusionSceneData& SceneData, FOcclusionFrameData& FrameData, TMap<FPrimitiveComponentId, bool>& VisibilityMap)
{
	const int32 RUN_SIZE = 512;
	const bool bUseSIMD = GSOSIMD != 0;
		
	int32 NumBoxes = SceneData.OccludeeBoxMinMax.Num()/2;
	const FVector* MinMax = SceneData.OccludeeBoxMinMax.GetData();
	const FPrimitiveComponentId* PrimIds = SceneData.OccludeeBoxPrimId.GetData();

	FMatrix WorldToFB = SceneData.ViewProj * FramebufferMat;
	
	// on stack mem for each run output
	MS_ALIGN(SIMD_ALIGNMENT) int32 Quads[RUN_SIZE*4] GCC_ALIGN(SIMD_ALIGNMENT);

	int32 NumRuns = NumBoxes/RUN_SIZE + 1;
	int32 NumBoxesProcessed = 0;
	
	for (int32 RunIdx = 0; RunIdx < NumRuns; ++RunIdx)
	{
		float QuadDepths[RUN_SIZE];
		int32 QuadClipFlags[RUN_SIZE];
		int32 RunSize = FMath::Min(NumBoxes - NumBoxesProcessed, RUN_SIZE);
		
		// Generate quads
		if (bUseSIMD)
		{
			ProcessOccludeeGeomSIMD(WorldToFB, MinMax, RunSize, Quads, QuadDepths, QuadClipFlags);
		}
		else
		{
			ProcessOccludeeGeomScalar(WorldToFB, MinMax, RunSize, Quads, QuadDepths, QuadClipFlags);
		}
							
		// Triangulate generated quads
		int32 QuadIdx = 0;
		for (int32 i = 0; i < RunSize; ++i)
		{
			int32 MinX = Quads[QuadIdx++];
			int32 MinY = Quads[QuadIdx++];
			int32 MaxX = Quads[QuadIdx++];
			int32 MaxY = Quads[QuadIdx++];
			
			FPrimitiveComponentId PrimitiveId = PrimIds[i];

			if (QuadClipFlags[i] != 0)
			{
				// clipped by near plane, visible
				VisibilityMap.FindOrAdd(PrimitiveId) = true;
				continue;
			}

			// Check MinX <= MaxX and MinY <= MaxY
			if (MinX > MaxX || MinY > MaxY)
			{
				// Do not rasterize if not on screen, occluded
				VisibilityMap.FindOrAdd(PrimitiveId) = false;
				continue;
			}
						
			float Depth = QuadDepths[i];
						
			// add only first tri, rasterizer will figure out to render a quad
			FScreenTriangle ST;
			ST.V[0] = {MinX, MinY};
			ST.V[1] = {MaxX, MaxY};
			ST.V[2] = {MinX, MaxY};
			AddTriangle(ST, Depth, PrimitiveId, 0, FrameData);
		}

		MinMax+= (RunSize*2);
		PrimIds+= RunSize;
		NumBoxesProcessed+= RunSize;
		
	} // for each run

	return true;
}

static void CollectOccludeeGeom(const FBoxSphereBounds& Bounds, FPrimitiveComponentId PrimitiveId, FOcclusionSceneData& SceneData)
{
	const FBox Box = Bounds.GetBox();

	SceneData.OccludeeBoxMinMax.Add(Box.Min);
	SceneData.OccludeeBoxMinMax.Add(Box.Max);
	SceneData.OccludeeBoxPrimId.Add(PrimitiveId);
}

static bool ClippedVertexToScreen(const FVector4& XFV, FScreenPosition& OutSP, float& OutDepth)
{
	checkSlow(XFV.W >= 0.f);

	FVector4 FSP = XFV / XFV.W;
	int32 X = FMath::RoundToInt((FSP.X + 1.f) * FRAMEBUFFER_WIDTH/2.0);
	int32 Y = FMath::RoundToInt((FSP.Y + 1.f) * FRAMEBUFFER_HEIGHT/2.0);
	
	OutSP.X = X;
	OutSP.Y = Y;
	OutDepth = FSP.Z;
	return false;
}

static uint8 ProcessXFormVertex(const FVector4& XFV, float W_CLIP)
{
	uint8 Flags = 0;
	float W = XFV.W;

	if (W < W_CLIP)
	{
		Flags|= EScreenVertexFlags::ClippedNear;
	}
		
	if (XFV.X < -W)
	{
		Flags|= EScreenVertexFlags::ClippedLeft;
	}
		
	if (XFV.X > W)
	{
		Flags|= EScreenVertexFlags::ClippedRight;
	}
		
	if (XFV.Y < -W)
	{
		Flags|= EScreenVertexFlags::ClippedTop;
	}
		
	if (XFV.Y > W)
	{
		Flags|= EScreenVertexFlags::ClippedBottom;
	}

	return Flags;
}

static constexpr int32 Edges[3][2] = {{0,1}, {1,2}, {2,0}};

static void ProcessOccluderGeom(const FOcclusionSceneData& SceneData, FOcclusionFrameData& OutData)
{
	const float W_CLIP = SceneData.ViewProj.M[3][2];

	const int32 NumMeshes = SceneData.OccluderData.Num();

	TArray<FVector4>	ClipVertexBuffer;
	TArray<uint8>		ClipVertexFlagsBuffer;
		
	for (int32 MeshIdx = 0; MeshIdx < NumMeshes; ++MeshIdx)
	{
		const FOcclusionMeshData& Mesh = SceneData.OccluderData[MeshIdx];
		int32 NumVtx = Mesh.VerticesSP.Num();
		
		ClipVertexBuffer.SetNumUninitialized(NumVtx, EAllowShrinking::Yes);
		ClipVertexFlagsBuffer.SetNumUninitialized(NumVtx, EAllowShrinking::Yes);

		const FVector3f* MeshVertices = Mesh.VerticesSP.GetData();
		FVector4* MeshClipVertices = ClipVertexBuffer.GetData();
		uint8*	MeshClipVertexFlags = ClipVertexFlagsBuffer.GetData();
		
		// Transform mesh to clip space
		{
			const FMatrix LocalToClip = Mesh.LocalToWorld * SceneData.ViewProj;
			VectorRegister mRow0  = VectorLoadAligned(LocalToClip.M[0]);
			VectorRegister mRow1  = VectorLoadAligned(LocalToClip.M[1]);
			VectorRegister mRow2  = VectorLoadAligned(LocalToClip.M[2]);
			VectorRegister mRow3  = VectorLoadAligned(LocalToClip.M[3]);
			
			for (int32 i = 0; i < NumVtx; ++i)
			{
				VectorRegister VTempX = VectorLoadFloat1(&MeshVertices[i].X);
				VectorRegister VTempY = VectorLoadFloat1(&MeshVertices[i].Y);
				VectorRegister VTempZ = VectorLoadFloat1(&MeshVertices[i].Z);
				VectorRegister VTempW;
				// Mul by the matrix
				VTempX = VectorMultiply(VTempX, mRow0);
				VTempY = VectorMultiply(VTempY, mRow1);
				VTempZ = VectorMultiply(VTempZ, mRow2);
				VTempW = VectorMultiply(GlobalVectorConstants::FloatOne, mRow3);
				// Add them all together
				VTempX = VectorAdd(VTempX, VTempY);
				VTempZ = VectorAdd(VTempZ, VTempW);
				VTempX = VectorAdd(VTempX, VTempZ);
				// Store
				VectorStoreAligned(VTempX, &MeshClipVertices[i]);
						
				uint8 VertexFlags = ProcessXFormVertex(MeshClipVertices[i], W_CLIP);
				MeshClipVertexFlags[i] = VertexFlags;
			}
		}
	
		const uint16* MeshIndices = Mesh.IndicesSP.GetData();
		int32 NumTris = Mesh.IndicesSP.Num()/3;

		// Create triangles
		for (int32 i = 0; i < NumTris; ++i)
		{
			uint16 I0 = MeshIndices[i*3 + 0];
			uint16 I1 = MeshIndices[i*3 + 1];
			uint16 I2 = MeshIndices[i*3 + 2];

			uint8 F0 = MeshClipVertexFlags[I0];
			uint8 F1 = MeshClipVertexFlags[I1];
			uint8 F2 = MeshClipVertexFlags[I2];

			if ((F0 & F1) & F2)
			{
				// fully clipped
				continue;
			}

			const uint16 V[3] = 
			{
				I0,
				I1,
				I2
			};

			if (uint8 TriFlags = F0 | F1 | F2; TriFlags & EScreenVertexFlags::ClippedNear)
			{
				FVector4 ClippedPos[4];
				int32 NumPos = 0;
			
				for(int32 EdgeIdx = 0; EdgeIdx < 3; EdgeIdx++)
				{
					int32 i0 = Edges[EdgeIdx][0];
					int32 i1 = Edges[EdgeIdx][1];

					if(!ClipVertexBuffer.IsValidIndex(V[i0]) || !ClipVertexBuffer.IsValidIndex(V[i1]))
					{
						continue;
					}

					bool dot0 = MeshClipVertices[V[i0]].W < W_CLIP;
					bool dot1 = MeshClipVertices[V[i1]].W < W_CLIP;
								
					if (!dot0)
					{
						ClippedPos[NumPos] = MeshClipVertices[V[i0]];
						NumPos++;
					}
				
					if (dot0 != dot1)
					{
						float t = (W_CLIP - MeshClipVertices[V[i0]].W) / (MeshClipVertices[V[i0]].W - MeshClipVertices[V[i1]].W);
						ClippedPos[NumPos] = MeshClipVertices[V[i0]] + t * (MeshClipVertices[V[i0]] - MeshClipVertices[V[i1]]);
						NumPos++;
					}
				}
			
				// triangulate clipped vertices
				for (int32 j = 2; j < NumPos; j++)
				{
					FScreenTriangle Tri;
					float Depths[3];
					bool bShouldDiscard = false;

					bShouldDiscard|= ClippedVertexToScreen(ClippedPos[0],	Tri.V[0], Depths[0]);
					bShouldDiscard|= ClippedVertexToScreen(ClippedPos[j-1],	Tri.V[1], Depths[1]);
					bShouldDiscard|= ClippedVertexToScreen(ClippedPos[j],	Tri.V[2], Depths[2]);
								
					if (!bShouldDiscard && TestFrontface(Tri))
					{
						// Min tri depth for occluder (further from screen)
						float TriDepth = FMath::Min3(Depths[0], Depths[1], Depths[2]);
						AddTriangle(Tri, TriDepth, Mesh.PrimId, 1, OutData);
					}
				}
			}
			else
			{
				FScreenTriangle Tri;
				float Depths[3];
				bool bShouldDiscard = false;
						
				for (int32 j = 0; j < 3 && !bShouldDiscard; ++j)
				{
					if (ClipVertexBuffer.IsValidIndex(V[j]))
					{
						bShouldDiscard|= ClippedVertexToScreen(MeshClipVertices[V[j]], Tri.V[j], Depths[j]);
					} else
					{
						bShouldDiscard |= true;
					}
				}
			
				if (!bShouldDiscard && TestFrontface(Tri))
				{
					// Min tri depth for occluder (further from screen)
					float TriDepth = FMath::Min3(Depths[0], Depths[1], Depths[2]);
					AddTriangle(Tri, TriDepth, Mesh.PrimId, /*MeshFlags*/ 1, OutData);
				}
			}
		} // for each triangle
	}// for each mesh
}

class FSWOccluderElementsCollector : public FOccluderElementsCollector
{
public:
	FSWOccluderElementsCollector(FOcclusionSceneData& InData)
		: SceneData(InData)
	{
		SceneData.NumOccluderTriangles = 0;
	}
	
	void SetPrimitiveID(FPrimitiveComponentId PrimitiveId)
	{
		CurrentPrimitiveId = PrimitiveId;
	}

	virtual void AddElements(const TArray<FVector3f>& Vertices, const TArray<uint16>& Indices, const FMatrix& LocalToWorld) override 
	{
		SceneData.OccluderData.AddDefaulted();
		FOcclusionMeshData& MeshData = SceneData.OccluderData.Last();

		MeshData.PrimId = CurrentPrimitiveId;
		MeshData.LocalToWorld = LocalToWorld;
		MeshData.VerticesSP = Vertices;
		MeshData.IndicesSP = Indices;

		SceneData.NumOccluderTriangles+= Indices.Num()/3;
	}

public:
	FOcclusionSceneData& SceneData;
	FPrimitiveComponentId CurrentPrimitiveId;
};

static void ProcessOcclusionFrame(const FOcclusionSceneData& InSceneData, FOcclusionFrameResults& OutResults)
{
	FOcclusionFrameData FrameData;
	int32 NumExpectedTriangles = InSceneData.NumOccluderTriangles + InSceneData.OccludeeBoxPrimId.Num(); // one triangle for each occludee
	FrameData.ReserveBuffers(NumExpectedTriangles);
		
	{
		SCOPE_CYCLE_COUNTER(STAT_SoftwareOcclusionProcessOccluder)
		ProcessOccluderGeom(InSceneData, FrameData);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_SoftwareOcclusionProcessOccludee)
		// Generate screen quads from all collected occludee bboxes
		ProcessOccludeeGeom(InSceneData, FrameData, OutResults.VisibilityMap);
	}

	int32 NumRasterizedOccluderTris = 0;
	int32 NumRasterizedOccludeeTris = 0;
	
	{
		SCOPE_CYCLE_COUNTER(STAT_SoftwareOcclusionRasterize);

		const uint8* MeshFlags = FrameData.ScreenTrianglesFlags.GetData();
		const FPrimitiveComponentId* PrimitiveIds = FrameData.ScreenTrianglesPrimID.GetData();
		const FScreenTriangle* Tris = FrameData.ScreenTriangles.GetData();
						
		for (int32 BinIdx = 0; BinIdx < BIN_NUM; ++BinIdx)
		{
			// Sort triangles in the bin by depth
			FrameData.SortedTriangles[BinIdx].Sort([](const FSortedIndexDepth& A, const FSortedIndexDepth& B) { 
				// biggerZ (closer) first 
				return A.Depth > B.Depth;
			});

			const FSortedIndexDepth* SortedTriIndices = FrameData.SortedTriangles[BinIdx].GetData();
			const int32 NumTris = FrameData.SortedTriangles[BinIdx].Num();
			const int32 BinMinX = BinIdx*BIN_WIDTH;
			FFramebufferBin& Bin = OutResults.Bins[BinIdx];
			// TODO: add a way to check when bin is already fully rasterized, so we can skip this work
						
			for (int32 TriIdx = 0; TriIdx < NumTris; ++TriIdx)
			{
				int32 TriID = SortedTriIndices[TriIdx].Index;
				uint8 Flags = MeshFlags[TriID];
				FPrimitiveComponentId PrimitiveId = PrimitiveIds[TriID];
				const FScreenTriangle& Tri = Tris[TriID];

				if (Flags != 0)
				{
					// rasterize occluder
					RasterizeOccluderTri(Tri, Bin.Data, BinMinX);
					NumRasterizedOccluderTris++;
				}
				else
				{
					// rasterize occludee
					bool& VisBit = OutResults.VisibilityMap.FindOrAdd(PrimitiveId);
					const bool bVisible = RasterizeOccludeeQuad(Tri, Bin.Data, BinMinX);
					VisBit |= bVisible;
					NumRasterizedOccludeeTris++;
				}
			}
		}
	}
	
	int32 NumTotalTris = FrameData.ScreenTriangles.Num();
	INC_DWORD_STAT_BY(STAT_SoftwareTriangles, NumTotalTris);
	INC_DWORD_STAT_BY(STAT_SoftwareOccluderTris, NumRasterizedOccluderTris);
	INC_DWORD_STAT_BY(STAT_SoftwareOccludeeTris, NumRasterizedOccludeeTris);
}

FSceneSoftwareOcclusion::FSceneSoftwareOcclusion()
{
}

FSceneSoftwareOcclusion::~FSceneSoftwareOcclusion()
{
	if (Processing)
	{
		Processing.Reset();
	}
	
	if (Available)
	{
		if(TaskRef)
		{
			TaskRef.SafeRelease();
		}
		
		Available.Reset();
	}
}

static int32 ApplyResults(const FScene* Scene, FViewInfo& View, const FOcclusionFrameResults& Results)
{
	int32 NumOccluded = 0;
	
	for (FSceneSetBitIterator BitIt(View.PrimitiveVisibilityMap); BitIt; ++BitIt)
	{
		uint32 PrimitiveIndex = BitIt.GetIndex();
		FPrimitiveComponentId PrimId = Scene->PrimitiveComponentIds[PrimitiveIndex];
			
		const bool* bVisiblePtr = Results.VisibilityMap.Find(PrimId);
		if (bVisiblePtr)
		{
			if (*bVisiblePtr == false)
			{
				View.PrimitiveVisibilityMap[PrimitiveIndex] = false;
				View.PrimitiveDefinitelyUnoccludedMap[PrimitiveIndex] = false;
				NumOccluded++;
			}
#if defined(ENGINE_MINOR_VERSION) && ENGINE_MINOR_VERSION >= 5
			else
			{
				View.PrimitiveDefinitelyUnoccludedMap[PrimitiveIndex] = true;
			}
		}
		else
		{
			// This applies for those are not occludees, so just follow the frustum culling result.
			View.PrimitiveDefinitelyUnoccludedMap[PrimitiveIndex] = View.PrimitiveVisibilityMap[PrimitiveIndex];
#endif
		}
	}

	INC_DWORD_STAT_BY(STAT_SoftwareCulledPrimitives, NumOccluded);
	
	return NumOccluded;
}

/*
static int32 GSOThreadName = 2;
static FAutoConsoleVariableRef CVarSOThreadName(
	TEXT("r.so.ThreadName"),
	GSOThreadName,
	TEXT("0 = AnyHiPriThreadNormalTask")
	TEXT("1 = AnyHiPriThreadHiPriTask")
	TEXT("2 = AnyNormalThreadNormalTask (Default)")
	TEXT("3 = AnyNormalThreadHiPriTask")
	TEXT("4 = AnyBackgroundThreadNormalTask")
	TEXT("5 = AnyBackgroundHiPriTask"),
	ECVF_RenderThreadSafe
	);

static const ENamedThreads::Type ThreadNameMap[] = 
{
	ENamedThreads::AnyHiPriThreadNormalTask,
	ENamedThreads::AnyHiPriThreadHiPriTask,
	ENamedThreads::AnyNormalThreadNormalTask,
	ENamedThreads::AnyNormalThreadHiPriTask,
	ENamedThreads::AnyBackgroundThreadNormalTask,
	ENamedThreads::AnyBackgroundHiPriTask,
};
*/

static ENamedThreads::Type GetOcclusionThreadName()
{
	// Scrap below. There's a chance it can just run on GameThead and that's a no-go.
	//int32 Index = FMath::Clamp<int32>(GSOThreadName, 0, UE_ARRAY_COUNT(ThreadNameMap)-1);
	//return ThreadNameMap[Index];

	// We want to ensure we return a high task priority thread.
	// TODO: Make a dedicated thread for this.
	return ENamedThreads::Type::HighTaskPriority;
}

const static float OCCLUDER_DISTANCE_WEIGHT = 15000.f; // Originally 10000.
static float ComputePotentialOccluderWeight(float ScreenSize, float DistanceSquared)
{
	return ScreenSize + OCCLUDER_DISTANCE_WEIGHT/DistanceSquared;
}

FGraphEventRef FSceneSoftwareOcclusion::SubmitScene(const FScene* Scene, const FViewInfo& View,
	FOcclusionFrameResults* Results)
{
	int32 NumCollectedOccluders = 0;
	int32 NumCollectedOccludees = 0;
	
	if(!Scene->World || !IsValid(Scene->World) || Scene->World->IsBeingCleanedUp() || Scene->World->HasAnyFlags(RF_MirroredGarbage) || Scene->World->HasAnyFlags(RF_BeginDestroyed))
	{
		return FGraphEventRef();
	}
			
	const FMatrix ViewProjMat = View.ViewMatrices.GetViewMatrix() * View.ViewMatrices.GetProjectionNoAAMatrix();
	const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();
	const float MaxDistanceSquared = FMath::Square(GSOMaxDistanceForOccluder);
	
	// Allocate occlusion scene
	TUniquePtr<FOcclusionSceneData> SceneData = MakeUnique<FOcclusionSceneData>();
	SceneData->ViewProj = ViewProjMat;

	const int32 NumReserveOccludee = 1024;
	SceneData->OccludeeBoxPrimId.Reserve(NumReserveOccludee);
	SceneData->OccludeeBoxMinMax.Reserve(NumReserveOccludee*2);
	SceneData->OccluderData.Reserve(GSOMaxOccluderNum);
			
	// Collect scene geometry: occluders, occludees
	{
		SCOPE_CYCLE_COUNTER(STAT_SoftwareOcclusionGather);
				
		FSWOccluderElementsCollector Collector(*SceneData);
		
		TArray<FPotentialOccluderPrimitive> PotentialOccluders;
		PotentialOccluders.Reserve(GSOMaxOccluderNum);
		
		for (FSceneSetBitIterator BitIt(View.PrimitiveVisibilityMap); BitIt; ++BitIt)
		{
			uint32 PrimitiveIndex = BitIt.GetIndex();
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];
			const FBoxSphereBounds& Bounds = Scene->PrimitiveOcclusionBounds[PrimitiveIndex];
			const uint8 OcclusionFlags = Scene->PrimitiveOcclusionFlags[PrimitiveIndex];
			const FPrimitiveComponentId PrimitiveComponentId = PrimitiveSceneInfo->PrimitiveComponentId;
			FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;

			// Ignore random components that shouldn't be occluded.
			if(Proxy->GetMeshDrawCommandStatsCategory() == "LineBatchComponent")
			{
				continue;
			}
			
			const bool bHasHugeBounds = Bounds.SphereRadius > HALF_WORLD_MAX/2.0f; // big objects like skybox
            float DistanceSquared = 0.f;
            float ScreenSize = 0.f;
        
            // Find out whether primitive can/should be occluder or occludee
            bool bCanBeOccluder = !bHasHugeBounds && Proxy->ShouldUseAsOccluder();
            if (bCanBeOccluder)
            {
            	// Size/distance requirements
            	DistanceSquared = FMath::Max(OCCLUDER_DISTANCE_WEIGHT, (Bounds.Origin - ViewOrigin).SizeSquared() - FMath::Square(Bounds.SphereRadius));
            	if (DistanceSquared < MaxDistanceSquared)
            	{
            		ScreenSize = ComputeBoundsScreenSize(Bounds.Origin, Bounds.SphereRadius, View);
            	}

            	bCanBeOccluder = GSOMinScreenRadiusForOccluder < ScreenSize;
            }
        
            if (bCanBeOccluder)
            {
            	FPotentialOccluderPrimitive PotentialOccluder{};

            	PotentialOccluder.PrimitiveSceneInfo = PrimitiveSceneInfo;

            	if (OcSubsystem && OcSubsystem->IDToMeshComp.Contains(PrimitiveComponentId.PrimIDValue))
            	{
		            if(auto StaticMeshComponent = Cast<UStaticMeshComponent>(*OcSubsystem->IDToMeshComp.Find(PrimitiveComponentId.PrimIDValue)))
            		{
            			PotentialOccluder.OccluderData = FOcclusionMeshData(StaticMeshComponent->GetStaticMesh());
            		}
            	}

            	PotentialOccluder.Weight = ComputePotentialOccluderWeight(ScreenSize, DistanceSquared);

            	PotentialOccluders.Add(PotentialOccluder);
            }
        
            bool bCanBeOccludee = !bHasHugeBounds && Proxy->CanBeOccluded() && (OcclusionFlags & EOcclusionFlags::CanBeOccluded) != 0;
            if (bCanBeOccludee)
            {
            	// Collect occludee bbox
            	CollectOccludeeGeom(Bounds, PrimitiveComponentId, *SceneData);
            	NumCollectedOccludees++;
            }
		}

		// We need to check the cached visibility map.
		// We do a check for OCSubsystem as it's null for the first frame (possibly 2nd too).
		if (OcSubsystem && !OcSubsystem->CachedVisibilityMap.IsEmpty())
		{
			for (auto& Tuple : OcSubsystem->CachedVisibilityMap)
			{
				if (Tuple.Value)
				{
					continue;
				}
				
				FPrimitiveComponentId PrimitiveComponentId = Tuple.Key;
				if (!OcSubsystem->IDToMeshComp.Contains(PrimitiveComponentId.PrimIDValue))
				{
					continue;
				}
				UMeshComponent* MeshComponent = *OcSubsystem->IDToMeshComp.Find(PrimitiveComponentId.PrimIDValue);

				if(!MeshComponent || !IsValid(MeshComponent) ||
					MeshComponent->HasAnyFlags(RF_MirroredGarbage) ||
					MeshComponent->HasAnyFlags(RF_BeginDestroyed))
				{
					continue;
				}
				
				const FBoxSphereBounds& Bounds = MeshComponent->Bounds;

				// Don't need to check flags nor if it has huge bounds here,
				// wouldn't be in cache if it wasn't allowed to be an occluded object.
				
				// Collect occludee bbox
				CollectOccludeeGeom(Bounds, PrimitiveComponentId, *SceneData);
				NumCollectedOccludees++;
			}
		}

		// Sort potential occluders by weight
		PotentialOccluders.Sort([&](const FPotentialOccluderPrimitive& A, const FPotentialOccluderPrimitive& B) { 
			return A.Weight > B.Weight; 
		});
		
		// Add sorted occluders to scene up to GSOMaxOccluderNum
		for (const FPotentialOccluderPrimitive& PotentialOccluder : PotentialOccluders)
		{
			const FPrimitiveComponentId PrimitiveComponentId = PotentialOccluder.PrimitiveSceneInfo->PrimitiveComponentId;
			FPrimitiveSceneProxy* Proxy = PotentialOccluder.PrimitiveSceneInfo->Proxy;

			// Relevance requirements
			FPrimitiveViewRelevance ViewRelevance = Proxy->GetViewRelevance(&View);
			const bool bNonOpaqueRelevance = (ViewRelevance.bMasked || ViewRelevance.HasTranslucency()); // TODO: opaque sections
			bool bCanBeOccluder = ViewRelevance.bDrawRelevance && (ViewRelevance.bOpaque && !bNonOpaqueRelevance);

			if (bCanBeOccluder)
			{
				Collector.SetPrimitiveID(PrimitiveComponentId);
				// Collect occluder geometry
				NumCollectedOccluders += CollectOccluderElements(Collector, Proxy, PotentialOccluder);
			}

			if (NumCollectedOccluders >= GSOMaxOccluderNum)
			{
				break;
			}
		}
	}

	INC_DWORD_STAT_BY(STAT_SoftwareOccluders, NumCollectedOccluders);
	INC_DWORD_STAT_BY(STAT_SoftwareOccludees, NumCollectedOccludees);
	
	// reserve space for occludees vis flags 
	Results->VisibilityMap.Reserve(NumCollectedOccludees);
	
	// Submit occlusion task
	FOcclusionSceneData* SceneDataParam = SceneData.Release();
	return FFunctionGraphTask::CreateAndDispatchWhenReady([SceneDataParam, Results]()
	{
		ProcessOcclusionFrame(*SceneDataParam, *Results);
		delete SceneDataParam;
	}, GET_STATID(STAT_SoftwareOcclusionProcess), nullptr, GetOcclusionThreadName());
}

int32 FSceneSoftwareOcclusion::Process(const FScene* Scene, FViewInfo& View)
{
	// Make sure occlusion task issued last frame is completed
	FlushResults();

	if(Available.IsValid())
	{
		Available.Reset(nullptr);
	}

	// Finished processing occlusion, set results as available
	Available = MoveTemp(Processing);

	// Submit occlusion scene for next frame
	Processing = MakeUnique<FOcclusionFrameResults>();

	// Ensure we aren't about to run with the world shutting down.
	if(!Scene->World || !IsValid(Scene->World) || Scene->World->IsBeingCleanedUp() || Scene->World->HasAnyFlags(RF_MirroredGarbage) || Scene->World->HasAnyFlags(RF_BeginDestroyed))
	{
		return 0;
	}
	
	TaskRef = SubmitScene(Scene, View, Processing.Get());

	// Apply available occlusion results
	int32 NumCulled = 0;
	if (Available.IsValid())
	{
		NumCulled = ApplyResults(Scene, View, *Available);
	}
	
	return NumCulled;
}

void FSceneSoftwareOcclusion::FlushResults()
{
	if (TaskRef.IsValid())
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(TaskRef);
		TaskRef = nullptr;
	}
}

inline bool BinRowTestBit(uint64 Mask, int32 Bit)
{
	return (Mask & (1ull << Bit)) != 0;
}

void FSceneSoftwareOcclusion::DebugDraw(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassRenderTarget Output, int32 InX, int32 InY) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GSOVisualizeBuffer == 0)
	{
		return;
	}

	FOcclusionFrameResults* Results = Available.Get();
	if (Results == nullptr)
	{
		return;
	}

	if (!Output.IsValid())
	{
		return;
	}

	AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Canvas"), View, Output, [Results, InX, InY](FCanvas& Canvas)
	{
		FLinearColor ColorBuffer[2] =
		{
			FLinearColor(0.1f, 0.1f, 0.1f), // un-occluded
			FLinearColor::White // occluded
		};
		
		FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Line);

		for (int32 i = 0; i < BIN_NUM; ++i)
		{
			int32 BinStartX = InX + i * BIN_WIDTH;
			int32 BinStartY = InY;

			// vertical line for each bin border
			BatchedElements->AddLine(FVector(BinStartX, BinStartY, 0.f), FVector(BinStartX, BinStartY + FRAMEBUFFER_HEIGHT, 0.f), FColor::Blue, FHitProxyId());

			const FFramebufferBin& Bin = Results->Bins[i];
			for (int32 j = 0; j < FRAMEBUFFER_HEIGHT; ++j)
			{
				uint64 RowData = Bin.Data[j];
				int32 BitY = (FRAMEBUFFER_HEIGHT + InY) - j; // flip image by Y axis

				FVector Pos0 = FVector(BinStartX, BitY, 0.f);
				int32 Bit0 = BinRowTestBit(RowData, 0) ? 1 : 0;

				for (int32 k = 1; k < BIN_WIDTH; ++k)
				{
					int32 Bit1 = BinRowTestBit(RowData, k) ? 1 : 0;
					if (Bit0 != Bit1 || (k == (BIN_WIDTH - 1)))
					{
						int32 BitX = BinStartX + k;
						FVector Pos1 = FVector(BitX, BitY, 0.f);
						BatchedElements->AddLine(Pos0, Pos1, ColorBuffer[Bit0], FHitProxyId());
						Pos0 = Pos1;
						Bit0 = Bit1;
					}
				}
			}
		}

		// vertical line for last bin border
		int32 BinX = InX + BIN_NUM * BIN_WIDTH;
		int32 BinY = InY;
		BatchedElements->AddLine(FVector(BinX, BinY, 0.f), FVector(BinX, BinY + FRAMEBUFFER_HEIGHT, 0.f), FColor::Blue, FHitProxyId());
	});
#endif//!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

int32 FSceneSoftwareOcclusion::CollectOccluderElements(FOccluderElementsCollector& Collector, FPrimitiveSceneProxy* Proxy,
	const FPotentialOccluderPrimitive& PotentialOccluder)
{
	// InstancedStaticMeshes are missing their transforms per instance (WHY?!?!?!)
	// It's as if Unreal decided "no we won't keep cool optimisations in anymore!!"
	
	// TODO: Make ISMs and HISMs not have a huge occlusion box, it could potentially cause issues in some projects
	
	if (!PotentialOccluder.OccluderData.VerticesSP.IsEmpty() && !PotentialOccluder.OccluderData.IndicesSP.IsEmpty())
	{
		Collector.AddElements(PotentialOccluder.OccluderData.VerticesSP, PotentialOccluder.OccluderData.IndicesSP, Proxy->GetLocalToWorld());
		return 1;
	}
	return 0;
}
