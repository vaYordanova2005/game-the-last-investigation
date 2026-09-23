#include "CorridorActor.h"
#include "RoomBuildLibrary.h"
#include "HallDoorActor.h"
#include "ClueActor.h"
#include "StormWindowActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
	/** Where the boards have given way, in front of the north wall between two portraits. */
	const FBox2D FloorHole(FVector2D(-335.f, 335.f), FVector2D(-245.f, 398.f));

	/** Where the ceiling has come down, over the runner. The fitting that hung there is on the floor. */
	const FVector2D CeilingHole(-800.f, 452.f);

	constexpr float ChairRail = 92.f;
	constexpr float PictureRail = 240.f;
	/** The family, down both walls. Shared with the wallpaper so no peel is hung over one. */
	struct FPortrait { bool bNorth; float U; float Z; float Size; float Tilt; bool bGlassGone; };
	const FPortrait Portraits[] = {
		{ true,  -150.f, 168.f, 78.f,  0.6f, false },
		{ true,  -330.f, 172.f, 56.f,  -7.f, false },
		{ true,  -880.f, 170.f, 64.f,  2.f,  true  },
		{ true,  -1180.f, 176.f, 58.f, -1.5f, false },
		{ false, 80.f,   168.f, 60.f,  4.f,  false },
		{ false, -680.f, 170.f, 92.f,  -1.f, false },
		{ false, -1080.f, 166.f, 62.f, 9.f,  true  },
		{ false, -1180.f, 196.f, 40.f, -3.f, false },
	};
	/** The turned portrait (a clue) and the mirror, which a peel must not cover either. */
	constexpr float TurnedPortraitU = -520.f;

	/**
	 * hanging_picture_frame_01's artwork is a pale print, and at the height of a lantern held at
	 * the chest it came out a white card in every frame. A painting sixty years in a damp house
	 * has gone brown under its varnish, so the artwork slot is re-instanced with the master's Tint.
	 */
	void DarkenArtwork(UStaticMeshComponent* Frame)
	{
		if (UMaterialInterface* Art = Frame ? Frame->GetMaterial(1) : nullptr)
		{
			if (UMaterialInstanceDynamic* Aged = UMaterialInstanceDynamic::Create(Art, Frame))
			{
				Aged->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.30f, 0.24f, 0.17f));
				Frame->SetMaterial(1, Aged);
			}
		}
	}
}

ACorridorActor::ACorridorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	CorridorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CorridorRoot"));
	SetRootComponent(CorridorRoot);
	CorridorRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
}

void ACorridorActor::Configure(const FCorridorSetup& InSetup, AStormWindowActor* InLeadStorm)
{
	Setup = InSetup;
	LeadStorm = InLeadStorm;

	// Here and not in BeginPlay, for the reason ARoomDressingActor::Configure gives: the motes
	// are scattered in the component's own BeginPlay, which runs before the owner's.
	const float EastFaceX = EastFace();
	DustMotes->ConfigureVolume(
		FVector((EastFaceX - WestFace) * 0.48f, ClearWidth * 0.46f, Setup.Height * 0.46f),
		FVector((EastFaceX + WestFace) * 0.5f, CenterY(), Setup.Height * 0.5f));

	const float DoorHalf = HallDoorWidth * 0.5f;
	Openings = {
		{ ESide::North, Setup.StartDoorCenterX, Setup.StartDoorWidth * 0.5f, Setup.StartDoorHeight },
		{ ESide::North, -690.f, DoorHalf, HallDoorHeight },
		{ ESide::North, -1060.f, DoorHalf, HallDoorHeight },
		{ ESide::South, -40.f, DoorHalf, HallDoorHeight },
		{ ESide::South, -470.f, DoorHalf, HallDoorHeight },
		{ ESide::South, -880.f, DoorHalf, HallDoorHeight },
		{ ESide::East, CenterY(), WindowWidth * 0.5f, WindowTop, WindowSill },
		{ ESide::West, CenterY(), StairOpeningWidth * 0.5f, StairOpeningHeight },
	};
}

void ACorridorActor::BeginPlay()
{
	Super::BeginPlay();

	// Its own stream, so tuning the corridor never relays the bedroom (see the clock's note).
	Random.Initialize(18950411);

	FRoomBuilder Build(this, CorridorRoot);
	CacheMaterials(Build);

	BuildShell(Build);
	BuildBackRooms(Build);
	BuildStairs(Build);
	BuildFloor(Build);
	BuildRunner(Build);
	BuildWallFinish(Build);
	BuildWainscot(Build);
	BuildDoorCasings(Build);
	BuildCeiling(Build);
	BuildDamage(Build);
	BuildPortraits(Build);
	BuildFurniture(Build);
	BuildDebris(Build);
	BuildFigures(Build);

	SpawnDoors();
	SpawnWindow();
	BuildClues();
}

void ACorridorActor::CacheMaterials(FRoomBuilder& Build)
{
	// The bedroom's tints, for the bedroom's reasons: everything held around a tenth to a fifth
	// reflectance so the lantern is the brightest thing in frame, and everything warm, because the
	// only fill up here is a cold rectangle of sky at the end of the corridor.
	MatPlaster = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.150f, 0.142f, 0.130f));
	// decrepit_wallpaper is a pale printed paper; held well under the plaster's value so that what
	// is left of it reads as darkened paper and not as the cleanest surface in the house.
	// First pass was 0.30 and the paper came out the palest surface on the landing, its printed
	// veins reading as a map drawn on the wall; this is roughly the plaster's value.
	MatWallpaper = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.175f, 0.148f, 0.120f));
	MatWallpaperDark = Build.Surface(RoomSurfaces::Wallpaper, FLinearColor(0.135f, 0.112f, 0.092f));
	MatPanel = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.300f, 0.300f, 0.300f));
	MatPanelField = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.235f, 0.235f, 0.240f));
	MatTrim = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.370f, 0.370f, 0.360f));
	MatCeiling = Build.Surface(RoomSurfaces::Ceiling, FLinearColor(0.34f, 0.33f, 0.30f));
	MatFloorboards = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.62f, 0.59f, 0.55f));
	MatFloorboardsWorn = Build.Surface(RoomSurfaces::Floorboards, FLinearColor(0.40f, 0.36f, 0.32f));
	MatRoughWood = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.402f, 0.409f, 0.391f));
	// The runner. rough_linen is a blue photograph (see the bedroom's paper note), so a red that
	// stays red has to take nearly all of the blue out: a Turkey-red runner gone to brown.
	MatCarpet = Build.Surface(RoomSurfaces::Drapery, FLinearColor(0.330f, 0.075f, 0.036f));
	MatRubble = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.145f, 0.127f, 0.110f));
	MatPaper = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.464f, 0.245f, 0.108f));
	MatPaperDamp = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.260f, 0.142f, 0.065f));
	MatCloth = Build.Surface(RoomSurfaces::Linen, FLinearColor(0.183f, 0.101f, 0.047f));
	MatIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatBrass = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatGlass = Build.Glass(RoomPalette::GlassShard, 0.28f, 0.06f);
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	// Behind the doors. Just enough surface for the lantern to find a wall, never enough to see one.
	MatBackRoom = Build.Surface(RoomSurfaces::Plaster, FLinearColor(0.035f, 0.032f, 0.029f));
	// The shell under every finish. Only ever seen through a gap, so it must not be paler than
	// whatever the gap is in (the bedroom's grey-board lesson).
	MatShell = Build.Flat(FLinearColor(0.012f, 0.008f, 0.005f), 1.f);
	MatWax = Build.Flat(FLinearColor(0.34f, 0.30f, 0.22f), 0.55f);
	MatDial = Build.Flat(FLinearColor(0.15f, 0.14f, 0.12f), 0.7f);
	// The mirror is dark polished glass, not metal: Flat's base (BasicShapeMaterial) has no
	// Metallic input, so the metal it was given was ignored and the glass came out a glossy pale
	// panel lit by the lantern like the plaster beside it. Near-black and nearly polished, what it
	// gives back is the specular — the lantern's flame, and faintly the lit wall behind the
	// player. The dust is projected over it, not baked into it.
	MatMirror = Build.Flat(FLinearColor(0.03f, 0.03f, 0.028f), 0.04f, 1.f);
	MatStem = Build.Flat(FLinearColor(0.045f, 0.036f, 0.020f), 0.95f);
	MatPetal = Build.Flat(FLinearColor(0.090f, 0.030f, 0.026f), 0.9f);
	// The figures at the end of the corridor are not lit things. They are where the light is not.
	MatShadow = Build.Flat(FLinearColor(0.002f, 0.002f, 0.002f), 1.f);
}

// ---------------------------------------------------------------------------------------------
// Wall-space helpers. U runs along a wall (X on north/south, Y on east/west), V runs up it.
// ---------------------------------------------------------------------------------------------

void ACorridorActor::SideRange(ESide Side, float& OutU0, float& OutU1) const
{
	if (Side == ESide::North || Side == ESide::South)
	{
		OutU0 = WestFace;
		OutU1 = EastFace();
	}
	else
	{
		OutU0 = NorthFace();
		OutU1 = SouthFace();
	}
}

bool ACorridorActor::IsOnOpening(ESide Side, float U, float V, float HalfU, float HalfV) const
{
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Side == Side
			&& FMath::Abs(U - Opening.CenterU) < Opening.HalfU + HalfU
			&& V > Opening.BottomV - HalfV && V < Opening.TopV + HalfV)
		{
			return true;
		}
	}
	return false;
}

namespace
{
	/** The rectangle [U0,U1] x [V0,V1] with every opening on the wall cut out of it. */
	template <typename TOpening, typename TSide>
	TArray<FBox2D> CutAround(const TArray<TOpening>& Openings, TSide Side, float U0, float U1, float V0, float V1)
	{
		TArray<const TOpening*> Hits;
		for (const TOpening& Opening : Openings)
		{
			if (Opening.Side == Side
				&& Opening.CenterU + Opening.HalfU > U0 && Opening.CenterU - Opening.HalfU < U1
				&& Opening.TopV > V0 && Opening.BottomV < V1)
			{
				Hits.Add(&Opening);
			}
		}
		Hits.Sort([](const TOpening& A, const TOpening& B) { return A.CenterU < B.CenterU; });

		TArray<FBox2D> Pieces;
		float Cursor = U0;
		for (const TOpening* Opening : Hits)
		{
			const float Left = FMath::Max(U0, Opening->CenterU - Opening->HalfU);
			const float Right = FMath::Min(U1, Opening->CenterU + Opening->HalfU);
			if (Left > Cursor)
			{
				Pieces.Add(FBox2D(FVector2D(Cursor, V0), FVector2D(Left, V1)));
			}
			const float HoleBottom = FMath::Max(V0, Opening->BottomV);
			const float HoleTop = FMath::Min(V1, Opening->TopV);
			if (HoleBottom > V0)
			{
				Pieces.Add(FBox2D(FVector2D(Left, V0), FVector2D(Right, HoleBottom)));
			}
			if (HoleTop < V1)
			{
				Pieces.Add(FBox2D(FVector2D(Left, HoleTop), FVector2D(Right, V1)));
			}
			Cursor = FMath::Max(Cursor, Right);
		}
		if (Cursor < U1)
		{
			Pieces.Add(FBox2D(FVector2D(Cursor, V0), FVector2D(U1, V1)));
		}
		return Pieces;
	}
}

void ACorridorActor::FacePanel(FRoomBuilder& Build, ESide Side, float U, float V, float SizeU, float SizeV, UMaterialInterface* Mat) const
{
	// A Mark stands proud along its own +Z, and a roll of R turns +Z to (0, sin R, cos R): +90
	// faces +Y, out of the north wall, and -90 faces -Y. The other way round the panel is pushed
	// into the wall it is laid on and never draws — which is how the first pass showed the bare
	// shell everywhere the plaster and the paper should have been.
	switch (Side)
	{
	case ESide::North: Build.Mark(FVector(U, NorthFace(), V), FRotator(0.f, 0.f, 90.f), FVector2D(SizeU, SizeV), Mat); break;
	case ESide::South: Build.Mark(FVector(U, SouthFace(), V), FRotator(0.f, 0.f, -90.f), FVector2D(SizeU, SizeV), Mat); break;
	case ESide::East:  Build.Mark(FVector(EastFace(), U, V), FRotator(90.f, 0.f, 0.f), FVector2D(SizeV, SizeU), Mat); break;
	default:           Build.Mark(FVector(WestFace, U, V), FRotator(-90.f, 0.f, 0.f), FVector2D(SizeV, SizeU), Mat); break;
	}
}

void ACorridorActor::FaceFill(FRoomBuilder& Build, ESide Side, float U0, float U1, float V0, float V1, UMaterialInterface* Mat) const
{
	for (const FBox2D& Piece : CutAround(Openings, Side, U0, U1, V0, V1))
	{
		const FVector2D Centre = Piece.GetCenter();
		const FVector2D Size = Piece.GetSize();
		if (Size.X > 0.5f && Size.Y > 0.5f)
		{
			FacePanel(Build, Side, Centre.X, Centre.Y, Size.X, Size.Y, Mat);
		}
	}
}

void ACorridorActor::AimAt(ESide Side, float U, float V, float Roll, FVector& OutLocation, FRotator& OutRotation) const
{
	// A decal projects along its own +X, so this is the direction *into* the wall.
	switch (Side)
	{
	case ESide::North: OutLocation = FVector(U, NorthFace() + 2.f, V); OutRotation = FRotator(0.f, -90.f, Roll); break;
	case ESide::South: OutLocation = FVector(U, SouthFace() - 2.f, V); OutRotation = FRotator(0.f, 90.f, Roll); break;
	case ESide::East:  OutLocation = FVector(EastFace() - 2.f, U, V); OutRotation = FRotator(0.f, 0.f, Roll); break;
	default:           OutLocation = FVector(WestFace + 2.f, U, V); OutRotation = FRotator(0.f, 180.f, Roll); break;
	}
}

namespace
{
	/** Unit vector out of a wall, into the corridor. */
	FVector SideNormal(int32 SideIndex)
	{
		switch (SideIndex)
		{
		case 0: return FVector(0.f, 1.f, 0.f);
		case 1: return FVector(0.f, -1.f, 0.f);
		case 2: return FVector(-1.f, 0.f, 0.f);
		default: return FVector(1.f, 0.f, 0.f);
		}
	}
}

// ---------------------------------------------------------------------------------------------

void ACorridorActor::BuildShell(FRoomBuilder& Build)
{
	const float T = Setup.WallThickness;
	const float H = Setup.Height;
	const float EastX = EastFace();
	const float WestOuter = WestFace - T;

	// Solid, colliding, and never seen: every face the player looks at is a finish laid over this.
	// The north wall only needs building west of the bedroom — the bedroom's own door wall is the
	// rest of it, and its corridor face is exactly NorthFace.
	auto Run = [&](ESide Side, float U0, float U1, float Line)
	{
		for (const FBox2D& Piece : CutAround(Openings, Side, U0, U1, -17.f, H + 10.f))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			if (Side == ESide::North || Side == ESide::South)
			{
				Build.Box(FVector(C.X, Line, C.Y), FRotator::ZeroRotator, FVector(S.X, T, S.Y), MatShell);
			}
			else
			{
				Build.Box(FVector(Line, C.X, C.Y), FRotator::ZeroRotator, FVector(T, S.X, S.Y), MatShell);
			}
		}
	};

	Run(ESide::North, WestOuter, -Setup.RoomHalfWidth, NorthFace() - T * 0.5f);
	Run(ESide::South, WestOuter, EastX + T, SouthFace() + T * 0.5f);
	Run(ESide::East, NorthFace() - T, SouthFace() + T, EastX + T * 0.5f);
	Run(ESide::West, NorthFace() - T, SouthFace() + T, WestFace - T * 0.5f);

	// Floor, cut round the hole, and the ceiling. The floor starts under the bedroom's door wall so
	// the threshold has something under it.
	const float FloorY0 = NorthFace() - T;
	const float FloorY1 = SouthFace() + T;
	auto FloorPiece = [&](float X0, float X1, float Y0, float Y1)
	{
		Build.Box(FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, -12.f), FRotator::ZeroRotator, FVector(X1 - X0, Y1 - Y0, 10.f), MatShell);
	};
	FloorPiece(WestOuter, FloorHole.Min.X, FloorY0, FloorY1);
	FloorPiece(FloorHole.Max.X, EastX + T, FloorY0, FloorY1);
	FloorPiece(FloorHole.Min.X, FloorHole.Max.X, FloorY0, FloorHole.Min.Y);
	FloorPiece(FloorHole.Min.X, FloorHole.Max.X, FloorHole.Max.Y, FloorY1);

	Build.Box(FVector((WestOuter + EastX + T) * 0.5f, (NorthFace() + FloorY1) * 0.5f, H + 5.f), FRotator::ZeroRotator,
		FVector(EastX + T - WestOuter, FloorY1 - NorthFace(), 10.f), MatShell);

	// The hole goes down to the ceiling void of the room below: a dark pit, joists across it, and
	// a floor at the bottom so a detective who steps in stumbles rather than falls out of the world.
	const FVector2D HoleC = FloorHole.GetCenter();
	const FVector2D HoleS = FloorHole.GetSize();
	Build.Box(FVector(HoleC.X, HoleC.Y, -66.f), FRotator::ZeroRotator, FVector(HoleS.X, HoleS.Y, 10.f), MatVoid);
	Build.Box(FVector(FloorHole.Min.X + 0.5f, HoleC.Y, -36.f), FRotator::ZeroRotator, FVector(1.f, HoleS.Y, 60.f), MatVoid, false);
	Build.Box(FVector(FloorHole.Max.X - 0.5f, HoleC.Y, -36.f), FRotator::ZeroRotator, FVector(1.f, HoleS.Y, 60.f), MatVoid, false);
	Build.Box(FVector(HoleC.X, FloorHole.Max.Y - 0.5f, -36.f), FRotator::ZeroRotator, FVector(HoleS.X, 1.f, 60.f), MatVoid, false);
	Build.Box(FVector(HoleC.X, FloorHole.Min.Y + 0.5f, -36.f), FRotator::ZeroRotator, FVector(HoleS.X, 1.f, 60.f), MatVoid, false);
	for (const float JoistX : { FloorHole.Min.X + 24.f, FloorHole.Max.X - 30.f })
	{
		Build.Box(FVector(JoistX, HoleC.Y, -20.f), FRotator(0.f, 0.f, JoistX < HoleC.X ? 0.f : 3.f), FVector(10.f, HoleS.Y + 4.f, 18.f), MatRoughWood, false);
	}

	// The bedroom threshold, bridging its floor and this one.
	Build.Box(FVector(Setup.StartDoorCenterX, NorthFace() - T * 0.5f, 0.f), FRotator::ZeroRotator,
		FVector(Setup.StartDoorWidth, T + 4.f, 6.f), MatRoughWood);
}

void ACorridorActor::BuildBackRooms(FRoomBuilder& Build)
{
	// Behind every hall door, a room the corridor never lets you into. Built at all only because
	// the ajar doors and the hole in one leaf look into it: what they have to show is a floor
	// that goes on and a wall a long way off, both nearly black. Nothing in them.
	const float T = Setup.WallThickness;
	const float H = Setup.Height;
	for (const FOpening& Opening : Openings)
	{
		if ((Opening.Side != ESide::North && Opening.Side != ESide::South) || Opening.CenterU == Setup.StartDoorCenterX)
		{
			continue;
		}

		const float Dir = Opening.Side == ESide::North ? -1.f : 1.f;
		const float Front = Opening.Side == ESide::North ? NorthFace() - T : SouthFace() + T;
		const float Depth = 300.f;
		const float Back = Front + Dir * Depth;
		const float MidY = (Front + Back) * 0.5f;
		const float X0 = Opening.CenterU - 170.f;
		const float X1 = Opening.CenterU + 170.f;

		Build.Box(FVector(Opening.CenterU, MidY, -5.f), FRotator::ZeroRotator, FVector(X1 - X0, Depth, 10.f), MatFloorboardsWorn);
		Build.Box(FVector(Opening.CenterU, MidY, H + 5.f), FRotator::ZeroRotator, FVector(X1 - X0, Depth, 10.f), MatBackRoom);
		Build.Box(FVector(Opening.CenterU, Back + Dir * 5.f, H * 0.5f), FRotator::ZeroRotator, FVector(X1 - X0, 10.f, H), MatBackRoom);
		Build.Box(FVector(X0 - 5.f, MidY, H * 0.5f), FRotator::ZeroRotator, FVector(10.f, Depth, H), MatBackRoom);
		Build.Box(FVector(X1 + 5.f, MidY, H * 0.5f), FRotator::ZeroRotator, FVector(10.f, Depth, H), MatBackRoom);
	}
}

void ACorridorActor::BuildStairs(FRoomBuilder& Build)
{
	// The stairs down, at the far west end, through an opening in the end wall. The man in the
	// story comes up these; the detective does not go down them. The flight turns out of sight
	// into the dark below a landing, and the opening is boarded across from this side.
	const float T = Setup.WallThickness;
	const float H = Setup.Height;
	const float Y = CenterY();
	const float HalfW = StairOpeningWidth * 0.5f;
	const float Behind = WestFace - T;

	// Landing, then ten steps descending westward.
	Build.Box(FVector(Behind - 35.f, Y, -5.f), FRotator::ZeroRotator, FVector(70.f, StairOpeningWidth + 30.f, 10.f), MatFloorboardsWorn);
	const float Rise = 18.f;
	const float Going = 27.f;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		const float Top = -Rise * (Step + 1);
		const float X = Behind - 70.f - Going * (Step + 0.5f);
		Build.Box(FVector(X, Y, Top - 5.f), FRotator::ZeroRotator, FVector(Going + 2.f, StairOpeningWidth + 10.f, 10.f), MatFloorboardsWorn);
		// The risers, a shade darker, so each step has an edge where the lantern catches it.
		Build.Box(FVector(X + Going * 0.5f, Y, Top - Rise * 0.5f), FRotator::ZeroRotator, FVector(2.f, StairOpeningWidth + 10.f, Rise), MatPanelField, false);
	}

	// The well around it.
	const float WellX0 = Behind - 420.f;
	const float WellMidX = (Behind + WellX0) * 0.5f;
	const float WellLength = Behind - WellX0;
	const float Bottom = -Rise * 10.f - 80.f;
	const float WallH = H - Bottom;
	Build.Box(FVector(WellMidX, Y - HalfW - 20.f, Bottom + WallH * 0.5f), FRotator::ZeroRotator, FVector(WellLength, 10.f, WallH), MatBackRoom);
	Build.Box(FVector(WellMidX, Y + HalfW + 20.f, Bottom + WallH * 0.5f), FRotator::ZeroRotator, FVector(WellLength, 10.f, WallH), MatBackRoom);
	Build.Box(FVector(WellX0 - 5.f, Y, Bottom + WallH * 0.5f), FRotator::ZeroRotator, FVector(10.f, StairOpeningWidth + 50.f, WallH), MatBackRoom);
	Build.Box(FVector(WellMidX, Y, H + 5.f), FRotator::ZeroRotator, FVector(WellLength, StairOpeningWidth + 50.f, 10.f), MatBackRoom);
	Build.Box(FVector(WellMidX, Y, Bottom - 5.f), FRotator::ZeroRotator, FVector(WellLength, StairOpeningWidth + 50.f, 10.f), MatVoid);

	// Handrail down the south side, on turned balusters, and a newel at the landing.
	const float RailY = Y + HalfW - 6.f;
	Build.Box(FVector(Behind - 64.f, RailY, 55.f), FRotator::ZeroRotator, FVector(11.f, 11.f, 110.f), MatTrim, false);
	Build.Sph(FVector(Behind - 64.f, RailY, 114.f), 12.f, MatTrim);
	const FVector RailFrom(Behind - 64.f, RailY, 92.f);
	const FVector RailTo(Behind - 70.f - Going * 10.f, RailY, 92.f - Rise * 10.f);
	const FVector RailDir = (RailTo - RailFrom).GetSafeNormal();
	Build.Cyl((RailFrom + RailTo) * 0.5f, FRotationMatrix::MakeFromZ(RailDir).Rotator(), FVector(6.f, 6.f, (RailTo - RailFrom).Size()), MatTrim, false);
	for (int32 Step = 0; Step < 10; Step += 1)
	{
		const float X = Behind - 70.f - Going * (Step + 0.5f);
		const float Tread = -Rise * (Step + 1);
		const float RailZ = FMath::Lerp(RailFrom.Z, RailTo.Z, (RailFrom.X - X) / (RailFrom.X - RailTo.X));
		// Two of them are gone; a banister with every baluster present is a new banister.
		if (Step == 3 || Step == 7)
		{
			continue;
		}
		Build.Cyl(FVector(X, RailY, (Tread + RailZ) * 0.5f), FRotator::ZeroRotator, FVector(3.2f, 3.2f, RailZ - Tread), MatTrim, false);
	}

	// Boarded over, from this side: seven planks nailed across the opening at whatever angle the
	// next one would go on at. Only in-plane rotation (roll, about the wall's normal) — a plank
	// turned any other way stands off the wall at one end.
	struct FPlank { float V; float Roll; float Length; float Height; };
	const FPlank Planks[] = {
		{ 30.f, 4.f, 190.f, 16.f }, { 62.f, -6.f, 186.f, 14.f }, { 96.f, 2.f, 194.f, 18.f },
		{ 128.f, 18.f, 200.f, 15.f }, { 152.f, -3.f, 188.f, 16.f }, { 190.f, -14.f, 196.f, 14.f },
		{ 224.f, 5.f, 184.f, 17.f },
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Planks); ++i)
	{
		const FPlank& P = Planks[i];
		const float X = WestFace + 1.8f + (i % 2) * 2.2f;
		Build.Box(FVector(X, Y + (i % 3 - 1) * 4.f, P.V), FRotator(0.f, 0.f, P.Roll), FVector(2.2f, P.Length, P.Height),
			(i % 3 == 0) ? MatPanelField : MatRoughWood);
		const float Reach = P.Length * 0.5f - 9.f;
		for (const float Side : { -1.f, 1.f })
		{
			const float RollRad = FMath::DegreesToRadians(P.Roll);
			const FVector Nail(X + 1.4f, Y + (i % 3 - 1) * 4.f + Side * Reach * FMath::Cos(RollRad), P.V + Side * Reach * FMath::Sin(RollRad));
			Build.Sph(Nail, 1.4f, MatIron);
		}
	}

	// And whatever the boards do not stop, this does.
	if (UStaticMeshComponent* Blocker = Build.Box(FVector(WestFace - T * 0.5f, Y, StairOpeningHeight * 0.5f), FRotator::ZeroRotator,
		FVector(T, StairOpeningWidth, StairOpeningHeight), MatVoid))
	{
		Blocker->SetHiddenInGame(true);
		Blocker->SetCastShadow(false);
	}
}

void ACorridorActor::BuildFloor(FRoomBuilder& Build)
{
	const float X0 = WestFace;
	const float X1 = EastFace();
	const float BoardWidth = 21.f;
	const int32 Rows = FMath::RoundToInt(ClearWidth / BoardWidth);

	// Runs along the corridor, as a landing is boarded: the boards lead the eye down it. Each row
	// is laid from random lengths so no two joints line up, and the rows over the hole are cut at
	// its edges rather than skipped whole.
	auto CutByHole = [&](float Y, float A, float B, TArray<FVector2D>& Out)
	{
		const bool bCrosses = Y > FloorHole.Min.Y && Y < FloorHole.Max.Y;
		if (!bCrosses || B <= FloorHole.Min.X || A >= FloorHole.Max.X)
		{
			Out.Add(FVector2D(A, B));
			return;
		}
		if (FloorHole.Min.X - A > 12.f)
		{
			Out.Add(FVector2D(A, FloorHole.Min.X));
		}
		if (B - FloorHole.Max.X > 12.f)
		{
			Out.Add(FVector2D(FloorHole.Max.X, B));
		}
	};

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float Y = NorthFace() + BoardWidth * (Row + 0.5f);

		// The subfloor, half a board over and the same way, as in the bedroom.
		{
			TArray<FVector2D> Spans;
			CutByHole(Y + BoardWidth * 0.5f, X0, X1, Spans);
			for (const FVector2D& Span : Spans)
			{
				if (Row < Rows - 1)
				{
					Build.Box(FVector((Span.X + Span.Y) * 0.5f, Y + BoardWidth * 0.5f, -4.f), FRotator::ZeroRotator,
						FVector(Span.Y - Span.X, BoardWidth - 1.f, 6.f), MatFloorboardsWorn, false);
				}
			}
		}

		float Cursor = X0;
		while (Cursor < X1 - 1.f)
		{
			const float Length = FMath::Min(Random.FRandRange(150.f, 420.f), X1 - Cursor);
			TArray<FVector2D> Spans;
			CutByHole(Y, Cursor, Cursor + Length, Spans);
			for (const FVector2D& Span : Spans)
			{
				const float SpanLength = Span.Y - Span.X;
				if (SpanLength < 16.f || Random.FRand() < 0.04f)
				{
					continue; // one in twenty-five simply gone
				}
				// Warp held under the bedroom's: the runner lies over these and must clear them.
				const float Lift = Random.FRandRange(-0.8f, 1.0f);
				UStaticMeshComponent* Board = Build.Box(
					FVector((Span.X + Span.Y) * 0.5f, Y, Lift),
					FRotator(Random.FRandRange(-0.4f, 0.4f), 0.f, Random.FRandRange(-1.2f, 1.2f)),
					FVector(SpanLength - 1.5f, BoardWidth - 1.5f, 6.f),
					Random.FRand() < 0.3f ? MatFloorboardsWorn.Get() : MatFloorboards.Get());
				if (Board && Random.FRand() < 0.04f && (Y < CenterY() - 50.f || Y > CenterY() + 50.f))
				{
					Board->AddRelativeRotation(FRotator(Random.FRandRange(1.f, 2.5f), 0.f, 0.f));
				}
			}
			Cursor += Length;
		}
	}

	// Broken board ends at the lip of the hole, snapped and hanging into it.
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Y = NorthFace() + BoardWidth * (Row + 0.5f);
		const float Snap = Random.FRandRange(18.f, 34.f);
		Build.Box(FVector(FloorHole.Min.X + Snap * 0.45f, Y, -4.f), FRotator(-Random.FRandRange(10.f, 24.f), 0.f, Random.FRandRange(-4.f, 4.f)),
			FVector(Snap, BoardWidth - 2.f, 5.f), MatFloorboardsWorn, false);
		if (Row != 1)
		{
			const float Other = Random.FRandRange(14.f, 26.f);
			Build.Box(FVector(FloorHole.Max.X - Other * 0.45f, Y, -3.f), FRotator(Random.FRandRange(8.f, 18.f), 0.f, 0.f),
				FVector(Other, BoardWidth - 2.f, 5.f), MatFloorboards, false);
		}
	}

	// Dust along the walls, where it gathers — projected, never laid (the bedroom's dust note).
	const FLinearColor DustTint(0.42f, 0.40f, 0.36f);
	for (int32 i = 0; i < 18; ++i)
	{
		const bool bNorth = Random.FRand() < 0.5f;
		const FVector2D Spot(Random.FRandRange(X0 + 40.f, X1 - 40.f),
			bNorth ? NorthFace() + Random.FRandRange(10.f, 36.f) : SouthFace() - Random.FRandRange(10.f, 36.f));
		if (FloorHole.IsInside(Spot))
		{
			continue;
		}
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, 10.f), FRotator(-90.f, 0.f, Random.FRandRange(-20.f, 20.f)),
			FVector2D(Random.FRandRange(60.f, 140.f), Random.FRandRange(22.f, 40.f)), DustTint, Random.FRandRange(0.22f, 0.36f), 1.35f);
	}

	// Old stains on the boards. Dark, wide, and nobody's business yet.
	const FVector2D StainSpots[] = { { -150.f, 505.f }, { -610.f, 372.f }, { -985.f, 512.f }, { 120.f, 360.f } };
	for (const FVector2D& Spot : StainSpots)
	{
		Build.Stain(RoomSurfaces::Damp, FVector(Spot.X, Spot.Y, 10.f), FRotator(-90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(50.f, 90.f), Random.FRandRange(34.f, 60.f)), FLinearColor(0.20f, 0.13f, 0.09f),
			Random.FRandRange(0.55f, 0.75f), 1.2f, 0.5f);
	}
}

void ACorridorActor::BuildRunner(FRoomBuilder& Build)
{
	// A runner down the middle, in lengths, because it has rotted into lengths: each piece is its
	// own cloth with its own frayed ends and its own holes. Generated for the curtains' reason — a
	// torn edge is an outline, and a box has none.
	//
	// It stops short of the hole in the floor by more than a runner's width: that bit of it went
	// through with the boards.
	const float Width = 88.f;
	const float Z = 5.f;
	const FVector2D Pieces[] = { { -1190.f, -905.f }, { -884.f, -575.f }, { -552.f, -236.f }, { -214.f, 86.f }, { 104.f, 336.f } };
	int32 Seed = 311;
	for (const FVector2D& Piece : Pieces)
	{
		const float Length = Piece.Y - Piece.X;
		Build.Cloth(FVector((Piece.X + Piece.Y) * 0.5f, CenterY() + Random.FRandRange(-4.f, 4.f), Z),
			FRotator(0.f, Random.FRandRange(-1.2f, 1.2f), 0.f), FVector2D(Length, Width), 0.7f, 0.f, Seed++, MatCarpet, 30.f);
	}

	// Worn to the weft down the middle and darkest in front of the bedroom door: the one place on
	// this floor somebody kept coming back to.
	for (int32 i = 0; i < 9; ++i)
	{
		const float X = FMath::Lerp(-1100.f, 300.f, i / 8.f) + Random.FRandRange(-40.f, 40.f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, CenterY(), 14.f), FRotator(-90.f, 0.f, 90.f + Random.FRandRange(-8.f, 8.f)),
			FVector2D(Random.FRandRange(30.f, 50.f), Random.FRandRange(90.f, 160.f)), FLinearColor(0.30f, 0.26f, 0.22f), 0.35f, 1.2f);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(Setup.StartDoorCenterX, CenterY() - 20.f, 14.f), FRotator(-90.f, 0.f, 0.f),
		FVector2D(80.f, 120.f), FLinearColor(0.18f, 0.15f, 0.12f), 0.6f, 1.2f);
}

void ACorridorActor::BuildWallFinish(FRoomBuilder& Build)
{
	const float H = Setup.Height;

	// Plaster over every face, full height. The wainscot and the paper go on top of it.
	for (const ESide Side : { ESide::North, ESide::South, ESide::East, ESide::West })
	{
		float U0, U1;
		SideRange(Side, U0, U1);
		FaceFill(Build, Side, U0, U1, 0.f, H, MatPlaster);
	}

	// What is left of the paper, between the chair rail and the picture rail. It survives in a
	// few runs of adjacent strips on each long wall — paper comes off in sheets, not in stripes
	// (the bedroom's 09-18 wallpaper note) — and a strip is laid proud of the plaster by only a
	// millimetre or two so the projected damage still reaches it.
	const float StripWidth = 53.f;
	for (const ESide Side : { ESide::North, ESide::South })
	{
		const FVector Normal = SideNormal(static_cast<int32>(Side));
		float U0, U1;
		SideRange(Side, U0, U1);
		const int32 StripCount = FMath::FloorToInt((U1 - U0) / StripWidth);

		// Runs as [first strip, count]: written out, not rolled, so the paper is gone from the
		// places it should be gone from — above all the stretch round the bedroom door, where the
		// bare wall is part of what makes that door look like it matters.
		const FIntPoint Runs[2][3] = {
			{ { 1, 5 }, { 9, 4 }, { 16, 6 } },
			{ { 2, 4 }, { 11, 7 }, { 23, 4 } },
		};
		for (const FIntPoint& Run : Runs[Side == ESide::North ? 0 : 1])
		{
			for (int32 Strip = Run.X; Strip < FMath::Min(Run.X + Run.Y, StripCount); ++Strip)
			{
				const float A = U0 + Strip * StripWidth;
				// Each strip hangs from its own top, a few centimetres short or long of the next:
				// the bottom edges never line up, and the top is under the picture rail anyway.
				const float Bottom = ChairRail + 2.f + Random.FRandRange(0.f, 26.f);
				UMaterialInterface* Mat = (Strip % 2 == 0) ? MatWallpaper.Get() : MatWallpaperDark.Get();
				for (const FBox2D& Piece : CutAround(Openings, Side, A, A + StripWidth - 0.3f, Bottom, PictureRail))
				{
					const FVector2D C = Piece.GetCenter();
					const FVector2D S = Piece.GetSize();
					FVector Location = (Side == ESide::North) ? FVector(C.X, NorthFace(), C.Y) : FVector(C.X, SouthFace(), C.Y);
					Location += Normal * (0.25f + (Strip % 2) * 0.12f);
					Build.Mark(Location, FRotator(0.f, 0.f, Side == ESide::North ? 90.f : -90.f), S, Mat);
				}
			}

			// The last strip of each run has come away at the top and hangs off the wall: a
			// generated sheet so its torn edge is a torn edge.
			const float PeelU = U0 + (Run.X + Run.Y) * StripWidth + StripWidth * 0.5f;
			// Never over anything hung on the wall: the mirror, the turned portrait, the family.
			bool bOverSomething = (Side == ESide::South && FMath::Abs(PeelU - Setup.StartDoorCenterX) < 80.f)
				|| (Side == ESide::North && FMath::Abs(PeelU - TurnedPortraitU) < 80.f);
			for (const FPortrait& P : Portraits)
			{
				bOverSomething |= (P.bNorth == (Side == ESide::North)) && FMath::Abs(PeelU - P.U) < P.Size * 0.4f + 40.f;
			}
			if (PeelU < U1 - 60.f && !bOverSomething && !IsOnOpening(Side, PeelU, 180.f, 35.f, 80.f))
			{
				const float PeelV = Random.FRandRange(150.f, 190.f);
				const FVector Base = (Side == ESide::North) ? FVector(PeelU, NorthFace(), PeelV) : FVector(PeelU, SouthFace(), PeelV);
				Build.Cloth(Base + Normal * 7.f, FRotator(0.f, 0.f, (Side == ESide::North ? -90.f : 90.f) + Random.FRandRange(-9.f, 9.f)),
					FVector2D(StripWidth - 6.f, Random.FRandRange(55.f, 90.f)), 3.5f, 0.f, 900 + Run.X, MatWallpaperDark, 53.f);
			}
		}
	}
}

void ACorridorActor::BuildWainscot(FRoomBuilder& Build)
{
	const float H = Setup.Height;

	// A box laid on a wall, standing Depth proud of it from ProudBase outwards.
	auto FaceBox = [&](ESide Side, float U, float V, float SizeU, float SizeV, float Depth, float ProudBase, UMaterialInterface* Mat)
	{
		const FVector Normal = SideNormal(static_cast<int32>(Side));
		FVector Location;
		FVector Size;
		switch (Side)
		{
		case ESide::North: Location = FVector(U, NorthFace(), V); Size = FVector(SizeU, Depth, SizeV); break;
		case ESide::South: Location = FVector(U, SouthFace(), V); Size = FVector(SizeU, Depth, SizeV); break;
		case ESide::East:  Location = FVector(EastFace(), U, V); Size = FVector(Depth, SizeU, SizeV); break;
		default:           Location = FVector(WestFace, U, V); Size = FVector(Depth, SizeU, SizeV); break;
		}
		Build.Box(Location + Normal * (ProudBase + Depth * 0.5f), FRotator::ZeroRotator, Size, Mat, false);
	};

	auto FaceBoxRun = [&](ESide Side, float U0, float U1, float V0, float V1, float Depth, float ProudBase, UMaterialInterface* Mat)
	{
		for (const FBox2D& Piece : CutAround(Openings, Side, U0, U1, V0, V1))
		{
			const FVector2D C = Piece.GetCenter();
			const FVector2D S = Piece.GetSize();
			if (S.X > 1.f)
			{
				FaceBox(Side, C.X, C.Y, S.X, S.Y, Depth, ProudBase, Mat);
			}
		}
	};

	for (const ESide Side : { ESide::North, ESide::South, ESide::East, ESide::West })
	{
		float U0, U1;
		SideRange(Side, U0, U1);

		// Cornice and picture rail on every wall: the two lines that make a corridor Victorian
		// before anything in it is, and the only straight lines left up there.
		FaceBoxRun(Side, U0, U1, H - 14.f, H, 12.f, 0.f, MatTrim);
		FaceBoxRun(Side, U0, U1, H - 24.f, H - 14.f, 6.f, 0.f, MatTrim);
		FaceBoxRun(Side, U0, U1, PictureRail - 2.f, PictureRail + 2.f, 2.5f, 0.f, MatTrim);

		if (Side != ESide::North && Side != ESide::South)
		{
			continue;
		}

		// Panelling to the chair rail on the long walls: a backing board, a skirting rail, a chair
		// rail, and stiles every sixty-odd centimetres with a sunk field between each pair.
		FaceBoxRun(Side, U0, U1, 0.f, ChairRail, 1.2f, 0.f, MatPanel);
		FaceBoxRun(Side, U0, U1, 0.f, 15.f, 1.6f, 1.2f, MatTrim);
		FaceBoxRun(Side, U0, U1, ChairRail - 4.f, ChairRail + 4.f, 3.8f, 1.2f, MatTrim);

		const float Bay = 64.f;
		for (float U = U0 + Bay * 0.5f; U < U1 - 10.f; U += Bay)
		{
			if (!IsOnOpening(Side, U, 50.f, 12.f, 40.f))
			{
				FaceBox(Side, U, (15.f + ChairRail - 4.f) * 0.5f, 7.f, ChairRail - 19.f, 1.4f, 1.2f, MatTrim);
			}
			const float FieldU = U + Bay * 0.5f;
			if (FieldU < U1 - 30.f && !IsOnOpening(Side, FieldU, 50.f, 32.f, 40.f))
			{
				// One field in six has split and dropped its face: the dark behind it is the lath.
				if (Random.FRand() < 0.16f)
				{
					FaceBox(Side, FieldU, 52.f, Bay - 16.f, ChairRail - 36.f, 0.4f, 1.3f, MatVoid);
				}
				else
				{
					FaceBox(Side, FieldU, 52.f, Bay - 16.f, ChairRail - 36.f, 0.3f, 1.3f, MatPanelField);
				}
			}
		}
	}
}

void ACorridorActor::BuildDoorCasings(FRoomBuilder& Build)
{
	// Architraves round every door on the corridor side, with plinth blocks at the foot and a
	// cornice cap over the head, and the reveals lined through the wall. The bedroom door gets the
	// same casing and nothing grander: it is important because of what is in front of it.
	for (const FOpening& Opening : Openings)
	{
		if (Opening.Side != ESide::North && Opening.Side != ESide::South)
		{
			continue;
		}
		const FVector Normal = SideNormal(static_cast<int32>(Opening.Side));
		const float Face = Opening.Side == ESide::North ? NorthFace() : SouthFace();
		auto At = [&](float U, float V, float Proud) { return FVector(U, Face, V) + Normal * Proud; };

		const float Casing = 11.f;
		const float Top = Opening.TopV;
		for (const float Side : { -1.f, 1.f })
		{
			const float U = Opening.CenterU + Side * (Opening.HalfU + Casing * 0.5f);
			Build.Box(At(U, (Top + Casing) * 0.5f, 1.4f), FRotator::ZeroRotator, FVector(Casing, 2.8f, Top + Casing), MatTrim, false);
			Build.Box(At(U, 12.f, 1.9f), FRotator::ZeroRotator, FVector(Casing + 2.f, 3.8f, 24.f), MatTrim, false);

			// Reveal lining, inside the opening and clear of the leaf.
			if (Opening.CenterU != Setup.StartDoorCenterX)
			{
				const float LineU = Opening.CenterU + Side * (Opening.HalfU - 1.f);
				const float Mid = Face - Normal.Y * (Setup.WallThickness * 0.5f + 3.f);
				Build.Box(FVector(LineU, Mid, Top * 0.5f), FRotator::ZeroRotator, FVector(2.f, Setup.WallThickness - 8.f, Top), MatPanel, false);
			}
		}
		Build.Box(At(Opening.CenterU, Top + Casing * 0.5f, 1.4f), FRotator::ZeroRotator, FVector(Opening.HalfU * 2.f + Casing * 2.f, 2.8f, Casing), MatTrim, false);
		Build.Box(At(Opening.CenterU, Top + Casing + 2.5f, 2.2f), FRotator::ZeroRotator, FVector(Opening.HalfU * 2.f + Casing * 2.f + 8.f, 4.4f, 5.f), MatTrim, false);
		if (Opening.CenterU != Setup.StartDoorCenterX)
		{
			const float Mid = Face - Normal.Y * (Setup.WallThickness * 0.5f + 3.f);
			Build.Box(FVector(Opening.CenterU, Mid, Top - 1.f), FRotator::ZeroRotator, FVector(Opening.HalfU * 2.f, Setup.WallThickness - 8.f, 2.f), MatPanel, false);
		}
	}

	// A pair of candle sconces either side of the bedroom door: brass backplate, a curved arm, a
	// drip pan and a stub of candle that went out a very long time ago.
	for (const float Side : { -1.f, 1.f })
	{
		const float U = Setup.StartDoorCenterX + Side * 96.f;
		const FVector Plate(U, NorthFace() + 0.8f, 168.f);
		Build.Box(Plate, FRotator::ZeroRotator, FVector(9.f, 1.6f, 20.f), MatBrass, false);
		Build.Cyl(Plate + FVector(0.f, 6.f, -2.f), FRotator(0.f, 0.f, -70.f), FVector(1.8f, 1.8f, 14.f), MatBrass, false);
		Build.Cyl(Plate + FVector(0.f, 12.f, 3.f), FRotator::ZeroRotator, FVector(8.f, 8.f, 1.2f), MatBrass, false);
		Build.Cyl(Plate + FVector(0.f, 12.f, 3.f + (Side < 0.f ? 3.5f : 6.f)), FRotator::ZeroRotator, FVector(2.4f, 2.4f, Side < 0.f ? 7.f : 12.f), MatWax, false);
	}
}

void ACorridorActor::BuildCeiling(FRoomBuilder& Build)
{
	const float H = Setup.Height;
	const float X0 = WestFace;
	const float X1 = EastFace();

	// The ceiling face, looking down, with a hole in it where it has come through.
	Build.Mark(FVector((X0 + X1) * 0.5f, CenterY(), H), FRotator(0.f, 0.f, 180.f), FVector2D(X1 - X0, ClearWidth), MatCeiling);
	Build.Mark(FVector(CeilingHole.X, CeilingHole.Y, H - 0.3f), FRotator(0.f, 0.f, 180.f), FVector2D(70.f, 52.f), MatVoid);
	for (int32 i = 0; i < 6; ++i)
	{
		// Lath hanging out of the hole: the strips the plaster was keyed to.
		const float Y = CeilingHole.Y - 22.f + i * 9.f;
		const float Droop = Random.FRandRange(4.f, 22.f);
		Build.Box(FVector(CeilingHole.X + Random.FRandRange(-12.f, 12.f), Y, H - 2.f - Droop * 0.4f),
			FRotator(Random.FRandRange(-18.f, -6.f), 0.f, 0.f), FVector(Random.FRandRange(30.f, 70.f), 3.f, 0.8f), MatRoughWood, false);
	}
	Build.Stain(RoomSurfaces::Substrate, FVector(CeilingHole.X, CeilingHole.Y, H - 2.f), FRotator(90.f, 0.f, 0.f),
		FVector2D(120.f, 90.f), FLinearColor(0.30f, 0.26f, 0.22f), 0.9f, 1.1f);

	// A ceiling rose and its pendant, still hanging, over the middle of the bedroom run.
	const FVector Rose(-150.f, CenterY(), H);
	Build.Cyl(Rose - FVector(0.f, 0.f, 1.5f), FRotator::ZeroRotator, FVector(52.f, 52.f, 3.f), MatTrim, false);
	Build.Cyl(Rose - FVector(0.f, 0.f, 4.f), FRotator::ZeroRotator, FVector(30.f, 30.f, 3.f), MatTrim, false);
	for (int32 Link = 0; Link < 9; ++Link)
	{
		Build.Box(Rose - FVector(0.f, 0.f, 8.f + Link * 6.f), FRotator(0.f, (Link % 2) * 90.f, 0.f), FVector(0.8f, 3.f, 6.f), MatIron, false);
	}
	Build.Add(FRoomShapes::Cone(), Rose - FVector(0.f, 0.f, 76.f), FRotator(0.f, 0.f, 4.f), FVector(36.f, 36.f, 18.f), MatBrass, false);
	Build.Sph(Rose - FVector(0.f, 0.f, 84.f), 11.f, MatGlass);

	// Water stains, spreading out from the hole and down the length of the ceiling.
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	for (int32 i = 0; i < 9; ++i)
	{
		const float X = (i < 3) ? CeilingHole.X + Random.FRandRange(-120.f, 120.f) : Random.FRandRange(X0 + 60.f, X1 - 60.f);
		Build.Stain(RoomSurfaces::Damp, FVector(X, CenterY() + Random.FRandRange(-60.f, 60.f), H - 2.f), FRotator(90.f, 0.f, Random.FRandRange(0.f, 360.f)),
			FVector2D(Random.FRandRange(90.f, 220.f), Random.FRandRange(70.f, 160.f)), DampTint * 0.7f, Random.FRandRange(0.35f, 0.6f), 1.2f);
	}
	Build.Crack(FVector(-520.f, CenterY(), H - 2.f), FRotator(90.f, 0.f, 0.f), FVector2D(260.f, 150.f), 0.8f, 18.f);
	Build.Crack(FVector(60.f, CenterY() + 30.f, H - 2.f), FRotator(90.f, 0.f, 20.f), FVector2D(180.f, 120.f), 0.7f, 22.f);

	// Cobwebs in the four ceiling corners and along the cornice, as in the bedroom: planes, never
	// boxes, turned across the corner and facing down into the corridor.
	const FVector2D Corners[4] = { { X0, NorthFace() }, { X1, NorthFace() }, { X0, SouthFace() }, { X1, SouthFace() } };
	for (const FVector2D& Corner : Corners)
	{
		const float SignX = Corner.X < 0.f && Corner.X < X0 + 1.f ? 1.f : -1.f;
		const float SignY = Corner.Y < CenterY() ? 1.f : -1.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Inset = 26.f + i * 20.f;
			Build.Add(FRoomShapes::Plane(), FVector(Corner.X + SignX * Inset, Corner.Y + SignY * Inset, H - 26.f - i * 6.f),
				FRotator(0.f, SignX * SignY > 0.f ? 45.f : -45.f, 180.f), FVector(Inset * 1.6f, Inset * 1.6f, 1.f), MatWeb, false);
		}
	}
	for (int32 i = 0; i < 5; ++i)
	{
		const bool bNorth = (i % 2) == 0;
		const float X = FMath::Lerp(X0 + 200.f, X1 - 150.f, i / 4.f) + Random.FRandRange(-60.f, 60.f);
		const float Y = bNorth ? NorthFace() + 22.f : SouthFace() - 22.f;
		Build.Add(FRoomShapes::Plane(), FVector(X, Y, H - 30.f), FRotator(0.f, Random.FRandRange(-10.f, 10.f), bNorth ? 150.f : -150.f),
			FVector(Random.FRandRange(50.f, 90.f), 40.f, 1.f), MatWeb, false);
	}
}

void ACorridorActor::BuildDamage(FRoomBuilder& Build)
{
	const float H = Setup.Height;
	const FLinearColor DampTint(0.46f, 0.38f, 0.30f);
	const FLinearColor SubstrateTint(0.30f, 0.26f, 0.22f);
	// Mould: the bedroom's rule is that nothing here is allowed to go cool, so it is a dark warm
	// bloom — the colour comes from how dark it is, not from any green in it.
	const FLinearColor MouldTint(0.20f, 0.19f, 0.13f);

	auto Stain = [&](ESide Side, float U, float V, float SU, float SV, const FRoomSurface& Set, const FLinearColor& Tint, float Opacity, float Roll, float Edge)
	{
		FVector Location;
		FRotator Rotation;
		AimAt(Side, U, V, Roll, Location, Rotation);
		Build.Stain(Set, Location, Rotation, FVector2D(SU, SV), Tint, Opacity, Edge);
	};
	auto Crack = [&](ESide Side, float U, float V, float SU, float SV, float Opacity, float Sharpness)
	{
		FVector Location;
		FRotator Rotation;
		AimAt(Side, U, V, 0.f, Location, Rotation);
		Build.Crack(Location, Rotation, FVector2D(SU, SV), Opacity, Sharpness);
	};

	for (const ESide Side : { ESide::North, ESide::South, ESide::East, ESide::West })
	{
		float U0, U1;
		SideRange(Side, U0, U1);
		const float Length = U1 - U0;
		const bool bLong = Side == ESide::North || Side == ESide::South;
		const int32 Scale = bLong ? 1 : 0;

		for (int32 i = 0; i < (bLong ? 18 : 3); ++i)
		{
			Stain(Side, Random.FRandRange(U0, U1), Random.FRandRange(40.f, H - 30.f), Random.FRandRange(110.f, 240.f), Random.FRandRange(90.f, 200.f),
				RoomSurfaces::Damp, DampTint * 0.85f, Random.FRandRange(0.14f, 0.26f), Random.FRandRange(0.f, 360.f), 1.15f);
		}
		// Water down from the ceiling line, bitten along its lower edge.
		for (int32 i = 0; i < (bLong ? 7 : 2); ++i)
		{
			const float Drop = Random.FRandRange(50.f, 130.f);
			Stain(Side, Random.FRandRange(U0, U1), H - Drop * 0.38f, Random.FRandRange(90.f, 200.f), Drop,
				RoomSurfaces::Damp, DampTint * 0.75f, Random.FRandRange(0.4f, 0.6f), 0.f, 1.25f);
		}
		// Cracks, clustered, with a good deal of wall that has none.
		for (int32 i = 0; i < 4 + Scale * 4; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float V = Random.FRandRange(ChairRail + 30.f, H - 50.f);
			if (!IsOnOpening(Side, U, V, 55.f, 55.f))
			{
				Crack(Side, U, V, Random.FRandRange(90.f, 180.f), Random.FRandRange(100.f, 220.f), Random.FRandRange(0.65f, 1.f), Random.FRandRange(15.f, 28.f));
			}
		}
		// Plaster blown off the brick, darker than the wall, above the panelling where the paper
		// is gone — the reason the paper is gone.
		for (int32 i = 0; i < 3 + Scale * 7; ++i)
		{
			const float U = Random.FRandRange(U0, U1);
			const float V = Random.FRandRange(ChairRail + 30.f, H - 40.f);
			if (!IsOnOpening(Side, U, V, 45.f, 45.f))
			{
				Stain(Side, U, V, Random.FRandRange(34.f, 100.f), Random.FRandRange(38.f, 115.f),
					RoomSurfaces::Substrate, SubstrateTint, Random.FRandRange(0.8f, 1.f), Random.FRandRange(0.f, 360.f), 0.9f);
			}
		}
		// Mould in the corners, low and high.
		for (const float Corner : { U0, U1 })
		{
			const float U = Corner + (Corner == U0 ? 1.f : -1.f) * Random.FRandRange(10.f, 40.f);
			Stain(Side, U, 30.f, Random.FRandRange(50.f, 110.f), Random.FRandRange(70.f, 130.f), RoomSurfaces::Damp, MouldTint, 0.7f, 0.f, 1.1f);
			Stain(Side, U, H - 30.f, Random.FRandRange(60.f, 120.f), Random.FRandRange(50.f, 100.f), RoomSurfaces::Damp, MouldTint, 0.6f, 0.f, 1.1f);
		}

		(void)Length;
	}

	// The mould round the window, where the rain has been coming in round the frame for years.
	for (int32 i = 0; i < 4; ++i)
	{
		const float U = CenterY() + (i % 2 == 0 ? -1.f : 1.f) * Random.FRandRange(90.f, 104.f);
		Stain(ESide::East, U, Random.FRandRange(40.f, 240.f), Random.FRandRange(30.f, 60.f), Random.FRandRange(80.f, 160.f),
			RoomSurfaces::Damp, MouldTint, 0.75f, 0.f, 1.1f);
	}

	// Scratches beside the far ajar door, at the height of a hand. Cracks drawn very fine are what
	// a gouge in plaster is (the bedroom's door-wall note): the surface taken away along a line.
	for (int32 i = 0; i < 6; ++i)
	{
		Crack(ESide::North, -1060.f + 66.f + Random.FRandRange(0.f, 26.f), Random.FRandRange(96.f, 150.f),
			Random.FRandRange(10.f, 18.f), Random.FRandRange(36.f, 60.f), 1.f, Random.FRandRange(38.f, 52.f));
	}
	// And low on the bedroom side of that same wall, beside the bedroom door.
	for (int32 i = 0; i < 4; ++i)
	{
		Crack(ESide::North, Setup.StartDoorCenterX - 70.f - Random.FRandRange(0.f, 18.f), Random.FRandRange(40.f, 80.f),
			Random.FRandRange(8.f, 14.f), Random.FRandRange(26.f, 40.f), 0.9f, Random.FRandRange(40.f, 52.f));
	}
}

void ACorridorActor::BuildPortraits(FRoomBuilder& Build)
{
	// The family, down both walls on the picture rail's line. hanging_picture_frame_01 faces its
	// local +Y, so yaw 0 turns it out of the north wall and 180 out of the south; the crooked hang
	// is pitch, which turns it in its own plane (the 09-21 notes — this mesh has bitten three times).
	for (const FPortrait& P : Portraits)
	{
		const float Face = P.bNorth ? NorthFace() : SouthFace();
		const float Out = P.bNorth ? 1.f : -1.f;
		if (UStaticMeshComponent* Frame = Build.Prop(RoomProps::PictureFrame, FVector(P.U, Face + Out * 3.f, P.Z),
			FRotator(P.Tilt, P.bNorth ? 0.f : 180.f, 0.f), P.Size, false))
		{
			Frame->SetMaterial(0, P.bGlassGone ? MatVoid.Get() : MatGlass.Get());
			Frame->SetMaterial(2, P.Size > 70.f ? MatBrass.Get() : MatRoughWood.Get());
			DarkenArtwork(Frame);
		}
		// The nail and the cord's shadow line above it.
		Build.Sph(FVector(P.U, Face + Out * 1.f, P.Z + P.Size * 0.5f + 12.f), 1.6f, MatIron);
		if (P.bGlassGone)
		{
			FVector Location = FVector(P.U, Face + Out * 8.f, P.Z);
			Build.Crack(Location, FRotator(0.f, P.bNorth ? -90.f : 90.f, 0.f), FVector2D(P.Size * 0.7f, P.Size), 0.9f, 24.f);
		}
	}

	// Where two more hung: the clean rectangle of wall the dirt never reached. The one place a
	// hard edge is right. Plus the nail.
	const FVector2D Ghosts[] = { { -520.f, 0.f }, { -1000.f, 1.f } };
	for (const FVector2D& Ghost : Ghosts)
	{
		const bool bNorth = Ghost.Y < 0.5f;
		if (bNorth)
		{
			continue; // the north one is the portrait that was turned round, not taken down
		}
		Build.Mark(FVector(Ghost.X, SouthFace() - 0.2f, 170.f), FRotator(0.f, 0.f, -90.f), FVector2D(50.f, 66.f), MatWallpaper);
		Build.Sph(FVector(Ghost.X, SouthFace() - 1.f, 216.f), 1.6f, MatIron);
	}
}

void ACorridorActor::BuildFurniture(FRoomBuilder& Build)
{
	// A hall table against the north wall under the big portrait: ClassicNightstand_01, which
	// faces its local +Y like the bedroom's other props, so yaw 0 turns its front to the corridor.
	const FVector TableSeat(-150.f, NorthFace() + 22.f, 0.f);
	// Its paint is a clean pale grey as shipped, and out here it was the brightest thing in the
	// corridor; the same Tint trick the portraits use takes it back to sixty years of dust.
	if (UStaticMeshComponent* Table = Build.PropSeated(RoomProps::Nightstand, TableSeat, FRotator(0.f, 0.f, 0.f), 0.f))
	{
		for (int32 Slot = 0; Slot < Table->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* Aged = UMaterialInstanceDynamic::Create(Table->GetMaterial(Slot), Table))
			{
				Aged->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.34f, 0.30f, 0.26f));
				Table->SetMaterial(Slot, Aged);
			}
		}
	}
	const float TableTop = 70.f;

	// A brass candlestick on it, the candle burnt to a stub, and its wax run down the stem.
	const FVector Stick = TableSeat + FVector(-14.f, 2.f, TableTop);
	Build.Cyl(Stick + FVector(0.f, 0.f, 1.f), FRotator::ZeroRotator, FVector(11.f, 11.f, 2.f), MatBrass, false);
	Build.Cyl(Stick + FVector(0.f, 0.f, 11.f), FRotator::ZeroRotator, FVector(2.6f, 2.6f, 18.f), MatBrass, false);
	Build.Cyl(Stick + FVector(0.f, 0.f, 20.5f), FRotator::ZeroRotator, FVector(7.f, 7.f, 1.2f), MatBrass, false);
	Build.Cyl(Stick + FVector(0.f, 0.f, 24.f), FRotator::ZeroRotator, FVector(2.4f, 2.4f, 6.f), MatWax, false);
	Build.Box(Stick + FVector(1.f, 1.2f, 16.f), FRotator(0.f, 0.f, 0.f), FVector(0.8f, 0.8f, 6.f), MatWax, false);

	// Two books on it, one open face down.
	Build.Box(TableSeat + FVector(12.f, -2.f, TableTop + 1.8f), FRotator(0.f, 12.f, 0.f), FVector(22.f, 16.f, 3.6f), MatCloth, false);
	Build.Box(TableSeat + FVector(13.f, -3.f, TableTop + 4.1f), FRotator(0.f, -8.f, 0.f), FVector(18.f, 13.f, 1.f), MatPaperDamp, false);
	Build.Box(TableSeat + FVector(14.f, -3.f, TableTop + 5.f), FRotator(0.f, -8.f, -4.f), FVector(19.f, 7.f, 0.7f), MatCloth, false);
	Build.Box(TableSeat + FVector(14.f, -3.f, TableTop + 5.f) + FVector(0.f, 6.f, 0.f), FRotator(0.f, -8.f, 4.f), FVector(19.f, 7.f, 0.7f), MatCloth, false);

	// An armchair on the south side, turned to face the bedroom door, a little way along. Nobody
	// puts a chair in a corridor to sit in; somebody sat in this one to watch that door.
	// ArmChair_01 faces its local +Y; yaw 180 faces it north, and a few degrees towards the door.
	Build.PropSeated(RoomProps::Armchair, FVector(318.f, SouthFace() - 44.f, 0.f), FRotator(0.f, 168.f, 0.f), 0.f);

	// A crate against the north wall between the far doors, with a pile of ledgers on it and one
	// slid off onto the floor.
	const FVector CrateSeat(-870.f, NorthFace() + 27.f, 0.f);
	Build.PropSeated(RoomProps::Crate, CrateSeat, FRotator(0.f, 4.f, 0.f), 46.f);
	for (int32 i = 0; i < 4; ++i)
	{
		const float W = Random.FRandRange(20.f, 27.f);
		const float D = Random.FRandRange(15.f, 19.f);
		const float T = Random.FRandRange(3.f, 5.5f);
		Build.Box(CrateSeat + FVector(Random.FRandRange(-3.f, 3.f), Random.FRandRange(-2.f, 2.f), 46.f + 2.8f + i * 4.6f),
			FRotator(0.f, Random.FRandRange(-16.f, 16.f), 0.f), FVector(W, D, T), (i % 2) ? MatCloth.Get() : MatPaperDamp.Get(), false);
	}
	Build.Box(CrateSeat + FVector(34.f, 18.f, 3.f), FRotator(0.f, 38.f, 0.f), FVector(24.f, 17.f, 3.6f), MatCloth, false);
	// Open, face up, pages swollen.
	Build.Box(CrateSeat + FVector(-38.f, 30.f, 1.6f), FRotator(0.f, -20.f, 3.f), FVector(17.f, 22.f, 1.4f), MatPaper, false);
	Build.Box(CrateSeat + FVector(-38.f, 30.f, 1.6f) + FVector(-14.f, -4.f, 0.f), FRotator(0.f, -20.f, -3.f), FVector(17.f, 22.f, 1.4f), MatPaper, false);

	// A kitchen chair on its side by the stairs.
	Build.Prop(RoomProps::Chair, FVector(-990.f, SouthFace() - 50.f, 24.f), FRotator(0.f, 70.f, 88.f), 92.f);

	// A floor candelabrum, over on its side on the runner at the far end: three arms, the stem
	// across the boards, candles rolled out of their cups.
	const FVector Foot(-1100.f, CenterY() - 42.f, 3.f);
	const FRotator Lay(0.f, -24.f, 0.f);
	const FVector Along = Lay.RotateVector(FVector(1.f, 0.f, 0.f));
	const FVector Across = Lay.RotateVector(FVector(0.f, 1.f, 0.f));
	Build.Cyl(Foot + FVector(0.f, 0.f, 3.f), FRotator(90.f, Lay.Yaw, 0.f), FVector(24.f, 24.f, 3.f), MatIron, false);
	Build.Cyl(Foot + Along * 62.f + FVector(0.f, 0.f, 3.f), FRotator(90.f, Lay.Yaw, 0.f), FVector(3.f, 3.f, 124.f), MatIron, false);
	Build.Cyl(Foot + Along * 124.f + FVector(0.f, 0.f, 3.f), FRotator(0.f, 0.f, 0.f), FVector(3.f, 3.f, 3.f), MatIron, false);
	for (const float Side : { -1.f, 0.f, 1.f })
	{
		const FVector Arm = Foot + Along * (114.f + FMath::Abs(Side) * -6.f) + Across * Side * 22.f + FVector(0.f, 0.f, 3.f);
		Build.Cyl(Arm, FRotator(90.f, Lay.Yaw, 0.f), FVector(6.f, 6.f, 3.f), MatIron, false);
	}
	Build.Cyl(Foot + Along * 150.f + Across * 30.f + FVector(0.f, 0.f, 2.4f), FRotator(90.f, 40.f, 0.f), FVector(4.6f, 4.6f, 20.f), MatWax, false);
	Build.Cyl(Foot + Along * 96.f - Across * 40.f + FVector(0.f, 0.f, 2.4f), FRotator(90.f, -70.f, 0.f), FVector(4.6f, 4.6f, 14.f), MatWax, false);
}

void ACorridorActor::BuildDebris(FRoomBuilder& Build)
{
	// Rubble along the foot of both walls, a heap under the hole in the ceiling, and the pendant
	// that came down with it. No collision on any of it: the brief asks for clutter that does not
	// block the walk, and a lump of plaster a boot catches on is a bug, not atmosphere.
	for (int32 i = 0; i < 46; ++i)
	{
		const bool bNorth = Random.FRand() < 0.5f;
		const FVector2D Spot(Random.FRandRange(WestFace + 20.f, EastFace() - 20.f),
			bNorth ? NorthFace() + Random.FRandRange(4.f, 30.f) : SouthFace() - Random.FRandRange(4.f, 30.f));
		if (FloorHole.IsInside(Spot) || IsOnOpening(bNorth ? ESide::North : ESide::South, Spot.X, 1.f, 10.f, 0.f))
		{
			continue;
		}
		const float S = Random.FRandRange(1.5f, 7.f);
		Build.Box(FVector(Spot.X, Spot.Y, 3.f + S * 0.35f), FRotator(Random.FRandRange(-30.f, 30.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-30.f, 30.f)),
			FVector(S * Random.FRandRange(0.8f, 1.8f), S * Random.FRandRange(0.8f, 1.5f), S * 0.6f), MatRubble, false);
	}

	for (int32 i = 0; i < 22; ++i)
	{
		const float R = Random.FRandRange(0.f, 48.f);
		const float A = Random.FRandRange(0.f, 2.f * PI);
		const float S = FMath::Lerp(9.f, 2.f, R / 48.f) * Random.FRandRange(0.7f, 1.3f);
		Build.Box(FVector(CeilingHole.X + FMath::Cos(A) * R, CeilingHole.Y + FMath::Sin(A) * R * 0.8f, 6.f + S * 0.3f),
			FRotator(Random.FRandRange(-25.f, 25.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-25.f, 25.f)),
			FVector(S * 1.4f, S, S * 0.5f), MatRubble, false);
	}
	for (int32 i = 0; i < 5; ++i)
	{
		Build.Box(FVector(CeilingHole.X + Random.FRandRange(-40.f, 40.f), CeilingHole.Y + Random.FRandRange(-30.f, 30.f), 7.f),
			FRotator(Random.FRandRange(-6.f, 6.f), Random.FRandRange(0.f, 180.f), 0.f), FVector(Random.FRandRange(40.f, 80.f), 3.f, 0.8f), MatRoughWood, false);
	}
	// The fallen pendant: shade on its side, chain in a heap.
	Build.Add(FRoomShapes::Cone(), FVector(CeilingHole.X + 36.f, CeilingHole.Y + 18.f, 12.f), FRotator(0.f, 30.f, 104.f), FVector(36.f, 36.f, 18.f), MatBrass, false);
	for (int32 Link = 0; Link < 10; ++Link)
	{
		Build.Box(FVector(CeilingHole.X + 20.f + Link * 4.f, CeilingHole.Y + 30.f + FMath::Sin(Link * 1.3f) * 6.f, 5.2f),
			FRotator(0.f, Link * 37.f, 90.f * (Link % 2)), FVector(0.8f, 3.f, 6.f), MatIron, false);
	}
	Build.Crack(FVector(CeilingHole.X, CeilingHole.Y, 12.f), FRotator(-90.f, 0.f, 0.f), FVector2D(160.f, 120.f), 0.5f, 10.f);

	// Papers, along the walls and a few out on the runner. Flat, both kinds, never two the same.
	for (int32 i = 0; i < 18; ++i)
	{
		const float Y = (i % 3 == 0)
			? CenterY() + Random.FRandRange(-30.f, 30.f)
			: ((i % 2) ? NorthFace() + Random.FRandRange(14.f, 50.f) : SouthFace() - Random.FRandRange(14.f, 50.f));
		const FVector2D Spot(Random.FRandRange(WestFace + 50.f, EastFace() - 60.f), Y);
		if (FloorHole.IsInside(Spot))
		{
			continue;
		}
		const float Z = (i % 3 == 0) ? 6.2f : 4.4f;
		Build.Mark(FVector(Spot.X, Spot.Y, Z), FRotator(Random.FRandRange(-2.f, 2.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-2.f, 2.f)),
			FVector2D(Random.FRandRange(19.f, 23.f), Random.FRandRange(26.f, 31.f)), Random.FRand() < 0.5f ? MatPaper.Get() : MatPaperDamp.Get());
	}

	// A broken frame and its glass on the boards under the crooked portrait, near the hole.
	const FVector Fallen(-360.f, NorthFace() + 44.f, 2.f);
	if (UStaticMeshComponent* Frame = Build.PropSeated(RoomProps::PictureFrame, Fallen, FRotator(0.f, 16.f, 90.f), 40.f, false))
	{
		Frame->SetMaterial(0, MatVoid);
		Frame->SetMaterial(2, MatRoughWood);
	}
	for (int32 i = 0; i < 7; ++i)
	{
		Build.Box(Fallen + FVector(Random.FRandRange(-40.f, 40.f), Random.FRandRange(-20.f, 30.f), 2.2f),
			FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f), FVector(Random.FRandRange(4.f, 12.f), Random.FRandRange(3.f, 9.f), 0.6f), MatGlass, false);
	}
}

void ACorridorActor::BuildFigures(FRoomBuilder& /*Build*/)
{
	// At the far end, by the boarded stairs: a woman, and a little girl holding her hand. They are
	// not there. They are there only while the sky is lit, and only some of the times it is, and
	// never when the detective is near enough to see what they are (see Tick).
	//
	// They are the two from the road. Nothing in the corridor says so, and nothing should: the
	// player who has seen the opening will know the shape of them, and nobody else needs to.
	Figures = NewObject<USceneComponent>(this, TEXT("Figures"));
	Figures->SetMobility(EComponentMobility::Movable);
	Figures->AttachToComponent(CorridorRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Figures->SetRelativeLocationAndRotation(FVector(WestFace + 60.f, CenterY() + 10.f, 3.f), FRotator(0.f, 0.f, 0.f));
	Figures->RegisterComponent();
	AddInstanceComponent(Figures);

	FRoomBuilder Shape(this, Figures);

	// The woman. A long dress is a cone, a coat over it is a cylinder, and in silhouette that is
	// all a figure is: the outline of the shoulders and the head is what the eye reads as a person.
	const float WY = 16.f;
	Shape.Add(FRoomShapes::Cone(), FVector(0.f, WY, 58.f), FRotator::ZeroRotator, FVector(50.f, 42.f, 116.f), MatShadow, false);
	Shape.Cyl(FVector(0.f, WY, 118.f), FRotator::ZeroRotator, FVector(26.f, 34.f, 52.f), MatShadow, false);
	Shape.Sph(FVector(0.f, WY, 142.f), 36.f, MatShadow);
	Shape.Cyl(FVector(0.f, WY, 152.f), FRotator::ZeroRotator, FVector(9.f, 9.f, 14.f), MatShadow, false);
	Shape.Sph(FVector(0.f, WY, 167.f), 21.f, MatShadow);
	Shape.Sph(FVector(3.f, WY, 164.f), 22.f, MatShadow); // hair, down her back
	Shape.Cyl(FVector(0.f, WY + 19.f, 116.f), FRotator(0.f, 0.f, -4.f), FVector(8.f, 8.f, 58.f), MatShadow, false);
	// Her other arm, down to the child's hand.
	Shape.Cyl(FVector(0.f, WY - 24.f, 112.f), FRotator(0.f, 0.f, 22.f), FVector(8.f, 8.f, 58.f), MatShadow, false);

	// The girl, at her side.
	const float GY = -34.f;
	Shape.Add(FRoomShapes::Cone(), FVector(0.f, GY, 32.f), FRotator::ZeroRotator, FVector(36.f, 32.f, 64.f), MatShadow, false);
	Shape.Cyl(FVector(0.f, GY, 66.f), FRotator::ZeroRotator, FVector(17.f, 21.f, 26.f), MatShadow, false);
	Shape.Sph(FVector(0.f, GY, 90.f), 17.f, MatShadow);
	Shape.Sph(FVector(2.f, GY, 88.f), 18.f, MatShadow);
	Shape.Cyl(FVector(0.f, GY + 11.f, 70.f), FRotator(0.f, 0.f, -34.f), FVector(6.f, 6.f, 30.f), MatShadow, false);

	Figures->SetVisibility(false, /*bPropagateToChildren*/ true);
}

void ACorridorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Figures || !LeadStorm)
	{
		return;
	}

	// Decided once per strike: every third one, and only if the detective is well down the
	// corridor from them. Close enough to walk up to, they are simply never there.
	const int32 Strike = LeadStorm->GetStrikeCount();
	if (Strike != FiguresStrike)
	{
		FiguresStrike = Strike;
		bool bFar = false;
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				const FVector Local = GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
				const bool bInCorridor = Local.Y > NorthFace() && Local.Y < SouthFace();
				bFar = bInCorridor && FVector::Dist2D(Local, Figures->GetRelativeLocation()) > 800.f;
			}
		}
		bFiguresThisStrike = bFar && (Strike % 3 == 2);
	}

	const bool bShow = bFiguresThisStrike && LeadStorm->GetFlashAlpha() > 0.08f;
	if (Figures->IsVisible() != bShow)
	{
		Figures->SetVisibility(bShow, /*bPropagateToChildren*/ true);
	}
}

// ---------------------------------------------------------------------------------------------

void ACorridorActor::SpawnDoors()
{
	struct FDoorSpec { bool bNorth; float U; float Ajar; FLinearColor Tint; bool bSix; bool bHole; const TCHAR* Prompt; };
	const FDoorSpec Specs[] = {
		{ true,  -690.f,  0.f,  FLinearColor(0.255f, 0.279f, 0.295f), true,  false,
			TEXT("Locked. It gives a little at the top and not at all at the bottom, as if something heavy is standing against it.") },
		{ true,  -1060.f, 24.f, FLinearColor(0.215f, 0.232f, 0.245f), false, false,
			TEXT("It will not open any further. Something on the other side is holding it.") },
		{ false, -40.f,   0.f,  FLinearColor(0.300f, 0.300f, 0.290f), false, true,
			TEXT("Locked. The brass round the keyhole is scratched bright — somebody tried a great many keys in it.") },
		{ false, -470.f,  17.f, FLinearColor(0.240f, 0.250f, 0.262f), true,  false,
			TEXT("It moves a finger's width and stops. It is too dark in there to see what stops it.") },
		{ false, -880.f,  0.f,  FLinearColor(0.330f, 0.330f, 0.320f), false, false,
			TEXT("Locked. A small door, painted once; the handle is set lower than the others.") },
	};

	int32 Seed = 4101;
	for (const FDoorSpec& Spec : Specs)
	{
		FHallDoorSetup DoorSetup;
		DoorSetup.Width = HallDoorWidth - 4.f;
		DoorSetup.Height = HallDoorHeight - 2.f;
		DoorSetup.AjarYaw = Spec.Ajar;
		DoorSetup.Seed = Seed++;
		DoorSetup.WoodTint = Spec.Tint;
		DoorSetup.bSixPanel = Spec.bSix;
		DoorSetup.bRotHole = Spec.bHole;
		DoorSetup.Prompt = Spec.Prompt;

		// Hinge on the corridor face, 2cm inside the opening so the reveal lining clears the leaf.
		// Local +X is the corridor side: yaw 90 on the north wall, -90 on the south.
		const float Half = HallDoorWidth * 0.5f;
		const FVector Hinge = Spec.bNorth
			? FVector(Spec.U + Half - 2.f, NorthFace() - 2.6f, 0.f)
			: FVector(Spec.U - Half + 2.f, SouthFace() + 2.6f, 0.f);
		const FTransform Transform(FRotator(0.f, Spec.bNorth ? 90.f : -90.f, 0.f), GetActorTransform().TransformPosition(Hinge));

		if (AHallDoorActor* Door = GetWorld()->SpawnActorDeferred<AHallDoorActor>(AHallDoorActor::StaticClass(), Transform, this))
		{
			Door->Configure(DoorSetup);
			Door->FinishSpawning(Transform);
			Doors.Add(Door);
		}
	}
}

void ACorridorActor::SpawnWindow()
{
	// A second, larger window on the same façade as the bedroom's, at the end of the corridor.
	// It is a follower of the bedroom's storm: one sky, one lightning, one directional light.
	const FTransform Transform(FRotator::ZeroRotator, GetActorTransform().TransformPosition(FVector(EastFace() + Setup.WallThickness * 0.5f, CenterY(), 0.f)));
	Window = GetWorld()->SpawnActorDeferred<AStormWindowActor>(AStormWindowActor::StaticClass(), Transform, this);
	if (Window)
	{
		FStormWindowSetup WindowSetup;
		WindowSetup.OpeningWidth = WindowWidth;
		WindowSetup.SillHeight = WindowSill;
		WindowSetup.TopHeight = WindowTop;
		WindowSetup.WallThickness = Setup.WallThickness;
		Window->Configure(WindowSetup);
		Window->SetLead(LeadStorm);
		Window->FinishSpawning(Transform);
	}
}

AClueActor* ACorridorActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation, const FString& ShortName, const FString& Description)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AClueActor* Clue = GetWorld()->SpawnActor<AClueActor>(AClueActor::StaticClass(), GetActorTransform().TransformPosition(LocalLocation), Rotation, SpawnParams);
	if (Clue)
	{
		Clue->Configure(FText::FromString(ShortName), FText::FromString(Description));
		Clues.Add(Clue);
	}
	return Clue;
}

void ACorridorActor::BuildClues()
{
	const float H = Setup.Height;

	// Flowers at the bedroom door, on the corridor side: somebody left them for whoever was in
	// there, and nobody came to the door. A dozen stems gone to straw, tied with a ribbon.
	if (AClueActor* Flowers = SpawnClue(FVector(Setup.StartDoorCenterX - 20.f, NorthFace() + 26.f, 4.f), FRotator(0.f, 14.f, 0.f),
		TEXT("Examine the flowers"),
		TEXT("Roses, dried to paper, tied with a black ribbon and laid at the door. Laid, not dropped — for whoever was on the other side.")))
	{
		FRoomBuilder B(Flowers, Flowers->GetRootScene());
		FRandomStream Stems(77);
		for (int32 i = 0; i < 12; ++i)
		{
			const float Spread = Stems.FRandRange(-9.f, 9.f);
			const float Length = Stems.FRandRange(34.f, 44.f);
			B.Cyl(FVector(0.f, Spread * 0.25f, 1.f), FRotator(90.f, Spread, 0.f), FVector(0.7f, 0.7f, Length), MatStem, false);
			const FVector Head = FRotator(0.f, Spread, 0.f).RotateVector(FVector(-Length * 0.5f, 0.f, 0.f)) + FVector(0.f, 0.f, 2.f);
			B.Sph(Head, Stems.FRandRange(3.4f, 4.6f), MatPetal);
			if (Stems.FRand() < 0.4f)
			{
				B.Sph(Head + FVector(Stems.FRandRange(-3.f, 3.f), Stems.FRandRange(-3.f, 3.f), -1.5f), 2.2f, MatPetal);
			}
		}
		B.Cyl(FVector(4.f, 0.f, 1.2f), FRotator(0.f, 0.f, 90.f), FVector(4.f, 4.f, 6.f), MatShadow, false);
		// The hit volume: a thin invisible slab over the bunch, so the trace finds the flowers
		// rather than the floor between the stems.
		if (UStaticMeshComponent* Hit = B.Box(FVector(0.f, 0.f, 3.f), FRotator::ZeroRotator, FVector(50.f, 22.f, 5.f), MatVoid))
		{
			Hit->SetHiddenInGame(true);
			Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
		// A few petals fallen off onto the boards.
		for (int32 i = 0; i < 5; ++i)
		{
			B.Sph(FVector(Stems.FRandRange(-30.f, 12.f), Stems.FRandRange(-14.f, 14.f), 0.6f), 1.6f, MatPetal);
		}
	}

	// The mirror, on the south wall, square to the bedroom door. What it gives back is the door —
	// and, through the dust, the shape of a man standing in front of it.
	if (AClueActor* Mirror = SpawnClue(FVector(Setup.StartDoorCenterX, SouthFace(), 158.f), FRotator::ZeroRotator,
		TEXT("Examine the mirror"),
		TEXT("Too much dust to see a face in it — only the shape of a man, and the door behind him. It would take a hand to wipe it clean.")))
	{
		FRoomBuilder B(Mirror, Mirror->GetRootScene());
		const float W = 72.f;
		const float Hh = 104.f;
		B.Box(FVector(0.f, -1.2f, 0.f), FRotator::ZeroRotator, FVector(W, 2.4f, Hh), MatVoid);
		B.Box(FVector(0.f, -2.8f, 0.f), FRotator::ZeroRotator, FVector(W - 10.f, 0.8f, Hh - 10.f), MatMirror, false);
		const float Rail = 8.f;
		B.Box(FVector(0.f, -4.f, Hh * 0.5f - Rail * 0.5f), FRotator::ZeroRotator, FVector(W + 4.f, 5.f, Rail), MatBrass, false);
		B.Box(FVector(0.f, -4.f, -Hh * 0.5f + Rail * 0.5f), FRotator::ZeroRotator, FVector(W + 4.f, 5.f, Rail), MatBrass, false);
		B.Box(FVector(-W * 0.5f + Rail * 0.5f, -4.f, 0.f), FRotator::ZeroRotator, FVector(Rail, 5.f, Hh), MatBrass, false);
		B.Box(FVector(W * 0.5f - Rail * 0.5f, -4.f, 0.f), FRotator::ZeroRotator, FVector(Rail, 5.f, Hh), MatBrass, false);
		B.Box(FVector(0.f, -4.5f, Hh * 0.5f + 7.f), FRotator::ZeroRotator, FVector(W * 0.6f, 5.f, 10.f), MatBrass, false);
		B.Sph(FVector(0.f, -1.f, Hh * 0.5f + 20.f), 1.6f, MatIron);
		// The dust, projected onto the glass: heaviest at the edges and the bottom, where it
		// settles, and thinner across the middle — thin enough that something shows.
		// One projection, not two: a second, heavier one over the lower half drew a hard line
		// across the glass where the two met.
		B.Stain(RoomSurfaces::Damp, FVector(0.f, -10.f, 0.f), FRotator(0.f, 90.f, 0.f), FVector2D(W - 6.f, Hh - 6.f),
			FLinearColor(0.16f, 0.15f, 0.13f), 0.16f, 1.2f, 1.4f);
	}

	// The long-case clock, broken: pendulum on the floor, trunk door hanging open, the hood glass
	// gone. The hands at four minutes past eleven, the time the whole house is stopped at.
	if (AClueActor* Clock = SpawnClue(FVector(-260.f, SouthFace(), 0.f), FRotator::ZeroRotator,
		TEXT("Examine the clock"),
		TEXT("The pendulum has been torn off and dropped. The hands say four minutes past eleven — the same as the clock in the bedroom.")))
	{
		FRoomBuilder B(Clock, Clock->GetRootScene());
		UMaterialInstanceDynamic* Case = B.Surface(RoomSurfaces::RoughWood, FLinearColor(0.20f, 0.19f, 0.19f));
		UMaterialInstanceDynamic* CaseEdge = B.Surface(RoomSurfaces::RoughWood, FLinearColor(0.27f, 0.26f, 0.25f));
		const float D = 30.f;
		B.Box(FVector(0.f, -D * 0.5f - 1.f, 22.f), FRotator::ZeroRotator, FVector(54.f, D, 44.f), Case);
		B.Box(FVector(0.f, -D * 0.5f - 1.f, 45.f), FRotator::ZeroRotator, FVector(58.f, D + 3.f, 4.f), CaseEdge, false);
		B.Box(FVector(0.f, -D * 0.5f - 1.f, 102.f), FRotator::ZeroRotator, FVector(42.f, D - 6.f, 110.f), Case);
		B.Box(FVector(0.f, -D * 0.5f - 1.f, 159.f), FRotator::ZeroRotator, FVector(52.f, D + 1.f, 4.f), CaseEdge, false);
		B.Box(FVector(0.f, -D * 0.5f - 1.f, 188.f), FRotator::ZeroRotator, FVector(50.f, D, 54.f), Case);
		B.Box(FVector(0.f, -D * 0.5f - 1.f, 218.f), FRotator::ZeroRotator, FVector(58.f, D + 4.f, 6.f), CaseEdge, false);
		B.Box(FVector(-18.f, -D * 0.5f - 1.f, 226.f), FRotator(0.f, 0.f, 0.f), FVector(10.f, D, 10.f), CaseEdge, false);
		B.Box(FVector(18.f, -D * 0.5f - 1.f, 226.f), FRotator(0.f, 0.f, 0.f), FVector(10.f, D, 10.f), CaseEdge, false);
		B.Sph(FVector(0.f, -D * 0.5f - 1.f, 232.f), 9.f, MatBrass);

		// The trunk: a dark well where the door has swung off it, and the door hanging on one hinge.
		const float TrunkFront = -D - 1.f + 3.f;
		B.Box(FVector(0.f, TrunkFront - 0.3f, 104.f), FRotator::ZeroRotator, FVector(28.f, 0.6f, 84.f), MatVoid, false);
		const FVector DoorHinge(-15.f, TrunkFront - 1.f, 104.f);
		const FRotator DoorOpen(0.f, 58.f, 3.f);
		B.Box(DoorHinge + DoorOpen.RotateVector(FVector(15.f, -0.8f, 0.f)), DoorOpen, FVector(30.f, 1.6f, 88.f), CaseEdge, false);
		B.Box(DoorHinge + DoorOpen.RotateVector(FVector(15.f, -1.8f, 0.f)), DoorOpen, FVector(22.f, 0.5f, 70.f), MatGlass, false);

		// The dial, behind a hood whose glass is gone.
		const FVector Dial(0.f, -D - 1.2f, 190.f);
		B.Cyl(Dial, FRotator(0.f, 0.f, 90.f), FVector(34.f, 34.f, 1.f), MatDial, false);
		B.Cyl(Dial + FVector(0.f, -0.6f, 0.f), FRotator(0.f, 0.f, 90.f), FVector(36.f, 36.f, 0.4f), MatBrass, false);
		// Clockwise as seen from the corridor, which faces +Y: the viewer's right is -X, so a
		// clockwise angle A from twelve is (-sin A, cos A) and a part turned to it is pitched by A
		// (the 09-21 mirrored-dial lesson).
		for (int32 Hour = 0; Hour < 12; ++Hour)
		{
			const float A = FMath::DegreesToRadians(Hour * 30.f);
			const FVector Dir(-FMath::Sin(A), 0.f, FMath::Cos(A));
			B.Box(Dial + Dir * 13.5f + FVector(0.f, -0.9f, 0.f), FRotator(Hour * 30.f, 0.f, 0.f), FVector(0.9f, 0.4f, Hour % 3 == 0 ? 4.f : 2.5f), MatShadow, false);
		}
		auto Hand = [&](float Degrees, float Length, float Width)
		{
			const float A = FMath::DegreesToRadians(Degrees);
			const FVector Dir(-FMath::Sin(A), 0.f, FMath::Cos(A));
			B.Box(Dial + Dir * Length * 0.4f + FVector(0.f, -1.3f, 0.f), FRotator(Degrees, 0.f, 0.f), FVector(Width, 0.4f, Length), MatShadow, false);
		};
		Hand((11.f + 4.f / 60.f) * 30.f, 9.f, 1.3f);
		Hand(4.f * 6.f, 13.f, 0.9f);

		// The pendulum, on the boards in front of it: rod and bob. And one weight, fallen.
		B.Cyl(FVector(6.f, -D - 38.f, 1.2f), FRotator(90.f, 80.f, 0.f), FVector(1.2f, 1.2f, 70.f), MatBrass, false);
		B.Cyl(FVector(2.f, -D - 72.f, 1.6f), FRotator::ZeroRotator, FVector(15.f, 15.f, 2.4f), MatBrass, false);
		B.Cyl(FVector(-22.f, -D - 14.f, 5.f), FRotator(90.f, 20.f, 0.f), FVector(6.f, 6.f, 22.f), MatIron, false);
	}

	// The portrait turned to face the wall. From the corridor, the back of a frame: brown board,
	// the hanging wire, a torn label.
	if (AClueActor* Turned = SpawnClue(FVector(-520.f, NorthFace() + 3.f, 170.f), FRotator::ZeroRotator,
		TEXT("Examine the portrait"),
		TEXT("Turned to face the wall, and the nail bent to hold it there. Somebody could not stand to be looked at by it.")))
	{
		FRoomBuilder B(Turned, Turned->GetRootScene());
		if (UStaticMeshComponent* Frame = B.Prop(RoomProps::PictureFrame, FVector::ZeroVector, FRotator(-2.f, 180.f, 0.f), 70.f))
		{
			Frame->SetMaterial(0, MatGlass);
			Frame->SetMaterial(2, MatBrass);
			DarkenArtwork(Frame);
		}
		// The backing board and a scrap of label on it.
		B.Box(FVector(0.f, 1.4f, 0.f), FRotator(-2.f, 0.f, 0.f), FVector(44.f, 0.6f, 64.f), MatPanelField, false);
		B.Box(FVector(-6.f, 1.9f, -12.f), FRotator(-6.f, 0.f, 0.f), FVector(12.f, 0.3f, 7.f), MatPaperDamp, false);
		B.Cyl(FVector(0.f, 2.2f, 30.f), FRotator(0.f, 0.f, 0.f), FVector(0.4f, 0.4f, 0.4f), MatIron, false);
		B.Cyl(FVector(-8.f, 2.2f, 34.f), FRotator(-60.f, 0.f, 0.f), FVector(0.3f, 0.3f, 19.f), MatIron, false);
		B.Cyl(FVector(8.f, 2.2f, 34.f), FRotator(60.f, 0.f, 0.f), FVector(0.3f, 0.3f, 19.f), MatIron, false);
	}

	// Keys, on a nail beside the door with the scratched keyhole.
	if (AClueActor* Keys = SpawnClue(FVector(-40.f + 78.f, SouthFace() - 1.f, 146.f), FRotator::ZeroRotator,
		TEXT("Examine the keys"),
		TEXT("A ring of old keys on a nail. Every one of them has been tried in the door beside it. None of them turned.")))
	{
		FRoomBuilder B(Keys, Keys->GetRootScene());
		B.Cyl(FVector(0.f, -1.5f, 0.f), FRotator(0.f, 0.f, 90.f), FVector(0.5f, 0.5f, 4.f), MatIron, false);
		for (int32 Seg = 0; Seg < 10; ++Seg)
		{
			const float A = Seg / 10.f * 2.f * PI;
			B.Box(FVector(FMath::Sin(A) * 3.2f, -3.2f, -3.4f + FMath::Cos(A) * 3.2f), FRotator(-Seg * 36.f, 0.f, 0.f), FVector(0.5f, 0.5f, 2.2f), MatIron, false);
		}
		for (int32 KeyIndex = 0; KeyIndex < 5; ++KeyIndex)
		{
			const float Swing = (KeyIndex - 2) * 8.f;
			const FRotator Hang(Swing, 0.f, 0.f);
			const FVector Top(FMath::Sin(FMath::DegreesToRadians(Swing)) * -3.f, -3.2f - KeyIndex * 0.35f, -6.6f);
			const float Length = 7.f + KeyIndex % 3 * 1.5f;
			B.Box(Top + Hang.RotateVector(FVector(0.f, 0.f, -Length * 0.5f)), Hang, FVector(0.6f, 0.4f, Length), KeyIndex % 2 ? MatBrass.Get() : MatIron.Get(), false);
			B.Box(Top + Hang.RotateVector(FVector(0.9f, 0.f, -Length + 1.f)), Hang, FVector(1.6f, 0.4f, 1.2f), MatIron, false);
		}
		if (UStaticMeshComponent* Hit = B.Box(FVector(0.f, -3.f, -8.f), FRotator::ZeroRotator, FVector(12.f, 4.f, 16.f), MatVoid))
		{
			Hit->SetHiddenInGame(true);
			Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}

	// The small door's frame: pencil lines up the hinge-side casing, a child measured year after
	// year, stopping a little over a metre up.
	if (AClueActor* Chart = SpawnClue(FVector(-880.f - HallDoorWidth * 0.5f - 5.5f, SouthFace() - 3.f, 0.f), FRotator::ZeroRotator,
		TEXT("Examine the door frame"),
		TEXT("Pencil lines up the frame, a child measured against it year after year. They stop a little over a metre up.")))
	{
		FRoomBuilder B(Chart, Chart->GetRootScene());
		const float Heights[] = { 74.f, 83.f, 91.f, 98.f, 104.f, 111.f };
		for (const float Z : Heights)
		{
			B.Box(FVector(0.f, -0.3f, Z), FRotator(Random.FRandRange(-4.f, 4.f), 0.f, 0.f), FVector(7.f, 0.4f, 0.45f), MatShadow, false);
			B.Box(FVector(2.2f, -0.3f, Z + 1.6f), FRotator::ZeroRotator, FVector(1.4f, 0.4f, 1.6f), MatShadow, false);
		}
		if (UStaticMeshComponent* Hit = B.Box(FVector(0.f, 0.5f, 92.f), FRotator::ZeroRotator, FVector(11.f, 2.f, 50.f), MatVoid))
		{
			Hit->SetHiddenInGame(true);
			Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}

	// The stairs, boarded from the landing side.
	if (AClueActor* Stairs = SpawnClue(FVector(WestFace + 6.f, CenterY(), 120.f), FRotator::ZeroRotator,
		TEXT("Examine the boards"),
		TEXT("The stairs down, boarded over. The nails were driven from this side. Whoever did it meant to stay up here.")))
	{
		FRoomBuilder B(Stairs, Stairs->GetRootScene());
		if (UStaticMeshComponent* Hit = B.Box(FVector::ZeroVector, FRotator::ZeroRotator, FVector(2.f, StairOpeningWidth, 200.f), MatVoid))
		{
			Hit->SetHiddenInGame(true);
			Hit->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}

	(void)H;
}
