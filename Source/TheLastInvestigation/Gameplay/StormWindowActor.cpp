#include "StormWindowActor.h"
#include "RoomBuildLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	const FLinearColor LightningColor(0.72f, 0.82f, 1.f); // cold blue-white

	/**
	 * One hanging drape, as a generated surface.
	 *
	 * The sheet is a graph x = f(y, z) in the pivot's space: y runs across the curtain towards the
	 * middle of the window (SignedWidth carries which way that is for this side), z runs down from
	 * the pole, and x is depth, with -x towards the room. Two sine waves of different period give
	 * the folds; their phase drifts with height, because a fold in real cloth does not fall in a
	 * straight line; and their amplitude opens towards the hem, because the top is gathered on the
	 * rings and the bottom is not.
	 *
	 * Where the cloth has gone is decided per column by Keep(), and quads with any corner outside
	 * it are never emitted. That is the whole reason for generating a mesh rather than stacking
	 * slabs: the hem becomes a contour that can wander, double back and thin to a thread, which is
	 * what rotted cloth does and what no arrangement of rectangles can be made to do.
	 */
	UProceduralMeshComponent* BuildDrapeMesh(AActor* Owner, USceneComponent* Pivot, UMaterialInterface* Cloth,
		float SignedWidth, float Length, int32 Seed)
	{
		if (!Owner || !Pivot)
		{
			return nullptr;
		}

		// A centimetre-ish step in both directions. The first pass was 57 by 81, which puts two and
		// a half centimetres between rows, and every torn edge in the drape came out as a visible
		// staircase: the whole point of generating the mesh is the outline, so the outline is the
		// thing that has to be finer than the eye.
		constexpr int32 Cols = 73;
		constexpr int32 Rows = 121;
		const float TexCm = 34.f;     // RoomSurfaces::Drapery's repeat, since the UVs are ours
		const float Thickness = 0.5f;

		FRandomStream Weave(Seed);

		// Where the hem has gone: three deep bites plus a fine fray. Bites rather than noise alone
		// because damage has to be in one place and absent from another — an evenly wandering edge
		// all the way along reads as a decorative deckle, not as rot.
		struct FBite { float Where; float Width; float Depth; };
		FBite Bites[3];
		for (FBite& Bite : Bites)
		{
			Bite.Where = Weave.FRandRange(0.1f, 0.94f);
			Bite.Width = Weave.FRandRange(0.06f, 0.16f);
			Bite.Depth = Weave.FRandRange(0.2f, 0.6f);
		}

		// And two holes eaten out of the middle of it, with edges torn by the same noise.
		struct FHole { float U; float T; float RU; float RT; };
		FHole Holes[2];
		for (FHole& Hole : Holes)
		{
			Hole.U = Weave.FRandRange(0.18f, 0.86f);
			Hole.T = Weave.FRandRange(0.26f, 0.66f);
			Hole.RU = Weave.FRandRange(0.05f, 0.12f);
			Hole.RT = Weave.FRandRange(0.035f, 0.085f);
		}

		const float Phase = Weave.FRandRange(0.f, 2.f * PI);
		const float Drift = Weave.FRandRange(0.f, 2.f * PI);
		const float Grain = Seed * 0.37f;

		auto Keep = [&](float U) -> float
		{
			float Left = 0.99f - 0.05f * FMath::Abs(FMath::PerlinNoise1D(U * 6.1f + Grain));
			for (const FBite& Bite : Bites)
			{
				const float D = (U - Bite.Where) / Bite.Width;
				Left -= Bite.Depth * FMath::Exp(-D * D);
			}
			Left -= 0.035f * FMath::Abs(FMath::PerlinNoise1D(U * 29.f + Grain * 3.1f));
			return FMath::Clamp(Left, 0.05f, 1.f);
		};

		auto Solid = [&](float U, float T) -> bool
		{
			if (T > Keep(U))
			{
				return false;
			}
			for (const FHole& Hole : Holes)
			{
				const float DU = (U - Hole.U) / Hole.RU;
				const float DT = (T - Hole.T) / Hole.RT;
				const float Edge = 1.f + 0.5f * FMath::PerlinNoise2D(FVector2D(U * 11.f + Grain, T * 11.f));
				if (DU * DU + DT * DT < Edge * Edge)
				{
					return false;
				}
			}
			return true;
		};

		auto Surface = [&](float U, float T) -> FVector
		{
			const float Across = U * FMath::Abs(SignedWidth);
			const float Open = 0.62f + 0.38f * T;                      // gathered at the pole, loose at the hem
			const float Wander = 4.2f * FMath::Sin(T * 2.7f + Drift);  // the fold lines are not plumb
			// Amplitude against period is the whole look of the cloth, and it is easy to get very
			// wrong: the first pass ran six and a half centimetres of swing over a thirteen
			// centimetre period, which is a slope of seventy degrees — corrugated iron, not a
			// drape. Lit by a window it came out as alternating blown-white and black stripes.
			// Two and a bit over seventeen is about forty degrees, which is cloth.
			float Depth = 2.3f * Open * FMath::Sin((Across + Wander) * (2.f * PI / 17.f) + Phase);
			Depth += 1.1f * Open * FMath::Sin((Across - Wander * 0.6f) * (2.f * PI / 41.f) + Phase * 0.7f);
			// And a little noise on top, because a fold that repeats exactly is a corrugation.
			Depth += 0.9f * Open * FMath::PerlinNoise2D(FVector2D(Across * 0.085f + Grain, T * 2.3f));
			Depth -= 2.4f * T;                                         // the hem hangs away from the wall
			return FVector(Depth, U * SignedWidth, -T * Length);
		};

		const int32 Grid = Cols * Rows;
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
		Verts.SetNum(Grid * 2);
		Normals.Init(FVector::ZeroVector, Grid * 2);
		UVs.SetNum(Grid * 2);
		Tangents.SetNum(Grid * 2);

		for (int32 Col = 0; Col < Cols; ++Col)
		{
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const float U = Col / static_cast<float>(Cols - 1);
				const float T = Row / static_cast<float>(Rows - 1);
				const FVector Point = Surface(U, T);
				const int32 Index = Col * Rows + Row;
				Verts[Index] = Point;
				Verts[Index + Grid] = Point + FVector(Thickness, 0.f, 0.f);
				// Our own UVs, in repeats, so the instance keeps TilingXY at one and the weave
				// runs continuously over the folds instead of per-part like a box does.
				const FVector2D UV(FMath::Abs(Point.Y) / TexCm, (T * Length) / TexCm);
				UVs[Index] = UV;
				UVs[Index + Grid] = UV;
			}
		}

		// Every triangle is emitted both ways round, and the shading normal is chosen explicitly
		// rather than inherited from the winding.
		//
		// This is not belt and braces, it is the bug. Winding decides which side of a triangle the
		// rasteriser keeps, the normal array decides which way the surface is lit, and getting the
		// first of them backwards on a single-sided material means the face the player is looking
		// at is culled and what they see is the sheet behind it — lit, correctly, for a surface
		// facing the window. That is exactly what the drape looked like: bands of blown white
		// where a fold happened to point at the sky and bands of black where it did not, on cloth
		// standing a metre from a lantern. Emitting both windings makes culling a non-question;
		// forcing the normal makes lighting a non-question; and the pair of them costs two extra
		// triangles per quad on an object there are two of in the game.
		auto Face = [&](int32 A, int32 B, int32 C, bool bFacingRoom)
		{
			Tris.Add(A);
			Tris.Add(B);
			Tris.Add(C);
			Tris.Add(A);
			Tris.Add(C);
			Tris.Add(B);

			FVector N = FVector::CrossProduct(Verts[B] - Verts[A], Verts[C] - Verts[A]);
			if ((N.X < 0.f) != bFacingRoom)
			{
				N = -N; // -X is into the room, which is the side the front sheet has to be lit on
			}
			Normals[A] += N;
			Normals[B] += N;
			Normals[C] += N;
		};

		for (int32 Col = 0; Col + 1 < Cols; ++Col)
		{
			for (int32 Row = 0; Row + 1 < Rows; ++Row)
			{
				const float U0 = Col / static_cast<float>(Cols - 1);
				const float U1 = (Col + 1) / static_cast<float>(Cols - 1);
				const float T0 = Row / static_cast<float>(Rows - 1);
				const float T1 = (Row + 1) / static_cast<float>(Rows - 1);
				if (!Solid(U0, T0) || !Solid(U1, T0) || !Solid(U0, T1) || !Solid(U1, T1))
				{
					continue;
				}

				const int32 A = Col * Rows + Row;
				const int32 B = (Col + 1) * Rows + Row;
				const int32 C = (Col + 1) * Rows + Row + 1;
				const int32 D = Col * Rows + Row + 1;

				Face(A, B, C, /*bFacingRoom*/ true);
				Face(A, C, D, /*bFacingRoom*/ true);
				Face(A + Grid, B + Grid, C + Grid, /*bFacingRoom*/ false);
				Face(A + Grid, C + Grid, D + Grid, /*bFacingRoom*/ false);
			}
		}

		for (int32 Col = 0; Col < Cols; ++Col)
		{
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const int32 Index = Col * Rows + Row;
				const int32 Before = FMath::Max(Col - 1, 0) * Rows + Row;
				const int32 After = FMath::Min(Col + 1, Cols - 1) * Rows + Row;
				const FVector Along = (Verts[After] - Verts[Before]).GetSafeNormal();
				Tangents[Index] = FProcMeshTangent(Along, false);
				Tangents[Index + Grid] = FProcMeshTangent(Along, false);

				Normals[Index] = Normals[Index].GetSafeNormal();
				Normals[Index + Grid] = Normals[Index + Grid].GetSafeNormal();
			}
		}

		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, MakeUniqueObjectName(Owner, UProceduralMeshComponent::StaticClass(), TEXT("Drape")));
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->AttachToComponent(Pivot, FAttachmentTransformRules::KeepRelativeTransform);
		Mesh->bUseAsyncCooking = false;
		Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, TArray<FLinearColor>(), Tangents, /*bCreateCollision*/ false);
		Mesh->SetMaterial(0, Cloth);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		Owner->AddInstanceComponent(Mesh);
		return Mesh;
	}
}

AStormWindowActor::AStormWindowActor()
{
	PrimaryActorTick.bCanEverTick = true;

	StormRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StormRoot"));
	SetRootComponent(StormRoot);
	StormRoot->SetMobility(EComponentMobility::Movable);

	// One directional light, not two. A second one makes the renderer pick arbitrarily which is
	// "the" light for volumetric fog and translucency, and complain about it on screen. So the
	// storm's constant overcast glow is the same light as the lightning, just at its floor value.
	LightningLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("LightningLight"));
	LightningLight->SetupAttachment(StormRoot);
	LightningLight->SetMobility(EComponentMobility::Movable);
	LightningLight->SetIntensity(StormAmbientLux);
	LightningLight->SetLightColor(LightningColor);
	LightningLight->SetCastShadows(true); // the whole point: hard, long shadows thrown across the room

	LightningGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightningGlow"));
	LightningGlow->SetupAttachment(StormRoot);
	LightningGlow->SetMobility(EComponentMobility::Movable);
	LightningGlow->SetIntensityUnits(ELightUnits::Candelas);
	LightningGlow->SetIntensity(0.f);
	LightningGlow->SetLightColor(LightningColor);
	LightningGlow->SetAttenuationRadius(2400.f);
	LightningGlow->SetCastShadows(false); // pure fill; the directional light already owns the shadows

	SkyPortal = CreateDefaultSubobject<URectLightComponent>(TEXT("SkyPortal"));
	SkyPortal->SetupAttachment(StormRoot);
	SkyPortal->SetMobility(EComponentMobility::Movable);
	SkyPortal->SetIntensityUnits(ELightUnits::Candelas);
	// Cold, and a long way from white: everything the storm lights has to read as the opposite of
	// the lantern, which is the room's whole colour idea.
	SkyPortal->SetLightColor(FLinearColor(0.40f, 0.55f, 0.88f));
	SkyPortal->SetCastShadows(true);
	// Nearly fully open. Narrowing the barn doors keeps light off the side walls — including the
	// door wall, which is the one the window is supposed to be printing itself onto.
	SkyPortal->SetBarnDoorAngle(88.f);
}

void AStormWindowActor::Configure(const FStormWindowSetup& InSetup)
{
	Setup = InSetup;
}

float AStormWindowActor::GetWindGust() const
{
	// One shared noise curve: a slow swell with a faster ripple on top, so gusts arrive in waves
	// instead of jittering. Remapped from Perlin's [-1,1] into [0,1].
	const float Swell = FMath::PerlinNoise1D(ElapsedTime * 0.23f);
	const float Ripple = FMath::PerlinNoise1D(ElapsedTime * 1.15f + 31.7f);
	return FMath::Clamp((Swell * 0.72f + Ripple * 0.28f) * 0.5f + 0.5f, 0.f, 1.f);
}

void AStormWindowActor::BeginPlay()
{
	Super::BeginPlay();

	Random.Initialize(20260918);

	BuildWindow();
	BuildOutsideWorld();
	BuildRain();
	BuildLightningBolts();

	const float WindowCenterZ = (Setup.SillHeight + Setup.TopHeight) * 0.5f;

	// Aimed low and across rather than steeply down. The angle is chosen so the beam comes through
	// the opening and lands on the door wall as a bright, muntin-barred rectangle — that patch is
	// the composition's second subject — and so everything standing on the floor throws a long
	// shadow away from the window instead of a short one under itself.
	const FRotator StrikeAim(-13.f, 122.f, 0.f);
	LightningLight->SetRelativeRotation(StrikeAim);
	LightningGlow->SetRelativeLocation(FVector(220.f, 0.f, WindowCenterZ + 60.f));

	// The sky, filling the opening from just outside the glass. Sized to the hole in the wall, so
	// the light arriving in the room is exactly the shape of the window.
	SkyPortal->SetRelativeLocation(FVector(Setup.WallThickness * 0.5f + 16.f, 0.f, WindowCenterZ));
	SkyPortal->SetRelativeRotation(FRotator(0.f, 180.f, 0.f)); // emits along its own +X, so it faces the room
	SkyPortal->SetSourceWidth(Setup.OpeningWidth);
	SkyPortal->SetSourceHeight(Setup.TopHeight - Setup.SillHeight);
	SkyPortal->SetAttenuationRadius(2000.f);
	SkyPortal->SetIntensity(SkyPortalCandelas);

	TimeUntilNextStrike = Random.FRandRange(1.2f, 2.4f); // one early strike, while the player is still getting oriented
}

void AStormWindowActor::BuildWindow()
{
	FRoomBuilder Build(this, StormRoot);

	// The joinery is painted, not bare timber. It is the palest thing in the room and the only
	// set of straight lines left in it, which is what makes the window read as a window from
	// across the floor — before the lightning shows anything of what is behind it.
	UMaterialInstanceDynamic* PaintMat = Build.Flat(RoomPalette::PaintedTrim, 0.62f);
	UMaterialInstanceDynamic* PaintWornMat = Build.Flat(RoomPalette::PaintedTrim * 0.55f, 0.82f);
	// Thin and nearly clear head-on. The grime is still there in the roughness, but the panes have
	// to be *seen through* — the trees, the rain and the bolt behind them are the point of the
	// window, and at a quarter opacity the glass was a sheet of frost with a view painted on it.
	//
	// REVERSED back to these values. They were taken down to 0.055 and 0.03 on the reading that
	// the sheen was washing the view out; it was not — the panes were fine and the broken ones
	// were black for an unrelated reason, and clearing the glass this far only took the window's
	// dirt with it. A pane of glass in a house like this is meant to be *nearly* clear.
	UMaterialInstanceDynamic* GlassMat = Build.Glass(FLinearColor(0.10f, 0.13f, 0.16f), 0.12f, 0.05f);
	if (GlassMat)
	{
		GlassMat->SetScalarParameterValue(TEXT("RoughnessSmear"), 0.20f);
	}
	// The fracture on the starred pane, carried on a sheet laid over the glass.
	//
	// The network itself is baked by Tools/make_glass_crack.py, and it took three goes to get
	// here. It was bars twice, and both times the answer was the same object with a different
	// colour on it: a bar has to be thick enough to render, and anything thick enough to render
	// shows its side from everywhere but dead ahead. Then it was drawn in the material from the
	// contour of a noise field — the trick that draws the cracks in the plaster and the cobwebs in
	// the corners — and it came out a scribble, because a noise contour is a smooth curve that
	// wanders, loops and doubles back, and knows nothing about where the stone hit.
	//
	// What the eye reads as broken glass is straightness and a common origin, and that is a thing
	// a generator can lay out and a material graph cannot.
	//
	// Dim, and ROUGH. The first pass had it near-white at a polished 0.16 roughness, on the
	// reasoning that a fracture in glass is glass — and it came back a star drawn in chalk, with
	// a bloom around the impact, because a mirror-smooth surface a metre in front of a sky portal
	// mirrors the sky portal. The physics runs the other way: a fracture face is conchoidal and
	// microscopically rough, which is the whole reason a crack is visible at all. Polish it and it
	// would disappear. Rough, and held under the brightness the bloom picks up, it is a hairline
	// again instead of a stroke of paint with a glow round it.
	//
	// The tint is then the balance between those two failures, and it is set high rather than low:
	// this pane is looked at from across the room far more often than from arm's length, and at
	// that distance the mip chain is averaging a sub-pixel line into the glass around it. A split
	// that is honest about its width and its brightness at the same time is not there at all.
	UMaterialInstanceDynamic* CrackMat = Build.GlassCrack(
		FLinearColor(0.38f, 0.40f, 0.44f), /*Opacity*/ 0.95f, /*Haze*/ 0.16f, /*Roughness*/ 0.45f);

	// Both of these were near-neutral tints on photographs that are not neutral — rough_linen is a
	// blue linen and green_metal_rust is a sheet of green paint — and a tint multiplies rather than
	// neutralises, so the curtains hung blue and the bars read green. See ARoomDressingActor's
	// material block for the measurements these come from.
	//
	// Grey, and worked backwards from the photograph rather than picked. rough_linen sits at
	// linear (0.283, 0.407, 0.612), so a tint of (0.268, 0.184, 0.118) lands the cloth on
	// (0.076, 0.075, 0.072) — a grey that stays grey, and a shade under the plaster, because a
	// curtain is the dirtiest soft thing in a room like this. The old value was a third of that
	// and brown, so what hung at the window was two near-black slabs with no light on them to
	// show any weave at all.
	UMaterialInstanceDynamic* ClothMat = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.268f, 0.184f, 0.118f), 1.18f);
	if (ClothMat)
	{
		// The drape generates its own UVs, already in repeats, so this instance must not scale
		// them a second time. Only the generated mesh uses it directly — the hanging threads go
		// through Add(), which derives its own tiled instance per part and leaves this one alone.
		ClothMat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		ClothMat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	UMaterialInstanceDynamic* IronMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.635f, 0.336f, 0.527f));

	const float Height = Setup.TopHeight - Setup.SillHeight;
	const float CenterZ = (Setup.SillHeight + Setup.TopHeight) * 0.5f;
	const float HalfWidth = Setup.OpeningWidth * 0.5f;
	const float InnerX = -Setup.WallThickness * 0.5f; // the room-side face of the wall
	const float SashX = 1.f;                          // the sash sits mid-reveal, glass roughly in the wall plane

	// The reveal. The wall is twenty centimetres thick, so the opening is a short tunnel, and
	// lining it is what gives the window depth instead of the look of a rectangle cut in card.
	Build.Box(FVector(0.f, -HalfWidth - 3.f, CenterZ), FRotator::ZeroRotator, FVector(Setup.WallThickness + 2.f, 6.f, Height + 12.f), PaintMat);
	Build.Box(FVector(0.f, HalfWidth + 3.f, CenterZ), FRotator::ZeroRotator, FVector(Setup.WallThickness + 2.f, 6.f, Height + 12.f), PaintMat);
	Build.Box(FVector(0.f, 0.f, Setup.TopHeight + 3.f), FRotator::ZeroRotator, FVector(Setup.WallThickness + 2.f, Setup.OpeningWidth + 12.f, 6.f), PaintMat);

	// The inner sill, projecting into the room and tilted a degree to shed water it has not had
	// to shed in years. Deep enough to stand things on: it is where the key sits.
	Build.Box(FVector(InnerX - 10.f, 0.f, Setup.SillHeight - 3.f), FRotator(-1.5f, 0.f, 0.f), FVector(Setup.WallThickness + 26.f, Setup.OpeningWidth + 24.f, 6.f), PaintMat);
	Build.Box(FVector(InnerX - 20.f, 0.f, Setup.SillHeight - 9.f), FRotator::ZeroRotator, FVector(4.f, Setup.OpeningWidth + 18.f, 7.f), PaintWornMat, /*bBlockingCollision*/ false);

	// Sash: the perimeter, then the muntin grid that divides it into small panes. The grid is the
	// point of the whole assembly — it is the pattern the storm prints across the far wall.
	const int32 Cols = 4;
	const int32 Rows = 4;
	const float SashW = 7.f;
	const float MuntinW = 2.6f;
	const float SashDepth = 5.f;

	Build.Box(FVector(SashX, 0.f, Setup.SillHeight + SashW * 0.5f), FRotator::ZeroRotator, FVector(SashDepth, Setup.OpeningWidth, SashW), PaintMat);
	Build.Box(FVector(SashX, 0.f, Setup.TopHeight - SashW * 0.5f), FRotator::ZeroRotator, FVector(SashDepth, Setup.OpeningWidth, SashW), PaintMat);
	Build.Box(FVector(SashX, -HalfWidth + SashW * 0.5f, CenterZ), FRotator::ZeroRotator, FVector(SashDepth, SashW, Height), PaintMat);
	Build.Box(FVector(SashX, HalfWidth - SashW * 0.5f, CenterZ), FRotator::ZeroRotator, FVector(SashDepth, SashW, Height), PaintMat);

	const float InnerWidth = Setup.OpeningWidth - SashW * 2.f;
	const float InnerHeight = Height - SashW * 2.f;
	const float CellW = (InnerWidth - MuntinW * (Cols - 1)) / Cols;
	const float CellH = (InnerHeight - MuntinW * (Rows - 1)) / Rows;
	const float FirstY = -InnerWidth * 0.5f + CellW * 0.5f;
	const float FirstZ = Setup.SillHeight + SashW + CellH * 0.5f;

	auto CellCenter = [&](int32 Col, int32 Row)
	{
		return FVector2D(FirstY + Col * (CellW + MuntinW), FirstZ + Row * (CellH + MuntinW));
	};

	for (int32 Col = 1; Col < Cols; ++Col)
	{
		const float Y = FirstY + (Col - 0.5f) * (CellW + MuntinW);
		Build.Box(FVector(SashX, Y, CenterZ), FRotator::ZeroRotator, FVector(SashDepth - 1.f, MuntinW, InnerHeight), PaintMat, /*bBlockingCollision*/ false);
	}
	for (int32 Row = 1; Row < Rows; ++Row)
	{
		const float Z = FirstZ + (Row - 0.5f) * (CellH + MuntinW);
		// The middle one is the meeting rail where the two sashes overlap, so it is heavier than
		// the muntins above and below it.
		const bool bMeetingRail = (Row == Rows / 2);
		Build.Box(FVector(SashX, 0.f, Z), FRotator::ZeroRotator,
			FVector(SashDepth - (bMeetingRail ? 0.f : 1.f), InnerWidth, bMeetingRail ? MuntinW * 2.4f : MuntinW),
			PaintMat, /*bBlockingCollision*/ false);
	}

	// Glass. Two panes are gone — that is where the wind and the rain get in, and where the glass
	// lying on the boards below came from — and one has taken a knock without letting go.
	//
	// Every pane is generated, because a hole in a pane has to be an *outline*: any arrangement of
	// boxes around an opening leaves the opening with straight inner edges, and a straight edge is
	// the one thing a pane that has been hit does not have.
	const FIntPoint BlownPanes[2] = { FIntPoint(0, 2), FIntPoint(2, 3) };
	const FIntPoint CrackedPane(1, 1);

	for (int32 Col = 0; Col < Cols; ++Col)
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const FVector2D Center = CellCenter(Col, Row);
			const FIntPoint Cell(Col, Row);
			const bool bBlown = (BlownPanes[0] == Cell) || (BlownPanes[1] == Cell);
			const bool bCracked = (Cell == CrackedPane);

			// Only the blown panes are damaged in the mesh, and only by losing glass. Splits were
			// cut into every pane for a while, and sixteen cracked panes is not a broken window,
			// it is a texture — it takes the two holes and the one starred pane down with it.
			// Damage is worth what it is worth by being somewhere and not everywhere.
			FPaneDamage Damage;
			if (bBlown)
			{
				Damage.BreakAt = FVector2D(Random.FRandRange(0.34f, 0.66f), Random.FRandRange(0.32f, 0.68f));
				Damage.HoleRadiusCm = FMath::Min(CellW, CellH) * Random.FRandRange(0.34f, 0.46f);
			}

			// Local X runs up the pane and local Y across it, which is what the pitch is for.
			Build.Pane(
				FVector(SashX, Center.X, Center.Y), FRotator(90.f, 0.f, 0.f),
				FVector2D(CellH + 1.f, CellW + 1.f),
				Damage, Random.RandRange(1, 90000), GlassMat);

			if (bCracked)
			{
				// One sheet, laid a centimetre in front of the glass, SQUARE and centred on the
				// impact. The crack map covers a square patch of glass so that the star lands on
				// a pane of any proportion without coming out elliptical — stretch this sheet to
				// the pane's own 5:3 and the network turns into an oval, which nothing that has
				// ever been hit looks like. Square on the pane's height means the splits running
				// up and down leave the glass at the muntin, the way a crack does when it reaches
				// the frame, and the ones running across have room to die out in open glass.
				//
				// The impact is a hand's width off the middle of the pane, because a break in the
				// exact centre of a rectangle is the one place it reads as decoration. Off-centre
				// across only: the sheet is as tall as the pane, so moving it up or down would
				// hang it over the muntin.
				//
				// A plane rather than a thin box, because a box has four rims the alpha never
				// touches — that is what left a rectangle of pale sticks hanging in every ceiling
				// corner when the cobwebs were slabs.
				const float ImpactY = Center.X - CellW * 0.13f;
				if (UStaticMeshComponent* Fracture = Build.Add(FRoomShapes::Plane(),
					FVector(SashX - 1.f, ImpactY, Center.Y), FRotator(90.f, 0.f, 0.f),
					FVector(CellH, CellH, 1.f), CrackMat, /*bBlockingCollision*/ false))
				{
					// What should print on the far wall is the muntin grid, as with the panes.
					Fracture->SetCastShadow(false);
				}
			}
		}
	}

	// Curtain pole and its brackets. Iron, and long out of true.
	const float PoleZ = Setup.TopHeight + 20.f;
	const float PoleX = InnerX - 16.f;
	Build.Cyl(FVector(PoleX, 0.f, PoleZ), FRotator(0.f, 0.f, 90.f), FVector(3.f, 3.f, Setup.OpeningWidth + 86.f), IronMat, /*bBlockingCollision*/ false);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.f : 1.f;
		Build.Sph(FVector(PoleX, SideSign * (HalfWidth + 44.f), PoleZ), 7.f, IronMat);
		Build.Box(FVector(PoleX + 8.f, SideSign * (HalfWidth + 38.f), PoleZ), FRotator::ZeroRotator, FVector(18.f, 4.f, 4.f), IronMat, /*bBlockingCollision*/ false);
	}

	// The curtains, and they are the one thing in this room that cannot be made out of boxes.
	//
	// Every version before this was a rank of slabs: five, then nine, with alternating depths to
	// fake the light and shade of hanging folds and a ragged run of lengths to fake a rotted hem.
	// It is a good trick for a thing seen once across a dark room and it does not survive being
	// looked at, because every silhouette in it is still a straight line and every surface in it
	// is still flat. Cloth has neither. A drape is a *curved* surface — the fold is a continuous
	// wave that wanders as it falls and opens towards the hem — and where it has rotted through,
	// the edge is a torn contour, not a cut.
	//
	// So it is a generated mesh: a grid across the width and down the length, displaced in depth
	// by two sine waves of different period, with a per-column survival fraction that decides
	// where the hem has gone and two holes eaten out of the middle. Quads whose corners are not
	// all still cloth are simply never emitted, which is what makes the tear an outline rather
	// than a shape somebody drew. At fifty-seven by eighty-one the grid steps are about a
	// centimetre, so nothing in the torn edge reads as a stair.
	//
	// Both faces are generated, half a centimetre apart. M_RoomSurface is single-sided — it is
	// built for walls, which have nothing behind them — and half of what the player sees of these
	// is the back of the drape against the window.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float SideSign = Side == 0 ? -1.f : 1.f;

		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("CurtainPivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(StormRoot, FAttachmentTransformRules::KeepRelativeTransform);
		// The pivot is the pole, so the drape swings from its top edge like real hanging cloth.
		Pivot->SetRelativeLocation(FVector(PoleX, SideSign * (HalfWidth + 16.f), PoleZ - 4.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder CurtainBuild(this, Pivot);

		// One drape is longer than the other; nothing in this house is a matched pair any more.
		// The width runs towards the middle of the window, which is local -SideSign.
		const float DrapeLength = (Height + 30.f) * (Side == 0 ? 1.f : 0.88f);
		BuildDrapeMesh(this, Pivot, ClothMat, -SideSign * 64.f, DrapeLength, Side == 0 ? 1104 : 1955);

		// A few threads still hanging where the hem tore away. These are boxes and have every
		// right to be: a thread is a straight thin thing, which is the one shape a box is honest
		// about.
		for (int32 i = 0; i < 5; ++i)
		{
			const float Hang = Random.FRandRange(7.f, 30.f);
			CurtainBuild.Box(
				FVector(Random.FRandRange(-5.f, 2.f), -SideSign * Random.FRandRange(4.f, 58.f), -DrapeLength * Random.FRandRange(0.42f, 0.9f) - Hang * 0.5f),
				FRotator(0.f, 0.f, Random.FRandRange(-11.f, 11.f)),
				FVector(Random.FRandRange(0.6f, 1.8f), Random.FRandRange(0.8f, 2.4f), Hang),
				ClothMat,
				/*bBlockingCollision*/ false);
		}

		// Damp, in the cloth rather than on it. Aimed along +X, which is into the front faces of
		// the folds, and the projection only reaches nine centimetres — far short of the glass.
		// On the pivot, so the stains swing with the curtain they are in.
		for (int32 Bloom = 0; Bloom < 3; ++Bloom)
		{
			CurtainBuild.Stain(
				RoomSurfaces::Damp,
				FVector(-11.f, -SideSign * Random.FRandRange(2.f, 52.f), -Random.FRandRange(20.f, DrapeLength * 0.8f)),
				FRotator(0.f, 0.f, Random.FRandRange(0.f, 360.f)),
				FVector2D(Random.FRandRange(34.f, 76.f), Random.FRandRange(30.f, 64.f)),
				FLinearColor(0.130f, 0.098f, 0.070f),
				Random.FRandRange(0.35f, 0.6f),
				1.f,
				1.2f);
		}

		Curtains.Add(Pivot);
	}
}

void AStormWindowActor::BuildOutsideWorld()
{
	FRoomBuilder Build(this, StormRoot);

	// The sky is emissive, not lit. It is the brightest thing in the frame and the only reason
	// the treeline reads as a silhouette at all — a shaded backdrop at this albedo, lit by the
	// same six lux that lights the room, would come out as black as the wall beside the window.
	SkyMaterial = Build.Emissive(FLinearColor(0.42f, 0.52f, 0.68f), SkyGlowFloor);

	// REVERSED: the treeline and the ground are lit surfaces again, and very dark ones.
	//
	// They were made emissive at a fifth of the sky to stop a *hole* in the window reading as a
	// black rectangle — on the theory that rain scatters the sky into anything forty metres off,
	// so a wood at night goes the colour of the sky and darker rather than black. The theory is
	// sound and it was fixing the wrong thing: the black rectangle was a winding bug in the
	// generated panes (see FRoomBuilder::Pane), and with that fixed, all the emissive treeline did
	// was take the silhouette away. A wood lit to a fifth of the sky behind it is a flat grey
	// cut-out; a wood at two per cent albedo against a storm sky is a shape, and the shape is the
	// whole of what a window at night has to show.
	UMaterialInstanceDynamic* GroundMat = Build.Flat(FLinearColor(0.020f, 0.022f, 0.018f), 1.f);
	UMaterialInstanceDynamic* TrunkMat = Build.Flat(FLinearColor(0.016f, 0.014f, 0.012f), 1.f);
	UMaterialInstanceDynamic* LeafMat = Build.Flat(RoomPalette::Foliage, 1.f);

	// A backdrop far enough out that it never enters the lantern's reach — it exists so the player
	// sees storm-lit distance through the window instead of the empty void past the level.
	//
	// Neither of these may cast a shadow, and that is not a performance nicety. The storm's light
	// is a *directional* light: it arrives from beyond the treeline, so a sixty-metre slab of sky
	// standing between it and the window puts the entire room inside one enormous shadow. That is
	// precisely what it did — the window went black and not one photon of storm light reached the
	// far wall.
	if (UStaticMeshComponent* Sky = Build.Box(FVector(3200.f, 0.f, 900.f), FRotator::ZeroRotator, FVector(40.f, 6000.f, 3600.f), SkyMaterial, /*bBlockingCollision*/ false))
	{
		Sky->SetCastShadow(false);
	}
	if (UStaticMeshComponent* Ground = Build.Box(FVector(1600.f, 0.f, -40.f), FRotator::ZeroRotator, FVector(3400.f, 6000.f, 40.f), GroundMat, /*bBlockingCollision*/ false))
	{
		Ground->SetCastShadow(false);
	}

	// Treeline. Dense and close: through a window this size the player sees a narrow cone, and it
	// wants to be full of wet black branches rather than showing the gap between two of them.
	// Trunk plus stacked cones, each on its own pivot at the base so it bends rather than slides.
	const int32 TreeCount = 16;
	for (int32 i = 0; i < TreeCount; ++i)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("TreePivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(StormRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocation(FVector(Random.FRandRange(620.f, 2400.f), Random.FRandRange(-1100.f, 1100.f), -40.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder TreeBuild(this, Pivot);
		const float TreeHeight = Random.FRandRange(500.f, 980.f);

		// Nothing out here casts a shadow. A treeline is a solid wall to a light arriving almost
		// horizontally, and with shadows on it simply switched the storm off — the room went black
		// and the window with it.
		auto NoShadow = [](UStaticMeshComponent* Part)
		{
			if (Part)
			{
				Part->SetCastShadow(false);
			}
		};

		NoShadow(TreeBuild.Cyl(FVector(0.f, 0.f, TreeHeight * 0.5f), FRotator::ZeroRotator, FVector(Random.FRandRange(26.f, 44.f), Random.FRandRange(26.f, 44.f), TreeHeight), TrunkMat, /*bBlockingCollision*/ false));

		const int32 CanopyLayers = Random.RandRange(2, 4);
		for (int32 Layer = 0; Layer < CanopyLayers; ++Layer)
		{
			const float LayerFraction = 1.f - Layer * 0.22f;
			NoShadow(TreeBuild.Add(
				FRoomShapes::Cone(),
				FVector(0.f, 0.f, TreeHeight * (0.55f + Layer * 0.18f)),
				FRotator::ZeroRotator,
				FVector(TreeHeight * 0.62f * LayerFraction, TreeHeight * 0.62f * LayerFraction, TreeHeight * 0.45f * LayerFraction),
				LeafMat,
				/*bBlockingCollision*/ false));
		}

		Trees.Add(Pivot);
		TreePhases.Add(Random.FRandRange(0.f, 100.f));
	}
}

void AStormWindowActor::BuildLightningBolts()
{
	// Three bolts, built once and hidden. Each is a jagged walk downward with two forks off it —
	// the shape matters less than the fact that it is different every strike, because a player
	// who sees the same bolt twice stops believing in the weather.
	const int32 BoltCount = 3;
	for (int32 BoltIndex = 0; BoltIndex < BoltCount; ++BoltIndex)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("BoltPivot")));
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->AttachToComponent(StormRoot, FAttachmentTransformRules::KeepRelativeTransform);
		Pivot->SetRelativeLocation(FVector(2400.f, 0.f, 0.f));
		Pivot->RegisterComponent();
		AddInstanceComponent(Pivot);

		FRoomBuilder BoltBuild(this, Pivot);
		UMaterialInstanceDynamic* BoltMat = BoltBuild.Emissive(RoomPalette::Lightning, 0.f);

		// Draws one jagged run of segments between two heights, and returns where it ended so a
		// fork can be hung off it.
		auto DrawRun = [&](FVector From, float ToZ, int32 Segments, float Spread, float Thickness) -> FVector
		{
			FVector Current = From;
			for (int32 i = 0; i < Segments; ++i)
			{
				const float Alpha = (i + 1) / static_cast<float>(Segments);
				const FVector Next(
					From.X + Random.FRandRange(-Spread, Spread),
					From.Y + Random.FRandRange(-Spread, Spread) + (Alpha - 0.5f) * Spread,
					FMath::Lerp(From.Z, ToZ, Alpha));

				const FVector Delta = Next - Current;
				const float Length = Delta.Size();
				if (Length > KINDA_SMALL_NUMBER)
				{
					const float Taper = FMath::Lerp(Thickness, Thickness * 0.35f, Alpha);
					BoltBuild.Cyl(
						Current + Delta * 0.5f,
						FRotationMatrix::MakeFromZ(Delta / Length).Rotator(),
						FVector(Taper, Taper, Length * 1.06f), // overlapped a little so the joints do not show gaps
						BoltMat,
						/*bBlockingCollision*/ false);
				}
				Current = Next;
			}
			return Current;
		};

		const FVector Head(0.f, 0.f, 1500.f);
		const FVector Tail = DrawRun(Head, 40.f, 11, 130.f, 9.f);
		DrawRun(FVector(Tail.X, Tail.Y, 780.f), 260.f, 4, 180.f, 5.f);
		DrawRun(FVector(Tail.X, Tail.Y, 1080.f), 620.f, 3, 150.f, 4.f);

		Pivot->SetVisibility(false, /*bPropagateToChildren*/ true);
		Bolts.Add(Pivot);
		BoltMaterials.Add(BoltMat);
	}
}

void AStormWindowActor::BuildRain()
{
	FRoomBuilder Build(this, StormRoot);

	// Rain as instanced slivers rather than a particle system: Niagara systems are Content Browser
	// assets and this project builds everything from code. A few hundred streaks in the window's
	// cone of view is enough — the player only ever sees rain framed by the opening.
	UMaterialInstanceDynamic* RainMat = Build.Flat(RoomPalette::Rain, 0.1f, 0.f);
	RainInstances = Build.Instances(FRoomShapes::Cube(), RainMat);
	if (!RainInstances)
	{
		return;
	}

	RainPositions.Reserve(RainDropCount);
	RainSpeeds.Reserve(RainDropCount);

	for (int32 i = 0; i < RainDropCount; ++i)
	{
		const FVector Position(Random.FRandRange(120.f, 1500.f), Random.FRandRange(-900.f, 900.f), Random.FRandRange(-40.f, 800.f));
		RainPositions.Add(Position);
		RainSpeeds.Add(Random.FRandRange(1300.f, 2000.f));

		// Slanted along the wind and stretched into a streak — the shape a falling drop makes on screen.
		const FTransform DropTransform(FRotator(0.f, 0.f, -18.f), Position, FVector(0.008f, 0.008f, Random.FRandRange(0.28f, 0.62f)));
		RainInstances->AddInstance(DropTransform);
	}
}

void AStormWindowActor::TickCurtains(float DeltaTime)
{
	const float Gust = GetWindGust();

	for (int32 i = 0; i < Curtains.Num(); ++i)
	{
		USceneComponent* Pivot = Curtains[i];
		if (!Pivot)
		{
			continue;
		}

		// Cloth lags the gust and overshoots slightly. Sampling the same noise at a different rate
		// gives that without a cloth sim.
		const float Lag = FMath::PerlinNoise1D(ElapsedTime * 0.9f + i * 13.3f) * 0.5f + 0.5f;
		// Degrees at the pole, so a little goes a long way down two metres of hanging cloth.
		const float Swing = FMath::Lerp(0.8f, 5.5f, Gust) * FMath::Lerp(0.6f, 1.f, Lag);
		const float Sway = FMath::Sin(ElapsedTime * 1.4f + i * 2.1f) * 2.5f;
		Pivot->SetRelativeRotation(FRotator(0.f, Sway, Swing * (i == 0 ? 1.f : -1.f)));
	}
}

void AStormWindowActor::TickTrees(float DeltaTime)
{
	const float Gust = GetWindGust();

	for (int32 i = 0; i < Trees.Num(); ++i)
	{
		USceneComponent* Pivot = Trees[i];
		if (!Pivot)
		{
			continue;
		}

		const float Phase = TreePhases.IsValidIndex(i) ? TreePhases[i] : 0.f;
		// Violent, not decorative: a heavy lean driven by the gust plus a fast whip on top.
		const float Lean = FMath::Lerp(2.f, 13.f, Gust) * (0.7f + 0.3f * FMath::Sin(ElapsedTime * 0.8f + Phase));
		const float Whip = FMath::Sin(ElapsedTime * 3.3f + Phase) * FMath::Lerp(0.8f, 4.f, Gust);
		Pivot->SetRelativeRotation(FRotator(0.f, 0.f, -(Lean + Whip)));
	}
}

void AStormWindowActor::TickRain(float DeltaTime)
{
	if (!RainInstances || RainPositions.Num() == 0)
	{
		return;
	}

	const float Gust = GetWindGust();
	const float WindDrift = FMath::Lerp(180.f, 620.f, Gust);
	const float Slant = FMath::Lerp(-10.f, -30.f, Gust);

	// Reused across frames rather than rebuilt: four hundred-odd transforms allocated and thrown
	// away every tick is a heap churn nothing in this scene has any use for. Reset keeps the slack.
	RainTransforms.Reset(RainPositions.Num());

	for (int32 i = 0; i < RainPositions.Num(); ++i)
	{
		FVector& Position = RainPositions[i];
		Position.Z -= RainSpeeds[i] * DeltaTime;
		Position.Y -= WindDrift * DeltaTime;

		// Recycle from the top once a drop lands or blows out of the band, keeping the count fixed.
		if (Position.Z < -40.f || Position.Y < -950.f)
		{
			Position.X = Random.FRandRange(120.f, 1500.f);
			Position.Y = Random.FRandRange(-200.f, 950.f);
			Position.Z = Random.FRandRange(700.f, 900.f);
		}

		RainTransforms.Add(FTransform(FRotator(0.f, 0.f, Slant), Position, FVector(0.008f, 0.008f, 0.45f)));
	}

	RainInstances->BatchUpdateInstancesTransforms(0, RainTransforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}

void AStormWindowActor::BeginStrike()
{
	// Each strike is a short burst of two to five sub-flashes (RandRange is inclusive at both
	// ends). The first is the brightest; the rest are the flickering afterbeats that make
	// lightning feel like a discharge rather than a light switch.
	SubFlashesRemaining = Random.RandRange(2, 5);
	StrikeIntensity = Random.FRandRange(StrikeLux.X, StrikeLux.Y);
	bSubFlashOn = true;
	SubFlashTimer = Random.FRandRange(0.05f, 0.12f);

	// Nudge the aim so consecutive strikes throw shadows in visibly different directions.
	const FRotator Aim(Random.FRandRange(-20.f, -6.f), Random.FRandRange(108.f, 136.f), 0.f);
	LightningLight->SetRelativeRotation(Aim);

	// Show the strike, not just its effect. One of the prebuilt bolts is moved out beyond the
	// treeline and switched on for the leading sub-flash — near enough to be framed by the
	// window, far enough that it reads as weather rather than as an object in the garden.
	if (Bolts.Num() > 0)
	{
		ActiveBolt = Random.RandRange(0, Bolts.Num() - 1);
		for (int32 i = 0; i < Bolts.Num(); ++i)
		{
			if (Bolts[i])
			{
				Bolts[i]->SetVisibility(i == ActiveBolt, /*bPropagateToChildren*/ true);
			}
		}

		if (USceneComponent* Bolt = Bolts[ActiveBolt])
		{
			Bolt->SetRelativeLocation(FVector(Random.FRandRange(1500.f, 2900.f), Random.FRandRange(-700.f, 700.f), 0.f));
			Bolt->SetRelativeRotation(FRotator(0.f, Random.FRandRange(-25.f, 25.f), 0.f));
		}
	}

	// PlayThunder() belongs here — deliberately unwired until a free-licensed thunder cue is
	// sourced, matching the storm-audio decision already logged for this room.
}

void AStormWindowActor::TickLightning(float DeltaTime)
{
	if (SubFlashesRemaining > 0)
	{
		SubFlashTimer -= DeltaTime;
		if (SubFlashTimer <= 0.f)
		{
			if (bSubFlashOn)
			{
				// Gap between afterbeats.
				bSubFlashOn = false;
				SubFlashTimer = Random.FRandRange(0.04f, 0.14f);
				--SubFlashesRemaining;
				if (SubFlashesRemaining <= 0)
				{
					TimeUntilNextStrike = Random.FRandRange(StrikeInterval.X, StrikeInterval.Y);
				}
			}
			else
			{
				bSubFlashOn = true;
				SubFlashTimer = Random.FRandRange(0.03f, 0.1f);
				StrikeIntensity *= Random.FRandRange(0.45f, 0.8f); // afterbeats fall off
			}
		}
	}
	else
	{
		TimeUntilNextStrike -= DeltaTime;
		if (TimeUntilNextStrike <= 0.f)
		{
			BeginStrike();
		}
	}

	// Snaps on instantly but decays over a few frames, so the room dims back down instead of
	// cutting to black and the flash leaves an afterimage.
	const float Target = (SubFlashesRemaining > 0 && bSubFlashOn) ? 1.f : 0.f;
	FlashAlpha = (Target > FlashAlpha) ? Target : FMath::FInterpTo(FlashAlpha, Target, DeltaTime, 14.f);

	// Never drops to zero: the floor value is the overcast sky the storm sits under, and it is what
	// keeps the window a faint blue rectangle between strikes.
	LightningLight->SetIntensity(StormAmbientLux + FlashAlpha * StrikeIntensity);
	LightningGlow->SetIntensity(FlashAlpha * StrikeIntensity * 260.f);
	// The window itself floods when the sky goes off: from inside a room, that — not the bolt — is
	// what a strike actually looks like.
	SkyPortal->SetIntensity(SkyPortalCandelas * (1.f + FlashAlpha * 9.f));

	// The channel is only lit while it is actually discharging — it snaps off with the sub-flash
	// rather than fading, which is what stops it looking like a hanging neon tube.
	const bool bDischarging = SubFlashesRemaining > 0 && bSubFlashOn;
	if (BoltMaterials.IsValidIndex(ActiveBolt) && BoltMaterials[ActiveBolt])
	{
		BoltMaterials[ActiveBolt]->SetScalarParameterValue(TEXT("Intensity"), bDischarging ? 14.f : 0.f);
	}
	if (!bDischarging && ActiveBolt != INDEX_NONE && Bolts.IsValidIndex(ActiveBolt) && Bolts[ActiveBolt])
	{
		Bolts[ActiveBolt]->SetVisibility(false, /*bPropagateToChildren*/ true);
	}

	// The whole sky lights up with the discharge, not just the channel — from inside the room that
	// is most of what a distant strike looks like.
	if (SkyMaterial)
	{
		SkyMaterial->SetScalarParameterValue(TEXT("Intensity"), SkyGlowFloor + FlashAlpha * 5.f);
	}
}

void AStormWindowActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ElapsedTime += DeltaTime;

	TickLightning(DeltaTime);
	TickCurtains(DeltaTime);
	TickTrees(DeltaTime);
	TickRain(DeltaTime);
}
