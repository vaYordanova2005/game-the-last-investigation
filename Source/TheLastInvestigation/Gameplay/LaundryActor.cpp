#include "LaundryActor.h"
#include "CellarActor.h"
#include "RoomBuildLibrary.h"
#include "ClueActor.h"
#include "DustMotesComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Algo/Find.h"

namespace
{
	/** The rectangles of the laundry_paper atlas (Tools/make_laundry_art.py: RECTS), in pixels. */
	struct FLaundryRect
	{
		const TCHAR* Name;
		float X, Y, W, H;
	};
	const FLaundryRect LaundryRects[] = {
		{ TEXT("calendar"), 1024.f, 512.f, 366.f, 512.f },
		{ TEXT("note_a"), 1536.f, 512.f, 342.f, 512.f },
		{ TEXT("schedule"), 0.f, 1024.f, 724.f, 512.f },
		{ TEXT("note_b"), 1024.f, 1024.f, 342.f, 512.f },
	};

	/** The generated models (Tools/make_laundry.py), /Game/Meshes/<name>. */
	namespace LaundryProps
	{
		const TCHAR* Washer = TEXT("laundry_washer");
		const TCHAR* Dryer = TEXT("laundry_dryer");
		const TCHAR* MachineDoor = TEXT("laundry_machine_door");
		const TCHAR* WashLoad = TEXT("laundry_wash_load");
		const TCHAR* Heater = TEXT("laundry_heater");
		const TCHAR* Sink = TEXT("laundry_sink");
		const TCHAR* Taps = TEXT("laundry_taps");
		const TCHAR* Valve = TEXT("laundry_valve");
		const TCHAR* Drain = TEXT("laundry_drain");
		const TCHAR* Counter = TEXT("laundry_counter");
		const TCHAR* Cabinet = TEXT("laundry_cabinet");
		const TCHAR* CabinetOpen = TEXT("laundry_cabinet_open");
		const TCHAR* CabinetDoor = TEXT("laundry_cabinet_door");
		const TCHAR* Shelving = TEXT("laundry_shelving");
		const TCHAR* WallShelf = TEXT("laundry_wall_shelf");
		const TCHAR* Stool = TEXT("laundry_stool");
		const TCHAR* IroningBoard = TEXT("laundry_ironing_board");
		const TCHAR* Iron = TEXT("laundry_iron");
		const TCHAR* Airer = TEXT("laundry_airer");
		const TCHAR* Shirt = TEXT("laundry_shirt");
		const TCHAR* ChildTop = TEXT("laundry_child_top");
		const TCHAR* TowelHung = TEXT("laundry_towel_hung");
		const TCHAR* Pillowcase = TEXT("laundry_pillowcase");
		const TCHAR* Socks = TEXT("laundry_socks");
		const TCHAR* TowelStack = TEXT("laundry_towel_stack");
		const TCHAR* SheetStack = TEXT("laundry_sheet_stack");
		const TCHAR* Rag = TEXT("laundry_rag");
		const TCHAR* Basket = TEXT("laundry_basket");
		const TCHAR* BasketClothes = TEXT("laundry_basket_clothes");
		const TCHAR* Detergent = TEXT("laundry_detergent");
		const TCHAR* Starch = TEXT("laundry_starch");
		const TCHAR* Softener = TEXT("laundry_softener");
		const TCHAR* Spray = TEXT("laundry_spray");
		const TCHAR* Soap = TEXT("laundry_soap");
		const TCHAR* Peg = TEXT("laundry_peg");
		const TCHAR* PegTin = TEXT("laundry_peg_tin");
		const TCHAR* MopBucket = TEXT("laundry_mop_bucket");
		const TCHAR* Broom = TEXT("laundry_broom");
		const TCHAR* Gloves = TEXT("laundry_gloves");
		const TCHAR* SewingKit = TEXT("laundry_sewing_kit");
		const TCHAR* Toolbox = TEXT("laundry_toolbox");
		const TCHAR* Radio = TEXT("laundry_radio");
	}

	/** The rendered walls' concrete, and the corridor's brick on its face of the door wall. */
	const FLinearColor LaundryRenderTint(0.150f, 0.147f, 0.140f);
	const FLinearColor LaundryCorridorBrickTint(0.150f, 0.130f, 0.112f);
	/** Smaller flags than the wine cellar's: a laundry was laid in tiles a hand could set. */
	const FRoomSurface LaundryFlagSet{ TEXT("large_floor_tiles_02"), 190.f };
	/** The dust on everything, as a decal aimed down: the colour the house's dust is everywhere. */
	const FLinearColor LaundryDust(0.42f, 0.40f, 0.36f);

	/** Aimed straight down, a decal lays its first size along Y and its second along X (09-27); this
	 *  is the box round that rectangle turned by its roll (the wine cellar's helper). */
	FVector2D LaundryFloorDecalHalfExtent(const FVector2D& Size, float Roll)
	{
		const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Roll)));
		const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Roll)));
		return FVector2D(Cos * Size.Y + Sin * Size.X, Cos * Size.X + Sin * Size.Y) * 0.5f;
	}

	/** A rotation that yaws, then leans the part's top back by Lean degrees towards Back (horizontal):
	 *  for things stood against a wall, so the lean always goes into the wall whatever the yaw. */
	FQuat LaundryLeaning(float Yaw, float Lean, const FVector& Back)
	{
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Back.GetSafeNormal());
		return FQuat(Axis, FMath::DegreesToRadians(Lean)) * Turn;
	}
}

ALaundryActor::ALaundryActor()
{
	PrimaryActorTick.bCanEverTick = false;

	LaundryRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LaundryRoot"));
	SetRootComponent(LaundryRoot);
	LaundryRoot->SetMobility(EComponentMobility::Movable);

	DustMotes = CreateDefaultSubobject<UDustMotesComponent>(TEXT("DustMotes"));
	DustMotes->ConfigureVolume(FVector(HalfX - 20.f, HalfY - 20.f, Height * 0.46f), FVector(0.f, 0.f, Height * 0.5f));
}

void ALaundryActor::BeginPlay()
{
	Super::BeginPlay();

	FRoomBuilder Build(this, LaundryRoot);
	CacheMaterials(Build);
	BuildShell(Build);
	BuildPipes(Build);
	BuildMachines(Build);
	BuildSinkAndTable(Build);
	BuildWestWall(Build);
	BuildDrying(Build);
	BuildDoorWall(Build);
	BuildCorner(Build);
	BuildWallDamp(Build);
	BuildFloor(Build);
	BuildDust(Build);
	BuildCobwebs(Build);
	BuildMist();
}

// ---------------------------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::Dress(UStaticMeshComponent* Mesh, std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	if (!Mesh)
	{
		return;
	}
	for (const TPair<const TCHAR*, UMaterialInterface*>& Slot : Slots)
	{
		if (Slot.Value && Mesh->GetMaterialIndex(FName(Slot.Key)) != INDEX_NONE)
		{
			Mesh->SetMaterialByName(FName(Slot.Key), Slot.Value);
		}
	}
}

UStaticMeshComponent* ALaundryActor::Place(FRoomBuilder& Build, const TCHAR* Name, const FVector& Location, const FRotator& Rotation,
	std::initializer_list<TPair<const TCHAR*, UMaterialInterface*>> Slots)
{
	UStaticMeshComponent* Mesh = Build.Prop(Name, Location, Rotation, 0.f, /*bBlockingCollision*/ false);
	Dress(Mesh, Slots);
	return Mesh;
}

void ALaundryActor::Blocker(FRoomBuilder& Build, const FVector& Centre, const FVector& Size, float Yaw)
{
	if (UStaticMeshComponent* Part = Build.Box(Centre, FRotator(0.f, Yaw, 0.f), Size, MatVoid))
	{
		Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Part->SetHiddenInGame(true);
		Part->SetCastShadow(false);
	}
}

UStaticMeshComponent* ALaundryActor::Paper(FRoomBuilder& Build, const TCHAR* Rect, const FVector& Centre, const FVector& Normal, const FVector& Up, float W, float H)
{
	// One instance per rectangle, made straight from the asset and never registered with the builder,
	// so Add() does not re-tile it to the plane's size (the wine cellar's paper).
	const FName Key(Rect);
	TObjectPtr<UMaterialInstanceDynamic>* Found = PaperMats.Find(Key);
	UMaterialInstanceDynamic* Mat = Found ? Found->Get() : nullptr;
	if (!Found)
	{
		const FLaundryRect* R = Algo::FindByPredicate(LaundryRects, [&](const FLaundryRect& Each) { return FCString::Strcmp(Each.Name, Rect) == 0; });
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_laundry_paper.MI_laundry_paper"));
		Mat = (Parent && R) ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		if (Mat)
		{
			// Paper in a damp cellar at about a fifth of the bake: anything paler burns out under the lantern.
			Mat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.24f, 0.22f, 0.19f));
			Mat->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
			Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(R->W / 2048.f, R->H / 2048.f, 0.f, 1.f));
			Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(R->X / 2048.f, R->Y / 2048.f, 0.f, 1.f));
		}
		PaperMats.Add(Key, Mat);
	}
	// Local Z out of the sheet, local X to the viewer's right, so local -Y is the top of the page.
	const FVector Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZX(Normal, Right).Rotator();
	UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Facing, FVector(W, H, 1.f), Mat ? Mat : MatCard.Get(), false);
	if (Sheet)
	{
		Sheet->SetCastShadow(false);
		Sheet->SetReceivesDecals(false);
	}
	return Sheet;
}

void ALaundryActor::Pipe(FRoomBuilder& Build, TConstArrayView<FVector> Points, float Diameter, UMaterialInterface* Mat, float BracketEvery)
{
	for (int32 i = 0; i + 1 < Points.Num(); ++i)
	{
		const FVector Span = Points[i + 1] - Points[i];
		const float Length = Span.Size();
		if (Length < 0.1f)
		{
			continue;
		}
		Build.Cyl((Points[i] + Points[i + 1]) * 0.5f, FRotationMatrix::MakeFromZ(Span).Rotator(), FVector(Diameter, Diameter, Length), Mat, /*bBlockingCollision*/ false);
		// Collars along the run: the clips that hold it, or the sockets where two lengths are joined.
		if (BracketEvery > 0.f)
		{
			const int32 Count = FMath::FloorToInt(Length / BracketEvery);
			for (int32 k = 1; k <= Count; ++k)
			{
				const FVector At = Points[i] + Span * (k / (Count + 1.f));
				Build.Cyl(At, FRotationMatrix::MakeFromZ(Span).Rotator(), FVector(Diameter * 1.35f, Diameter * 1.35f, 2.2f), Mat, false);
			}
		}
	}
	// A fitting at every turn and at the ends: an elbow is fatter than the pipe it joins.
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		Build.Sph(Points[i], Diameter * 1.3f, Mat);
	}
}

void ALaundryActor::Web(FRoomBuilder& Build, const FVector& Centre, const FRotator& Rotation, const FVector2D& Size)
{
	if (UStaticMeshComponent* Sheet = Build.Add(FRoomShapes::Plane(), Centre, Rotation, FVector(Size.X, Size.Y, 1.f), MatWeb, false))
	{
		Sheet->SetCastShadow(false);
	}
}

AClueActor* ALaundryActor::SpawnClue(const FVector& LocalLocation, const FRotator& Rotation)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AClueActor* Clue = GetWorld()->SpawnActor<AClueActor>(AClueActor::StaticClass(), GetActorTransform().TransformPosition(LocalLocation),
		GetActorRotation() + Rotation, SpawnParams);
	if (Clue)
	{
		Clues.Add(Clue);
	}
	return Clue;
}

void ALaundryActor::KeepWallDecalsOff(const UPrimitiveComponent* Part)
{
	if (!Part || !Part->IsRegistered())
	{
		return;
	}
	// Translated from the cellar's frame and never turned, so the room box is the world box moved.
	// A wall decal is put two centimetres off the plaster and reaches nine either way from there.
	const FBox Box = Part->Bounds.GetBox().ShiftBy(-GetActorLocation());
	const float Reach = 12.f;
	const FVector2D AlongX(Box.Min.X, Box.Max.X);
	const FVector2D AlongY(Box.Min.Y, Box.Max.Y);
	const FVector2D Up(Box.Min.Z, Box.Max.Z);
	if (Box.Min.Y < -HalfY + Reach)
	{
		WallKeepOuts.Add({ EWall::North, AlongX, Up });
	}
	if (Box.Max.Y > HalfY - Reach)
	{
		WallKeepOuts.Add({ EWall::South, AlongX, Up });
	}
	if (Box.Max.X > HalfX - Reach)
	{
		WallKeepOuts.Add({ EWall::East, AlongY, Up });
	}
	if (Box.Min.X < -HalfX + Reach)
	{
		WallKeepOuts.Add({ EWall::West, AlongY, Up });
	}
}

void ALaundryActor::KeepWallDecalsOff(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
	for (const UPrimitiveComponent* Part : Parts)
	{
		KeepWallDecalsOff(Part);
	}
}

bool ALaundryActor::WallDecalHitsSomething(EWall Wall, float U, float Z, float HalfAlong, float HalfUp) const
{
	// The doorway and its timber lining: a decal projects straight through an opening and smears
	// down the reveal (the cellar's 10-05 rule).
	const float OpeningMargin = 15.f;
	if (Wall == EWall::North && FMath::Abs(U - DoorX) < DoorHalf + HalfAlong + OpeningMargin && Z - HalfUp < DoorHeight + OpeningMargin)
	{
		return true;
	}
	const float Margin = 5.f;
	for (const FWallKeepOut& Keep : WallKeepOuts)
	{
		if (Keep.Wall == Wall
			&& U + HalfAlong + Margin > Keep.U.X && U - HalfAlong - Margin < Keep.U.Y
			&& Z + HalfUp + Margin > Keep.Z.X && Z - HalfUp - Margin < Keep.Z.Y)
		{
			return true;
		}
	}
	return false;
}

bool ALaundryActor::FloorDecalReachesDoor(const FVector2D& Point, const FVector2D& HalfExtent)
{
	const float Margin = 15.f;
	// The doorway: the step and the lining in the wall's thickness, which a floor decal projected
	// from four above the flags takes in the foot of.
	if (FMath::Abs(Point.X - DoorX) < DoorHalf + HalfExtent.X + Margin && Point.Y - HalfExtent.Y < -HalfY + Margin)
	{
		return true;
	}
	// The leaf's sweep, as ACellarActor::SpawnDoor hangs it on a south room: hinged at the west jamb
	// on the corridor's side, the leaf along +X when shut, swinging south into the room.
	const FVector2D Hinge(DoorX - DoorHalf + ACellarActor::DoorHingeInset, -(Depth + WallThickness) * 0.5f + ACellarActor::DoorHingeProud);
	const float Leaf = DoorHalf * 2.f - ACellarActor::DoorLeafClearance;
	const float Overswing = Leaf * FMath::Sin(FMath::DegreesToRadians(FMath::Max(ACellarActor::DoorOpenYaw - 90.f, 0.f)));
	return FVector2D::Distance(Point, Hinge) < Leaf + HalfExtent.Size() + Margin
		&& Point.X + HalfExtent.X > Hinge.X - Overswing - Margin;
}

// ---------------------------------------------------------------------------------------------
// Materials. Every tint worked back from the photograph's measured mean or from what the other
// rooms settled on: nothing down here much above a tenth, the whites included.
// ---------------------------------------------------------------------------------------------

UMaterialInstanceDynamic* ALaundryActor::Sheet(FRoomBuilder& Build, const FRoomSurface& Set, const FLinearColor& Tint, float RoughnessScale)
{
	// For a model's own UVs, which are in repeats of the slot's texel size already: tiling at one
	// (the nursery's and the wine cellar's rule).
	UMaterialInstanceDynamic* Mat = Build.Surface(Set, Tint, RoughnessScale);
	if (Mat)
	{
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	return Mat;
}

UMaterialInterface* ALaundryActor::WallPieceMat(const FRoomSurface& Set, const FLinearColor& Tint, float W, float H, int32 Index)
{
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Materials/MI_%s.MI_%s"), Set.Set, Set.Set));
	UMaterialInstanceDynamic* Mat = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
	if (Mat)
	{
		Mat->SetVectorParameterValue(TEXT("Tint"), Tint);
		Mat->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(W / Set.TexelSizeCm, H / Set.TexelSizeCm, 0.f, 1.f));
		Mat->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(FMath::Frac(Index * 0.317f), FMath::Frac(Index * 0.533f), 0.f, 1.f));
		WallMats.Add(Mat);
	}
	return Mat;
}

void ALaundryActor::WallPiece(FRoomBuilder& Build, const FRoomSurface& Set, const FLinearColor& Tint, const FVector& Centre, float Yaw, float W, float H, float Thick)
{
	// The engine cube's broad faces carry U along its local X and V up its Z: with the repeats
	// written out for this face, a piece narrower than it is tall keeps its courses level (10-09).
	Build.Box(Centre, FRotator(0.f, Yaw, 0.f), FVector(W, Thick, H), WallPieceMat(Set, Tint, W, H, WallMats.Num()));
}

void ALaundryActor::CacheMaterials(FRoomBuilder& Build)
{
	MatRender = Build.Surface(RoomSurfaces::Plaster, LaundryRenderTint);
	MatCorridorBrick = Build.Surface(RoomSurfaces::Substrate, LaundryCorridorBrickTint);
	// large_floor_tiles_02 is a neutral grey at 0.19: held a shade cooler than the walls (the wine cellar's).
	MatFlags = Build.Surface(LaundryFlagSet, FLinearColor(0.34f, 0.335f, 0.33f));
	MatBoards = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.30f, 0.29f, 0.28f));
	MatJoist = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.20f, 0.19f, 0.18f));
	MatTimber = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.290f, 0.276f, 0.258f));
	// Pipes: the mains in iron gone to rust, the soil pipe in painted cast iron gone black.
	MatPipeIron = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.703f));
	MatLead = Build.Flat(FLinearColor(0.022f, 0.022f, 0.024f), 0.75f, 0.3f);

	// The machines' enamel: white once, ivory now, and grimy. Held near the plaster's value: under the
	// lantern a "white" machine at a real white's albedo is the brightest thing in the cellar.
	MatEnamel = Build.Flat(FLinearColor(0.095f, 0.090f, 0.078f), 0.55f);
	MatChrome = Build.Flat(FLinearColor(0.30f, 0.29f, 0.28f), 0.5f, 1.f);
	MatRubber = Build.Flat(FLinearColor(0.012f, 0.012f, 0.012f), 0.8f);
	MatDrum = Build.Flat(FLinearColor(0.10f, 0.10f, 0.10f), 0.55f, 0.8f);
	// The porthole: thick glass, clear but filmed. The glass rule: nearly not there (10-08).
	MatPortGlass = Build.Glass(FLinearColor(0.045f, 0.046f, 0.044f), 0.10f, 0.30f);
	MatDial = Build.Flat(FLinearColor(0.14f, 0.13f, 0.11f), 0.6f);
	MatInk = Build.Flat(FLinearColor(0.010f, 0.009f, 0.008f), 0.6f);
	MatKnob = Build.Flat(FLinearColor(0.020f, 0.012f, 0.008f), 0.35f);
	MatShadow = Build.Flat(FLinearColor(0.008f, 0.007f, 0.006f), 1.f);
	MatCopper = Build.Flat(FLinearColor(0.10f, 0.05f, 0.025f), 0.6f, 0.6f);
	// Brass gone brown and iron gone to rust; green_metal_rust needs its corrected tint or it is green paint.
	MatBrass = Sheet(Build, RoomSurfaces::RustedIron, FLinearColor(1.0f, 0.36f, 0.24f), 0.7f);
	MatIron = Sheet(Build, RoomSurfaces::RustedIron, FLinearColor(0.847f, 0.448f, 0.704f));
	MatZinc = Sheet(Build, RoomSurfaces::RustedIron, FLinearColor(1.02f, 0.54f, 0.85f), 0.8f);
	// The sink's glaze: the cracked-plaster photograph's crazing at the kitchen china's ivory.
	MatCeramic = Sheet(Build, RoomSurfaces::Plaster, FLinearColor(0.28f, 0.27f, 0.25f), 0.8f);
	// The cupboards' grey paint: the cracked-plaster photograph, whose crazing reads as old paint
	// cracked on wood. On the planks photograph (the kitchen's dresser) the seams read as boarding.
	MatPaint = Sheet(Build, RoomSurfaces::Plaster, FLinearColor(0.135f, 0.145f, 0.160f), 1.1f);
	MatWood = Sheet(Build, RoomSurfaces::RoughWood, FLinearColor(0.45f, 0.42f, 0.40f));
	// The folding table's top, scrubbed pale for forty years, then left.
	MatScrubbed = Sheet(Build, RoomSurfaces::RoughWood, FLinearColor(0.62f, 0.57f, 0.50f));
	MatWicker = Sheet(Build, RoomSurfaces::RoughWood, FLinearColor(0.74f, 0.60f, 0.40f), 1.3f);
	MatStraw = Sheet(Build, RoomSurfaces::RoughWood, FLinearColor(0.66f, 0.48f, 0.22f), 1.6f);
	MatStrings = Build.Flat(FLinearColor(0.085f, 0.078f, 0.064f), 0.95f);
	// green_metal_rust as it comes: green paint with rust through it, which is what a tin box is.
	MatTin = Sheet(Build, RoomSurfaces::RustedIron, FLinearColor(0.42f, 0.42f, 0.42f), 0.9f);
	MatToolPaint = Sheet(Build, RoomSurfaces::RustedIron, FLinearColor(0.55f, 0.50f, 0.48f), 0.9f);

	// Cloth: rough_linen at the drapes' 34cm, a blue photograph, so every tint is solved backwards
	// from it (09-20). The whites stay near a tenth; the girl's blouse a faded pink.
	MatShirt = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.40f, 0.27f, 0.16f));
	MatShirtBlue = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.20f, 0.19f, 0.17f));
	MatBlouse = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.42f, 0.18f, 0.13f));
	MatGreyCloth = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.22f, 0.15f, 0.10f));
	MatTowel = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.38f, 0.25f, 0.14f), 1.2f);
	MatTowelBlue = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.19f, 0.185f, 0.17f), 1.2f);
	MatSheet = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.401f, 0.27f, 0.16f));
	MatCover = Sheet(Build, RoomSurfaces::Drapery, FLinearColor(0.22f, 0.14f, 0.075f), 1.1f);

	if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_laundry_paper.MI_laundry_paper")))
	{
		// The packaging's labels on the models' own UVs, which address the atlas directly.
		MatLabel = UMaterialInstanceDynamic::Create(Parent, this);
		MatLabel->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.26f, 0.24f, 0.21f));
		MatLabel->SetScalarParameterValue(TEXT("RoughnessScale"), 1.f);
		MatLabel->SetVectorParameterValue(TEXT("TilingXY"), FLinearColor(1.f, 1.f, 0.f, 1.f));
		MatLabel->SetVectorParameterValue(TEXT("UVOffset"), FLinearColor(0.f, 0.f, 0.f, 1.f));
	}
	else
	{
		MatLabel = Build.Flat(FLinearColor(0.15f, 0.13f, 0.10f), 0.9f);
	}
	MatCard = Build.Flat(FLinearColor(0.085f, 0.068f, 0.048f), 0.9f);
	MatAmber = Build.Flat(FLinearColor(0.020f, 0.008f, 0.003f), 0.5f);
	MatPlastic = Build.Flat(FLinearColor(0.07f, 0.06f, 0.025f), 0.6f);
	MatCap = Build.Flat(FLinearColor(0.045f, 0.008f, 0.006f), 0.6f);
	MatSoap = Build.Flat(FLinearColor(0.11f, 0.09f, 0.05f), 0.7f);
	MatBakelite = Build.Flat(FLinearColor(0.018f, 0.010f, 0.006f), 0.35f);
	MatGrille = Build.Flat(FLinearColor(0.050f, 0.040f, 0.026f), 0.95f);
	MatGlove = Build.Flat(FLinearColor(0.075f, 0.040f, 0.010f), 0.5f);
	MatCushion = Build.Flat(FLinearColor(0.050f, 0.008f, 0.006f), 0.9f);
	for (const FLinearColor& Thread : { FLinearColor(0.055f, 0.008f, 0.008f), FLinearColor(0.008f, 0.010f, 0.030f), FLinearColor(0.12f, 0.11f, 0.09f) })
	{
		MatThreads.Add(Build.Flat(Thread, 0.85f));
	}
	MatWeb = Build.Cobweb(RoomPalette::Web);
	MatVoid = Build.Flat(RoomPalette::Void, 1.f);
	MatBulb = Build.Glass(FLinearColor(0.06f, 0.06f, 0.055f), 0.22f, 0.3f);
}

// ---------------------------------------------------------------------------------------------
// The shell: floor, walls in render over brick, the ceiling's boards and joists, the doorway.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildShell(FRoomBuilder& Build)
{
	const float T = WallThickness;
	const float WH = Width * 0.5f;
	const float DH = Depth * 0.5f;
	const float Outer = WH + T * 0.5f;

	// The floor and the ceiling stop at the middle of the door wall: past it is the corridor's floor
	// (the bare rooms' rule — a face shared with it would flicker).
	Build.Box(FVector(0.f, T * 0.25f, -5.f), FRotator::ZeroRotator, FVector(Width + T, Depth + T * 0.5f, 10.f), MatFlags);
	Build.Box(FVector(0.f, T * 0.25f, Height + 5.f), FRotator::ZeroRotator, FVector(Width + T, Depth + T * 0.5f, 10.f), MatBoards);
	// Joists across the short span, and one beam the length of the room under them.
	for (float X = -HalfX + 26.f; X < HalfX - 10.f; X += 52.f)
	{
		Build.Box(FVector(X, 0.f, Height - 9.f), FRotator::ZeroRotator, FVector(10.f, HalfY * 2.f, 18.f), MatJoist, false);
	}
	Build.Box(FVector(0.f, 60.f, Height - 18.f - 11.f), FRotator::ZeroRotator, FVector(HalfX * 2.f, 20.f, 22.f), MatJoist, false);

	// The door wall in two skins: render inside, the corridor's brick outside. Every piece gets its
	// repeats written out for its own face.
	const float DoorL = DoorX - DoorHalf;
	const float DoorR = DoorX + DoorHalf;
	for (const float Skin : { -1.f, 1.f })
	{
		const FRoomSurface& Set = Skin > 0.f ? RoomSurfaces::Plaster : RoomSurfaces::Substrate;
		const FLinearColor& Tint = Skin > 0.f ? LaundryRenderTint : LaundryCorridorBrickTint;
		const float Y = -DH + Skin * T * 0.25f;
		const float LeftW = DoorL + Outer;
		const float RightW = Outer - DoorR;
		WallPiece(Build, Set, Tint, FVector(-Outer + LeftW * 0.5f, Y, Height * 0.5f), 0.f, LeftW, Height, T * 0.5f);
		WallPiece(Build, Set, Tint, FVector(DoorR + RightW * 0.5f, Y, Height * 0.5f), 0.f, RightW, Height, T * 0.5f);
		WallPiece(Build, Set, Tint, FVector(DoorX, Y, (DoorHeight + Height) * 0.5f), 0.f, DoorHalf * 2.f, Height - DoorHeight, T * 0.5f);
	}
	// Rough timber lining the doorway, a lintel, and a step worn hollow (the bare rooms' doorway).
	for (const float Jamb : { -1.f, 1.f })
	{
		Build.Box(FVector(DoorX + Jamb * (DoorHalf - 3.f), -DH, DoorHeight * 0.5f), FRotator::ZeroRotator, FVector(6.f, T + 2.f, DoorHeight), MatTimber);
	}
	Build.Box(FVector(DoorX, -DH, DoorHeight - 4.f), FRotator::ZeroRotator, FVector(DoorHalf * 2.f, T + 2.f, 8.f), MatTimber, false);
	Build.Box(FVector(DoorX, -DH, 1.f), FRotator::ZeroRotator, FVector(DoorHalf * 2.f - 12.f, T + 2.f, 2.f), MatTimber);

	// The far (south) wall, solid: render inside. Nothing is behind it but the earth.
	WallPiece(Build, RoomSurfaces::Plaster, LaundryRenderTint, FVector(0.f, DH, Height * 0.5f), 0.f, Width + T, Height, T);
	// The east and west walls, between the two.
	for (const float Side : { -1.f, 1.f })
	{
		WallPiece(Build, RoomSurfaces::Plaster, LaundryRenderTint, FVector(Side * WH, 0.f, Height * 0.5f), 90.f, Depth - T, Height, T);
	}
}

// ---------------------------------------------------------------------------------------------
// Pipes: the mains along the walls under the ceiling, the drops, the valves, the soil pipe.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildPipes(FRoomBuilder& Build)
{
	// Four centimetres off the render, as clipped pipe runs; cold over hot.
	const float NorthY = -HalfY + 4.f;
	const float EastX = HalfX - 4.f;
	const float SouthY = HalfY - 4.f;
	const float ColdZ = 270.f;
	const float HotZ = 262.f;
	const float D = 2.8f;
	const FVector HeaterAt = Heater();
	const float ColdIn = HeaterAt.X + 14.f;
	const float HotOut = HeaterAt.X - 14.f;
	const float HeaterPipeY = HeaterAt.Y - 4.f;
	// The taps over the sink (make_laundry.py: taps), the model turned to face north.
	const float TapZ = SinkTop + 22.f;
	const float ColdTapX = SinkX + 11.f;
	const float HotTapX = SinkX - 11.f;

	// The cold main: in through the door wall from the corridor, along the north wall, down the east
	// wall to the corner and along the south wall to the sink.
	Pipe(Build, { FVector(-30.f, -HalfY - 2.f, ColdZ), FVector(-30.f, NorthY, ColdZ), FVector(EastX, NorthY, ColdZ), FVector(EastX, SouthY, ColdZ),
		FVector(ColdTapX, SouthY, ColdZ), FVector(ColdTapX, SouthY, TapZ + 70.f) }, D, MatPipeIron, 110.f);
	// Into the heater, and out of it as the hot main, which follows the cold round to the sink.
	Pipe(Build, { FVector(ColdIn, NorthY, ColdZ), FVector(ColdIn, HeaterPipeY, ColdZ), FVector(ColdIn, HeaterPipeY, HeaterTop + 26.f) }, D, MatPipeIron);
	Pipe(Build, { FVector(HotOut, HeaterPipeY, HeaterTop + 26.f), FVector(HotOut, HeaterPipeY, HotZ), FVector(HotOut, NorthY, HotZ), FVector(EastX, NorthY, HotZ),
		FVector(EastX, SouthY, HotZ), FVector(HotTapX, SouthY, HotZ), FVector(HotTapX, SouthY, TapZ + 70.f) }, D, MatPipeIron, 110.f);
	// Gate valves on the heater's pipes, wheels to the room.
	for (const float X : { ColdIn, HotOut })
	{
		Place(Build, LaundryProps::Valve, FVector(X, HeaterPipeY, HeaterTop + 52.f), FRotator(90.f, 180.f, 0.f), { { TEXT("Brass"), MatBrass }, { TEXT("Iron"), MatIron } });
	}
	// The drops to the washer, each with its valve, into the wall behind the machine.
	const FVector WasherAt = Washer();
	for (const float Y : { WasherAt.Y - 14.f, WasherAt.Y + 4.f })
	{
		const float Z = Y < WasherAt.Y ? ColdZ : HotZ;
		Pipe(Build, { FVector(EastX, Y, Z), FVector(EastX, Y, MachineTop + 20.f), FVector(HalfX + 2.f, Y, MachineTop + 20.f) }, D, MatPipeIron);
		Place(Build, LaundryProps::Valve, FVector(EastX, Y, MachineTop + 44.f), FRotator(90.f, 180.f, 0.f), { { TEXT("Brass"), MatBrass }, { TEXT("Iron"), MatIron } });
	}
	// The gas to the heater's thermostat, low along the east wall.
	Pipe(Build, { FVector(HeaterAt.X + 22.f, HeaterAt.Y + HeaterRadius - 4.f, 30.f), FVector(EastX, HeaterAt.Y + HeaterRadius - 4.f, 30.f),
		FVector(EastX, HeaterAt.Y + HeaterRadius - 4.f, 2.f) }, 2.2f, MatPipeIron);

	// The soil pipe from the house above, across the room under the joists: painted cast iron gone
	// black, hung on straps, sockets every length. One socket has wept for years: a rust run down from
	// it and a drip mark on the floor under it (BuildFloor).
	const float SoilY = -110.f;
	const float SoilZ = Height - 18.f - 6.5f;
	Pipe(Build, { FVector(-HalfX - 4.f, SoilY, SoilZ), FVector(HalfX + 4.f, SoilY, SoilZ) }, 11.f, MatLead, 150.f);
	for (float X = -HalfX + 52.f; X < HalfX; X += 156.f)
	{
		Build.Box(FVector(X, SoilY, SoilZ + 6.f), FRotator::ZeroRotator, FVector(2.f, 13.f, 1.f), MatIron, false);
	}
	Build.Stain(RoomSurfaces::RustedIron, FVector(30.f, SoilY, SoilZ - 8.f), FRotator(90.f, 0.f, 0.f), FVector2D(12.f, 18.f), FLinearColor(0.35f, 0.16f, 0.10f), 0.7f, 1.2f);
}

// ---------------------------------------------------------------------------------------------
// The machines: the heater in the corner, the washer with the wash still in it, the dryer.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildMachines(FRoomBuilder& Build)
{
	const FVector HeaterAt = Heater();
	KeepWallDecalsOff(Place(Build, LaundryProps::Heater, HeaterAt, FRotator::ZeroRotator,
		{ { TEXT("Enamel"), MatEnamel }, { TEXT("Iron"), MatIron }, { TEXT("Shadow"), MatShadow }, { TEXT("Copper"), MatCopper },
		  { TEXT("Brass"), MatBrass }, { TEXT("Dial"), MatDial }, { TEXT("Knob"), MatKnob } }));
	Blocker(Build, HeaterAt + FVector(0.f, 0.f, 90.f), FVector(60.f, 60.f, 180.f));

	auto MachineSlots = [this](UStaticMeshComponent* Mesh)
	{
		Dress(Mesh, { { TEXT("Enamel"), MatEnamel }, { TEXT("Chrome"), MatChrome }, { TEXT("Rubber"), MatRubber }, { TEXT("Drum"), MatDrum },
			{ TEXT("Shadow"), MatShadow }, { TEXT("Dial"), MatDial }, { TEXT("Ink"), MatInk }, { TEXT("Knob"), MatKnob }, { TEXT("Copper"), MatCopper } });
		KeepWallDecalsOff(Mesh);
	};
	// Both face the room (-X): turned so the models' +Y front looks west.
	const float Yaw = 90.f;
	const FRotator Facing(0.f, Yaw, 0.f);
	auto Hinge = [&](const FVector& Machine) { return Machine + Facing.RotateVector(FVector(DoorHingeX, DoorHingeY, PortZ)); };

	// The washer, its door swung open, the wash still in the drum and a sleeve over the lip: the clue.
	const FVector WasherAt = Washer();
	MachineSlots(Build.Prop(LaundryProps::Washer, WasherAt, Facing, 0.f, false));
	Blocker(Build, WasherAt + FVector(0.f, 0.f, 45.f), FVector(62.f, 60.f, 90.f), Yaw);
	Place(Build, LaundryProps::MachineDoor, Hinge(WasherAt), FRotator(0.f, Yaw + 74.f, 0.f),
		{ { TEXT("Chrome"), MatChrome }, { TEXT("Glass"), MatPortGlass }, { TEXT("Knob"), MatKnob } });
	if (AClueActor* Clue = SpawnClue(WasherAt, Facing))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		Dress(B.Prop(LaundryProps::WashLoad, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
			{ { TEXT("ClothA"), MatShirtBlue }, { TEXT("ClothB"), MatSheet }, { TEXT("ClothC"), MatBlouse }, { TEXT("ClothD"), MatGreyCloth } });
	}

	// The dryer, shut, empty.
	const FVector DryerAt = Dryer();
	MachineSlots(Build.Prop(LaundryProps::Dryer, DryerAt, Facing, 0.f, false));
	Blocker(Build, DryerAt + FVector(0.f, 0.f, 45.f), FVector(62.f, 60.f, 90.f), Yaw);
	Place(Build, LaundryProps::MachineDoor, Hinge(DryerAt), Facing, { { TEXT("Chrome"), MatChrome }, { TEXT("Glass"), MatPortGlass }, { TEXT("Knob"), MatKnob } });

	// Grime down the machines' fronts and rust round their feet and the washer's hinge, and rust where
	// the heater's jacket has worn through under the thermostat: decals aimed at their faces.
	for (const FVector& At : { WasherAt, DryerAt })
	{
		const float Front = At.X - MachineDepth * 0.5f;
		Build.Stain(RoomSurfaces::RustedIron, FVector(Front - 4.f, At.Y, 10.f), FRotator::ZeroRotator, FVector2D(56.f, 18.f), FLinearColor(0.30f, 0.14f, 0.08f), 0.7f, 1.3f);
		Build.Stain(RoomSurfaces::Damp, FVector(Front - 4.f, At.Y, 50.f), FRotator::ZeroRotator, FVector2D(60.f, 80.f), FLinearColor(0.30f, 0.27f, 0.22f), 0.45f, 1.4f);
	}
	Build.Stain(RoomSurfaces::RustedIron, FVector(WasherAt.X - MachineDepth * 0.5f - 4.f, WasherAt.Y + DoorHingeX, PortZ), FRotator::ZeroRotator, FVector2D(10.f, 30.f),
		FLinearColor(0.30f, 0.14f, 0.08f), 0.6f, 1.2f);
	Build.Stain(RoomSurfaces::RustedIron, HeaterAt + FVector(0.f, HeaterRadius + 4.f, 20.f), FRotator(0.f, -90.f, 0.f), FVector2D(40.f, 34.f), FLinearColor(0.30f, 0.14f, 0.08f), 0.7f, 1.3f);
	Build.Stain(RoomSurfaces::Damp, HeaterAt + FVector(0.f, HeaterRadius + 4.f, 90.f), FRotator(0.f, -90.f, 0.f), FVector2D(50.f, 140.f), FLinearColor(0.28f, 0.25f, 0.20f), 0.5f, 1.4f);

	// The basket she was filling, or emptying, on the floor in front of them: half full, a sleeve and
	// the girl's dress over its rim. Clear of the washer's door at its widest.
	const FVector BasketAt(194.f, -98.f, 0.f);
	const FRotator BasketTurn(0.f, 12.f, 0.f);
	Place(Build, LaundryProps::Basket, BasketAt, BasketTurn, { { TEXT("Wicker"), MatWicker } });
	Place(Build, LaundryProps::BasketClothes, BasketAt, BasketTurn,
		{ { TEXT("ClothA"), MatShirtBlue }, { TEXT("ClothB"), MatShirt }, { TEXT("ClothC"), MatBlouse }, { TEXT("ClothD"), MatGreyCloth } });
	Blocker(Build, BasketAt + FVector(0.f, 0.f, 15.f), FVector(62.f, 44.f, 30.f), BasketTurn.Yaw);
}

// ---------------------------------------------------------------------------------------------
// The sink, and the folding table beside it with the shelf over it.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildSinkAndTable(FRoomBuilder& Build)
{
	FRandomStream Random(5410);
	const FRotator North(0.f, 180.f, 0.f);

	const FVector SinkAt = Sink();
	KeepWallDecalsOff(Place(Build, LaundryProps::Sink, SinkAt, North, { { TEXT("Ceramic"), MatCeramic }, { TEXT("Shadow"), MatShadow }, { TEXT("Iron"), MatIron } }));
	Blocker(Build, SinkAt + FVector(0.f, 0.f, 45.f), FVector(78.f, 52.f, 90.f));
	KeepWallDecalsOff(Place(Build, LaundryProps::Taps, FVector(SinkX, HalfY, SinkTop + 22.f), North,
		{ { TEXT("Brass"), MatBrass }, { TEXT("Iron"), MatIron }, { TEXT("Ceramic"), MatCeramic } }));
	// A rag left in the bottom of the sink, dried stiff, and the stain of the drip under the cold tap.
	Place(Build, LaundryProps::Rag, SinkAt + FVector(-12.f, 4.f, SinkTop - 23.f), FRotator(0.f, 40.f, 0.f), { { TEXT("ClothD"), MatGreyCloth } });
	Build.Stain(RoomSurfaces::RustedIron, SinkAt + FVector(11.f, -6.f, SinkTop - 19.f), FRotator(-90.f, 0.f, 0.f), FVector2D(12.f, 20.f),
		FLinearColor(0.30f, 0.14f, 0.08f), 0.65f, 1.1f);

	// The folding table: what she had out on it.
	const FVector CounterAt = Counter();
	KeepWallDecalsOff(Place(Build, LaundryProps::Counter, CounterAt, North, { { TEXT("Wood"), MatScrubbed } }));
	Blocker(Build, CounterAt + FVector(0.f, 0.f, 45.f), FVector(CounterLength + 2.f, CounterDepth + 2.f, 90.f));
	auto On = [&](float X, float Y) { return FVector(CounterAt.X + X, CounterAt.Y + Y, CounterTop); };
	Place(Build, LaundryProps::TowelStack, On(-74.f, 6.f), FRotator(0.f, 183.f, 0.f), { { TEXT("TowelA"), MatTowel }, { TEXT("TowelB"), MatTowelBlue } });
	Place(Build, LaundryProps::SheetStack, On(-30.f, 8.f), FRotator(0.f, 178.f, 0.f), { { TEXT("Sheet"), MatSheet } });
	// The sewing box open, its lid against the wall; a pair of gloves; the peg tin; the iron left out
	// on its rest; the soap.
	Place(Build, LaundryProps::SewingKit, On(16.f, 4.f), FRotator(0.f, 168.f, 0.f),
		{ { TEXT("Tin"), MatTin }, { TEXT("ThreadA"), MatThreads[0] }, { TEXT("ThreadB"), MatThreads[1] }, { TEXT("ThreadC"), MatThreads[2] },
		  { TEXT("Wood"), MatWood }, { TEXT("Cushion"), MatCushion }, { TEXT("Iron"), MatIron }, { TEXT("Chrome"), MatChrome } });
	Place(Build, LaundryProps::Gloves, On(-2.f, -16.f), FRotator(0.f, 30.f, 0.f), { { TEXT("Rubber"), MatGlove } });
	Place(Build, LaundryProps::PegTin, On(48.f, 14.f), FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f),
		{ { TEXT("Tin"), MatTin }, { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron } });
	Build.Box(On(62.f, -12.f) + FVector(0.f, 0.f, 0.4f), FRotator(0.f, 20.f, 0.f), FVector(16.f, 24.f, 0.8f), MatIron, false);
	Place(Build, LaundryProps::Iron, On(62.f, -12.f) + FVector(0.f, 0.f, 0.8f), FRotator(0.f, 200.f, 0.f),
		{ { TEXT("Chrome"), MatChrome }, { TEXT("Bakelite"), MatBakelite }, { TEXT("Rubber"), MatRubber } });
	Place(Build, LaundryProps::Soap, On(88.f, 12.f), FRotator(0.f, 170.f, 0.f),
		{ { TEXT("Ceramic"), MatCeramic }, { TEXT("Soap"), MatSoap }, { TEXT("Card"), MatCard }, { TEXT("Label"), MatLabel } });
	// The note, on the table in front of the towels, with a stub of pencil: the clue.
	if (AClueActor* Clue = SpawnClue(On(-52.f, -18.f), FRotator(0.f, 7.f, 0.f)))
	{
		FRoomBuilder B(Clue, Clue->GetRootScene());
		// Read standing at the table, facing the wall: the top of the sheet towards the wall (+Y).
		Paper(B, TEXT("note_a"), FVector(0.f, 0.f, 0.06f), FVector::UpVector, FVector(0.f, 1.f, 0.f), 12.f, 18.f);
		B.Cyl(FVector(9.f, -3.f, 0.45f), FRotator(90.f, 60.f, 0.f), FVector(0.9f, 0.9f, 8.f), MatWood, false);
	}
	// Under it, on the slats: two baskets, empty, and the household toolbox.
	Place(Build, LaundryProps::Basket, FVector(CounterAt.X - 58.f, CounterAt.Y + 2.f, 18.1f), FRotator(0.f, 178.f, 0.f), { { TEXT("Wicker"), MatWicker } });
	Place(Build, LaundryProps::Toolbox, FVector(CounterAt.X + 40.f, CounterAt.Y + 6.f, 18.1f), FRotator(0.f, 184.f, 0.f),
		{ { TEXT("Paint"), MatToolPaint }, { TEXT("Iron"), MatIron } });

	// The shelf over the table, and what she bought: powder, starch, rinse, the cleaner, and the
	// radio at the end, its dial towards the room. Labels to the room, a few turned.
	const FVector ShelfAt = WallShelf();
	KeepWallDecalsOff(Place(Build, LaundryProps::WallShelf, ShelfAt, North, { { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron } }));
	auto Shelf = [&](float X) { return FVector(ShelfAt.X + X, ShelfAt.Y - WallShelfDepth * 0.5f, ShelfAt.Z); };
	auto Packet = [&](const TCHAR* Name, float X, float Turn)
	{
		KeepWallDecalsOff(Place(Build, Name, Shelf(X), FRotator(0.f, 180.f + Turn, 0.f),
			{ { TEXT("Card"), MatCard }, { TEXT("Shadow"), MatShadow }, { TEXT("Label"), MatLabel }, { TEXT("Bottle"), Name == LaundryProps::Spray ? MatPlastic.Get() : MatAmber.Get() },
			  { TEXT("Cap"), MatCap } }));
	};
	Packet(LaundryProps::Detergent, -62.f, 4.f);
	Packet(LaundryProps::Starch, -44.f, -6.f);
	Packet(LaundryProps::Softener, -28.f, 12.f);
	Packet(LaundryProps::Softener, -16.f, -20.f);
	Packet(LaundryProps::Spray, -2.f, 30.f);
	Packet(LaundryProps::Spray, 9.f, -15.f);
	KeepWallDecalsOff(Place(Build, LaundryProps::Radio, Shelf(44.f), FRotator(0.f, 184.f, 0.f),
		{ { TEXT("Wood"), MatWood }, { TEXT("Grille"), MatGrille }, { TEXT("Label"), MatLabel }, { TEXT("Ink"), MatInk }, { TEXT("Bakelite"), MatBakelite },
		  { TEXT("Rubber"), MatRubber } }));
	// The radio's flex, down from the shelf to a socket on the wall under it.
	Build.Cyl(FVector(ShelfAt.X + 60.f, HalfY - 1.2f, ShelfAt.Z - 30.f), FRotator::ZeroRotator, FVector(0.6f, 0.6f, 56.f), MatRubber, false);
	Build.Box(FVector(ShelfAt.X + 60.f, HalfY - 1.5f, ShelfAt.Z - 60.f), FRotator::ZeroRotator, FVector(8.f, 3.f, 8.f), MatBakelite, false);

	// The stool, pulled out from the table.
	Place(Build, LaundryProps::Stool, FVector(CounterAt.X - 18.f, CounterAt.Y - 70.f, 0.f), FRotator(0.f, 25.f, 0.f), { { TEXT("Wood"), MatWood } });
	Blocker(Build, FVector(CounterAt.X - 18.f, CounterAt.Y - 70.f, 23.f), FVector(36.f, 36.f, 46.f));
}

// ---------------------------------------------------------------------------------------------
// The west wall: the cupboards, the open shelves, the ironing board, the baskets in the corner.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildWestWall(FRoomBuilder& Build)
{
	FRandomStream Random(5420);
	// The cupboards face east (-90 turns the models' +Y front to +X).
	const FRotator East(0.f, -90.f, 0.f);
	const FVector Shut = Cupboard(0);
	KeepWallDecalsOff(Place(Build, LaundryProps::Cabinet, Shut, East, { { TEXT("Paint"), MatPaint }, { TEXT("Iron"), MatIron }, { TEXT("Shadow"), MatShadow } }));
	Blocker(Build, Shut + FVector(0.f, 0.f, CabinetHeight * 0.5f), FVector(CabinetWidth, CabinetDepth, CabinetHeight), -90.f);
	const FVector Open = Cupboard(1);
	KeepWallDecalsOff(Place(Build, LaundryProps::CabinetOpen, Open, East,
		{ { TEXT("Paint"), MatPaint }, { TEXT("Iron"), MatIron }, { TEXT("Shadow"), MatShadow }, { TEXT("Sheet"), MatSheet }, { TEXT("TowelA"), MatTowel } }));
	Blocker(Build, Open + FVector(0.f, 0.f, CabinetHeight * 0.5f), FVector(CabinetWidth, CabinetDepth, CabinetHeight), -90.f);
	// Its right-hand door, standing open: hinged at the carcass's right front corner, a negative turn
	// swinging it out (make_laundry.py: cabinet_door).
	const FVector DoorHinge = Open + East.RotateVector(FVector(CabinetWidth * 0.5f, CabinetDepth * 0.5f, 0.f));
	Place(Build, LaundryProps::CabinetDoor, DoorHinge, FRotator(0.f, East.Yaw - 64.f, 0.f), { { TEXT("Paint"), MatPaint }, { TEXT("Iron"), MatIron } });
	// Paint worn back to the wood on the fronts, low and round the knobs, where hands and knees went.
	for (const FVector& At : { Shut, Open })
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const float Y = Random.FRandRange(-38.f, 38.f);
			const float Z = i == 0 ? Random.FRandRange(14.f, 40.f) : Random.FRandRange(60.f, 190.f);
			const FVector2D Size(Random.FRandRange(10.f, 26.f), Random.FRandRange(12.f, 30.f));
			const float Roll = Random.FRandRange(0.f, 360.f);
			Build.Stain(RoomSurfaces::RoughWood, FVector(At.X + CabinetDepth * 0.5f + 6.f, At.Y + Y, Z), FRotator(0.f, 180.f, Roll), Size,
				FLinearColor(0.30f, 0.24f, 0.18f), 0.8f, 1.4f);
		}
	}

	// The open shelves in the corner by the door: linen, spare packets, the toolbox's tins, a bucket.
	const FVector ShelvesAt = Shelving();
	KeepWallDecalsOff(Place(Build, LaundryProps::Shelving, ShelvesAt, East, { { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron } }));
	Blocker(Build, ShelvesAt + FVector(0.f, 0.f, 95.f), FVector(100.f, ShelvesDepth, 190.f), -90.f);
	// On a shelf at height Z, U along it from its middle (the model's X), V out from its middle.
	auto OnShelf = [&](float Z, float U, float V) { return ShelvesAt + East.RotateVector(FVector(U, V, 0.f)) + FVector(0.f, 0.f, Z + 1.1f); };
	Place(Build, LaundryProps::TowelStack, OnShelf(50.f, -24.f, 0.f), FRotator(0.f, -90.f + 2.f, 0.f), { { TEXT("TowelA"), MatTowelBlue }, { TEXT("TowelB"), MatTowel } });
	Place(Build, LaundryProps::SheetStack, OnShelf(50.f, 24.f, 0.f), FRotator(0.f, -92.f, 0.f), { { TEXT("Sheet"), MatSheet } });
	Place(Build, LaundryProps::SheetStack, OnShelf(94.f, -22.f, 0.f), FRotator(0.f, -88.f, 0.f), { { TEXT("Sheet"), MatSheet } });
	for (int32 i = 0; i < 3; ++i)
	{
		Place(Build, i == 1 ? LaundryProps::Starch : LaundryProps::Detergent, OnShelf(94.f, 14.f + i * 13.f, -4.f), FRotator(0.f, -90.f + Random.FRandRange(-10.f, 10.f), 0.f),
			{ { TEXT("Card"), MatCard }, { TEXT("Shadow"), MatShadow }, { TEXT("Label"), MatLabel } });
	}
	for (int32 i = 0; i < 4; ++i)
	{
		Place(Build, i % 2 ? LaundryProps::Spray : LaundryProps::Softener, OnShelf(138.f, -36.f + i * 11.f, Random.FRandRange(-6.f, 4.f)),
			FRotator(0.f, -90.f + Random.FRandRange(-30.f, 30.f), 0.f),
			{ { TEXT("Bottle"), i % 2 ? MatPlastic.Get() : MatAmber.Get() }, { TEXT("Cap"), MatCap }, { TEXT("Label"), MatLabel } });
	}
	Place(Build, LaundryProps::TowelStack, OnShelf(138.f, 24.f, 0.f), FRotator(0.f, -88.f, 0.f), { { TEXT("TowelA"), MatTowel }, { TEXT("TowelB"), MatTowel } });
	Place(Build, LaundryProps::Basket, OnShelf(182.f, 0.f, 2.f), FRotator(0.f, -90.f, 0.f), { { TEXT("Wicker"), MatWicker } });
	Place(Build, LaundryProps::Rag, OnShelf(6.f, -20.f, 0.f), FRotator(0.f, 10.f, 0.f), { { TEXT("ClothD"), MatGreyCloth } });
	Place(Build, LaundryProps::Detergent, OnShelf(6.f, 24.f, 2.f), FRotator(0.f, -70.f, 0.f), { { TEXT("Card"), MatCard }, { TEXT("Shadow"), MatShadow }, { TEXT("Label"), MatLabel } });

	// The ironing board, folded and stood against the wall south of the cupboards, leaning back on it:
	// its folded legs to the room, which is what says ironing board and not a slab.
	const float Lean = 11.f;
	const FQuat BoardTurn = LaundryLeaning(90.f, Lean, FVector(-1.f, 0.f, 0.f));
	const float BoardFoot = -HalfX + 3.6f + IroningLength * FMath::Sin(FMath::DegreesToRadians(Lean));
	KeepWallDecalsOff(Place(Build, LaundryProps::IroningBoard, FVector(BoardFoot, 148.f, 0.f), BoardTurn.Rotator(),
		{ { TEXT("Wood"), MatWood }, { TEXT("Cover"), MatCover }, { TEXT("Iron"), MatIron }, { TEXT("Rubber"), MatRubber } }));

	// The corner: two baskets one inside the other, and a third on its side by them.
	Place(Build, LaundryProps::Basket, FVector(-HalfX + 40.f, HalfY - 34.f, 0.f), FRotator(0.f, 84.f, 0.f), { { TEXT("Wicker"), MatWicker } });
	Place(Build, LaundryProps::Basket, FVector(-HalfX + 40.f, HalfY - 34.f, 4.f), FRotator(0.f, 76.f, 0.f), { { TEXT("Wicker"), MatWicker } });
	Blocker(Build, FVector(-HalfX + 40.f, HalfY - 34.f, 18.f), FVector(64.f, 46.f, 36.f), 84.f);
}

// ---------------------------------------------------------------------------------------------
// Drying: the airer by the east wall, and the line across the room.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildDrying(FRoomBuilder& Build)
{
	// The airer, along the wall: its length along Y.
	const FVector AirerAt = Airer();
	const FRotator AirerTurn(0.f, 90.f, 0.f);
	Place(Build, LaundryProps::Airer, AirerAt, AirerTurn, { { TEXT("Iron"), MatChrome }, { TEXT("Rubber"), MatRubber } });
	Blocker(Build, AirerAt + FVector(0.f, 0.f, 48.f), FVector(AirerLength, AirerSplay * 2.f + 4.f, 96.f), AirerTurn.Yaw);
	auto Local = [&](float U, float V, float Z) { return AirerAt + AirerTurn.RotateVector(FVector(U, V, 0.f)) + FVector(0.f, 0.f, Z); };

	// Shirts on their hangers from the top bar, hung in the middle of the A between the two frames;
	// the hook's curl rests on the bar (make_laundry.py: SHIRT_HOOK).
	const float HangZ = AirerTop + AirerTube - (ShirtHook + 0.18f);
	struct FHung { const TCHAR* Name; float U; float V; UMaterialInterface* Mat; };
	const FHung Hung[] = {
		{ LaundryProps::Shirt, -31.f, -1.2f, MatShirt },
		{ LaundryProps::Shirt, 8.f, 1.0f, MatShirtBlue },
		{ LaundryProps::ChildTop, 38.5f, -0.6f, MatBlouse },
	};
	for (const FHung& Each : Hung)
	{
		Place(Build, Each.Name, Local(Each.U, Each.V, HangZ), AirerTurn,
			{ { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatChrome }, { TEXT("Cloth"), Each.Mat }, { TEXT("ClothC"), Each.Mat }, { TEXT("Dial"), MatDial } });
	}
	// A towel over the middle rail and the girl's socks over the bottom one, on the room's side.
	auto RailV = [&](float Z) { return AirerSplay * (AirerTop - Z) / (AirerTop - 1.f); };
	Place(Build, LaundryProps::TowelHung, Local(-24.f, RailV(56.f), 56.f), AirerTurn, { { TEXT("TowelB"), MatTowel } });
	Place(Build, LaundryProps::Socks, Local(30.f, RailV(34.f), 34.f), AirerTurn, { { TEXT("ClothB"), MatShirt } });

	// The line: from an eye in the west wall to one in the east, sagging. A pillowcase and a towel
	// pegged on it, stiff as boards.
	const float Sag = 7.f;
	auto LineAt = [&](float X) { return LineZ - Sag * (1.f - FMath::Square(X / HalfX)); };
	TArray<FVector> Line;
	for (int32 i = 0; i <= 8; ++i)
	{
		const float X = -HalfX + 2.f * HalfX * i / 8.f;
		Line.Add(FVector(X, LineY, LineAt(X)));
	}
	for (int32 i = 0; i + 1 < Line.Num(); ++i)
	{
		const FVector Span = Line[i + 1] - Line[i];
		Build.Cyl((Line[i] + Line[i + 1]) * 0.5f, FRotationMatrix::MakeFromZ(Span).Rotator(), FVector(0.45f, 0.45f, Span.Size()), MatStrings, false);
	}
	for (const float Side : { -1.f, 1.f })
	{
		Build.Cyl(FVector(Side * (HalfX - 3.f), LineY, LineAt(HalfX)), FRotator(90.f, 0.f, 0.f), FVector(0.6f, 0.6f, 6.f), MatIron, false);
		Build.Cyl(FVector(Side * (HalfX - 6.f), LineY, LineAt(HalfX)), FRotator::ZeroRotator, FVector(2.6f, 2.6f, 0.6f), MatIron, false);
	}
	struct FPegged { const TCHAR* Name; float X; float Width; UMaterialInterface* Mat; const TCHAR* Slot; };
	const FPegged Pegged[] = {
		{ LaundryProps::Pillowcase, -150.f, 48.f, MatSheet, TEXT("Sheet") },
		{ LaundryProps::TowelHung, -56.f, 52.f, MatTowelBlue, TEXT("TowelB") },
	};
	for (const FPegged& Each : Pegged)
	{
		Place(Build, Each.Name, FVector(Each.X, LineY, LineAt(Each.X)), FRotator::ZeroRotator, { { Each.Slot, Each.Mat } });
		for (const float Side : { -1.f, 1.f })
		{
			// A peg clipped over the line at each corner, legs down: the model lies along its own X.
			const float X = Each.X + Side * (Each.Width * 0.5f - 4.f);
			Place(Build, LaundryProps::Peg, FVector(X, LineY, LineAt(X) - 1.6f), FRotator(90.f, 90.f, 0.f), { { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron } });
		}
	}
	// Spare pegs left on the line between them.
	for (const float X : { -110.f, 30.f, 70.f })
	{
		Place(Build, LaundryProps::Peg, FVector(X, LineY, LineAt(X) - 1.6f), FRotator(90.f, 90.f, 0.f), { { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron } });
	}
}

// ---------------------------------------------------------------------------------------------
// The door wall: the keys, the calendar, the week's schedule and a note.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildDoorWall(FRoomBuilder& Build)
{
	const float Face = -HalfY;
	// Pinned to the wall: none of the wall's decals on any of it, and none put near it.
	auto Pinned = [this](AActor* Clue)
	{
		TInlineComponentArray<UPrimitiveComponent*> Parts(Clue);
		for (UPrimitiveComponent* Part : Parts)
		{
			Part->SetReceivesDecals(false);
		}
		KeepWallDecalsOff(Clue);
	};

	// The keys on their board by the door, where she hung them.
	if (AClueActor* Keys = SpawnClue(FVector(-14.f, Face, 152.f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Keys, Keys->GetRootScene());
		Dress(B.Prop(RoomProps::CellarKeyRack, FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false),
			{ { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron }, { TEXT("Brass"), MatBrass }, { TEXT("Tag"), MatCard } });
		Pinned(Keys);
	}
	// The month on the wall, hung on a nail, crossed off day by day to the tenth.
	if (AClueActor* Calendar = SpawnClue(FVector(46.f, Face + 0.8f, 152.f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Calendar, Calendar->GetRootScene());
		Paper(B, TEXT("calendar"), FVector::ZeroVector, FVector(0.f, 1.f, 0.f), FVector::UpVector, 30.f, 42.f);
		B.Cyl(FVector(0.f, 0.4f, 19.6f), FRotator(90.f, 0.f, 0.f), FVector(0.5f, 0.5f, 2.f), MatIron, false);
		Pinned(Calendar);
	}
	// The week's laundry, ruled up by hand and pinned at its corners; one corner has lost its pin.
	if (AClueActor* Schedule = SpawnClue(FVector(118.f, Face + 0.8f, 150.f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Schedule, Schedule->GetRootScene());
		Paper(B, TEXT("schedule"), FVector::ZeroVector, FVector(0.f, 1.f, 0.f), FVector::UpVector, 42.f, 29.7f);
		for (const FVector2D Pin : { FVector2D(-20.f, 13.8f), FVector2D(20.f, 13.8f), FVector2D(-20.f, -13.8f) })
		{
			B.Sph(FVector(Pin.X, 0.4f, Pin.Y), 1.1f, MatBrass);
		}
		B.Box(FVector(19.f, 1.2f, -13.f), FRotator(0.f, 0.f, 16.f), FVector(6.f, 0.3f, 4.f), MatCard, false);
		Pinned(Schedule);
	}
	// A note pinned beside it: a list, ticked off partway.
	if (AClueActor* Note = SpawnClue(FVector(162.f, Face + 0.8f, 134.f), FRotator::ZeroRotator))
	{
		FRoomBuilder B(Note, Note->GetRootScene());
		Paper(B, TEXT("note_b"), FVector::ZeroVector, FVector(0.f, 1.f, 0.f), FVector::UpVector, 12.f, 18.f);
		B.Sph(FVector(0.f, 0.4f, 8.2f), 1.1f, MatBrass);
		Pinned(Note);
	}

	// The light: a bare bulb on its flex from the beam, dead. Nobody has turned it on in years.
	const FVector Rose(0.f, -6.f, Height - 18.f);
	Build.Cyl(Rose + FVector(0.f, 0.f, -1.5f), FRotator::ZeroRotator, FVector(8.f, 8.f, 3.f), MatBakelite, false);
	Build.Cyl(Rose + FVector(0.f, 0.f, -24.f), FRotator::ZeroRotator, FVector(0.6f, 0.6f, 42.f), MatRubber, false);
	Build.Cyl(Rose + FVector(0.f, 0.f, -48.f), FRotator::ZeroRotator, FVector(3.6f, 3.6f, 6.f), MatBakelite, false);
	Build.Sph(Rose + FVector(0.f, 0.f, -55.f), 6.f, MatBulb);
}

// ---------------------------------------------------------------------------------------------
// The south-east corner: the mop and its bucket, the broom; and the drain in the floor.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildCorner(FRoomBuilder& Build)
{
	const FVector Bucket(HalfX - 36.f, HalfY - 38.f, 0.f);
	KeepWallDecalsOff(Place(Build, LaundryProps::MopBucket, Bucket, FRotator(0.f, -40.f, 0.f),
		{ { TEXT("Zinc"), MatZinc }, { TEXT("Iron"), MatIron }, { TEXT("Wood"), MatWood }, { TEXT("Strings"), MatStrings } }));
	Blocker(Build, Bucket + FVector(0.f, 0.f, 16.f), FVector(36.f, 36.f, 32.f));
	// The broom, its fan along the wall and its handle leant back on it.
	const float Lean = 10.f;
	const FQuat BroomTurn = LaundryLeaning(90.f, Lean, FVector(0.f, 1.f, 0.f));
	KeepWallDecalsOff(Place(Build, LaundryProps::Broom, FVector(224.f, HalfY - 3.f - 142.f * FMath::Sin(FMath::DegreesToRadians(Lean)), 0.f), BroomTurn.Rotator(),
		{ { TEXT("Straw"), MatStraw }, { TEXT("Wood"), MatWood }, { TEXT("Iron"), MatIron } }));

	// The drain in the middle of the floor, where the water from the machines and the mop went.
	Place(Build, LaundryProps::Drain, FVector(92.f, 18.f, 0.f), FRotator(0.f, 20.f, 0.f), { { TEXT("Iron"), MatIron }, { TEXT("Shadow"), MatShadow } });
}

// ---------------------------------------------------------------------------------------------
// Damp on the walls: the render blown off to the brick, rising damp, cracks, the window's run.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildWallDamp(FRoomBuilder& Build)
{
	// Every decal tested against what stands against its wall and against the openings, each number
	// drawn into a local first and in order, the skipped ones' too (09-29).
	FRandomStream Random(5440);
	const FLinearColor Damp(0.40f, 0.34f, 0.27f);
	const FLinearColor Mould(0.10f, 0.12f, 0.08f);
	const FLinearColor Brick(0.30f, 0.26f, 0.22f);
	struct FFace { EWall Wall; FVector Normal; FVector Along; float Half; };
	const FFace Faces[] = {
		{ EWall::North, FVector(0.f, -1.f, 0.f), FVector(1.f, 0.f, 0.f), HalfX },
		{ EWall::South, FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f), HalfX },
		{ EWall::East, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), HalfY },
		{ EWall::West, FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), HalfY },
	};
	for (const FFace& Face : Faces)
	{
		const float Plane = (FMath::Abs(Face.Normal.X) > 0.5f ? HalfX : HalfY) - 2.f;
		const FRotator Aim = Face.Normal.Rotation();
		// Where the render has come away: the brick under it, in big torn patches, mostly high up and
		// in the corners, where the damp got behind it first.
		for (int32 i = 0; i < 3; ++i)
		{
			const float U = Random.FRandRange(-Face.Half + 30.f, Face.Half - 30.f);
			const float Z = Random.FRandRange(150.f, 260.f);
			const float W = Random.FRandRange(60.f, 140.f);
			const float H = Random.FRandRange(50.f, 110.f);
			const float Roll = Random.FRandRange(-20.f, 20.f);
			if (!WallDecalHitsSomething(Face.Wall, U, Z, W * 0.5f, H * 0.5f))
			{
				Build.Stain(RoomSurfaces::Substrate, Face.Normal * Plane + Face.Along * U + FVector(0.f, 0.f, Z), Aim + FRotator(0.f, 0.f, Roll),
					FVector2D(W, H), Brick, 0.95f, 1.1f);
			}
		}
		// Rising damp along the foot, and mould in it; cracks over it.
		for (int32 i = 0; i < 6; ++i)
		{
			const float U = Random.FRandRange(-Face.Half + 30.f, Face.Half - 30.f);
			const float H = Random.FRandRange(40.f, 120.f);
			const float W = Random.FRandRange(70.f, 160.f);
			const float Opacity = Random.FRandRange(0.45f, 0.75f);
			const bool bMould = Random.FRand() < 0.3f;
			const bool bCrack = Random.FRand() < 0.45f;
			const float CrackW = Random.FRandRange(50.f, 130.f);
			const float CrackH = Random.FRandRange(70.f, 160.f);
			const float CrackZ = Random.FRandRange(90.f, 240.f);
			const float CrackOpacity = Random.FRandRange(0.5f, 0.85f);
			const float Sharpness = Random.FRandRange(16.f, 26.f);
			const float Z = H * 0.4f;
			if (!WallDecalHitsSomething(Face.Wall, U, Z, W * 0.5f, H * 0.5f))
			{
				Build.Stain(RoomSurfaces::Damp, Face.Normal * Plane + Face.Along * U + FVector(0.f, 0.f, Z), Aim, FVector2D(W, H),
					bMould ? Mould : Damp * 0.8f, bMould ? Opacity * 0.7f : Opacity, 1.2f);
			}
			if (bCrack && !WallDecalHitsSomething(Face.Wall, U, CrackZ, CrackW * 0.5f, CrackH * 0.5f))
			{
				Build.Crack(Face.Normal * Plane + Face.Along * U + FVector(0.f, 0.f, CrackZ), Aim, FVector2D(CrackW, CrackH), CrackOpacity, Sharpness);
			}
		}
	}
	// Damp coming through over the sink, where the pipes go into the wall, and mould along the top of
	// the far wall, under the ground outside.
	if (!WallDecalHitsSomething(EWall::South, SinkX + 50.f, 150.f, 20.f, 50.f))
	{
		Build.Stain(RoomSurfaces::Damp, FVector(SinkX + 50.f, HalfY - 2.f, 150.f), FRotator(0.f, 90.f, 0.f), FVector2D(40.f, 100.f), FLinearColor(0.16f, 0.15f, 0.12f), 0.7f, 1.3f);
	}
	Build.Stain(RoomSurfaces::Damp, FVector(SinkX, HalfY - 2.f, Height - 8.f), FRotator(0.f, 90.f, 0.f), FVector2D(220.f, 26.f), Mould, 0.6f, 1.3f);
	// Rust run down the east wall from under each valve.
	for (const float Y : { Washer().Y - 14.f, Washer().Y + 4.f })
	{
		if (!WallDecalHitsSomething(EWall::East, Y, MachineTop + 26.f, 6.f, 14.f))
		{
			Build.Stain(RoomSurfaces::RustedIron, FVector(HalfX - 2.f, Y, MachineTop + 26.f), FRotator::ZeroRotator, FVector2D(10.f, 28.f), FLinearColor(0.30f, 0.14f, 0.08f), 0.6f, 1.2f);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The floor: water from the leaks, rust under the machines, dirt in the corners, cracks.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildFloor(FRoomBuilder& Build)
{
	FRandomStream Random(5450);
	const FLinearColor Water(0.12f, 0.12f, 0.12f);
	const FLinearColor Damp(0.30f, 0.26f, 0.21f);
	const FLinearColor Rust(0.10f, 0.045f, 0.022f);
	const FLinearColor Dirt(0.20f, 0.17f, 0.13f);
	auto FloorStain = [&](const FRoomSurface& Set, const FVector2D& At, const FVector2D& Size, float Roll, const FLinearColor& Tint, float Opacity, float Edge, float Rough)
	{
		if (!FloorDecalReachesDoor(At, LaundryFloorDecalHalfExtent(Size, Roll)))
		{
			Build.Stain(Set, FVector(At.X, At.Y, 4.f), FRotator(-90.f, 0.f, Roll), Size, Tint, Opacity, Edge, Rough);
		}
	};
	// Standing water: under the soil pipe's weeping socket, round the drain, under the sink.
	FloorStain(RoomSurfaces::Floorboards, FVector2D(30.f, -110.f), FVector2D(74.f, 52.f), 20.f, Water, 0.8f, 1.2f, 0.1f);
	FloorStain(RoomSurfaces::Floorboards, FVector2D(92.f, 18.f), FVector2D(110.f, 80.f), 60.f, Water, 0.7f, 1.3f, 0.15f);
	FloorStain(RoomSurfaces::Floorboards, FVector2D(SinkX + 10.f, HalfY - 50.f), FVector2D(60.f, 44.f), -15.f, Water, 0.75f, 1.2f, 0.1f);
	// The tide-marks of water that stood and dried, and rust round the drain's grate.
	FloorStain(RoomSurfaces::Damp, FVector2D(92.f, 18.f), FVector2D(170.f, 140.f), 10.f, Damp, 0.55f, 1.3f, 1.f);
	FloorStain(RoomSurfaces::RustedIron, FVector2D(92.f, 18.f), FVector2D(34.f, 34.f), 0.f, Rust * 2.f, 0.7f, 1.1f, 1.f);
	// Rust under the machines and the heater's legs, spreading out from under their fronts.
	for (const FVector& At : { Washer(), Dryer() })
	{
		FloorStain(RoomSurfaces::RustedIron, FVector2D(At.X - 30.f, At.Y), FVector2D(70.f, 40.f), 0.f, Rust * 2.2f, 0.65f, 1.3f, 1.f);
	}
	FloorStain(RoomSurfaces::RustedIron, FVector2D(Heater().X, Heater().Y), FVector2D(80.f, 80.f), 30.f, Rust * 2.f, 0.6f, 1.3f, 1.f);
	FloorStain(RoomSurfaces::RustedIron, FVector2D(HalfX - 36.f, HalfY - 38.f), FVector2D(44.f, 44.f), 0.f, Rust * 1.6f, 0.55f, 1.2f, 1.f);
	// Damp patches across the flags.
	for (int32 i = 0; i < 12; ++i)
	{
		const float X = Random.FRandRange(-HalfX + 40.f, HalfX - 40.f);
		const float Y = Random.FRandRange(-HalfY + 40.f, HalfY - 40.f);
		const float SizeX = Random.FRandRange(70.f, 170.f);
		const float SizeY = Random.FRandRange(50.f, 130.f);
		const float Roll = Random.FRandRange(0.f, 360.f);
		const float Opacity = Random.FRandRange(0.3f, 0.55f);
		FloorStain(RoomSurfaces::Damp, FVector2D(X, Y), FVector2D(SizeX, SizeY), Roll, Damp, Opacity, 1.3f, 1.f);
	}
	// Dirt driven into the corners and along the foot of the walls, where no mop reached.
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		FloorStain(RoomSurfaces::Damp, FVector2D(Corner.X * (HalfX - 26.f), Corner.Y * (HalfY - 26.f)), FVector2D(100.f, 100.f), 45.f, Dirt, 0.7f, 1.2f, 1.f);
	}
	// Cracks across the flags, a few.
	for (int32 i = 0; i < 5; ++i)
	{
		const float X = Random.FRandRange(-HalfX + 60.f, HalfX - 120.f);
		const float Y = Random.FRandRange(-HalfY + 80.f, HalfY - 80.f);
		const FVector2D Size(Random.FRandRange(60.f, 140.f), Random.FRandRange(40.f, 90.f));
		const float Roll = Random.FRandRange(0.f, 360.f);
		if (!FloorDecalReachesDoor(FVector2D(X, Y), LaundryFloorDecalHalfExtent(Size, Roll)))
		{
			Build.Crack(FVector(X, Y, 4.f), FRotator(-90.f, 0.f, Roll), Size, 0.7f, 20.f);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Dust on every top: the machines, the table, the shelves, the cupboards.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildDust(FRoomBuilder& Build)
{
	auto Dust = [&](const FVector& Top, const FVector2D& Size, float Roll, float Opacity)
	{
		Build.Stain(RoomSurfaces::Damp, Top + FVector(0.f, 0.f, 4.f), FRotator(-90.f, 0.f, Roll), Size, LaundryDust, Opacity, 1.5f);
	};
	// Aimed down a decal lays its first size along Y (09-27): sizes are (along Y, along X).
	Dust(Washer() + FVector(0.f, 0.f, MachineTop), FVector2D(62.f, 60.f), 0.f, 0.45f);
	Dust(Dryer() + FVector(0.f, 0.f, MachineTop), FVector2D(62.f, 60.f), 0.f, 0.45f);
	Dust(Counter() + FVector(0.f, 0.f, CounterTop), FVector2D(CounterDepth, CounterLength), 0.f, 0.35f);
	Dust(WallShelf() - FVector(0.f, WallShelfDepth * 0.5f, 0.f), FVector2D(WallShelfDepth, WallShelfLength), 0.f, 0.4f);
	for (int32 i = 0; i < 2; ++i)
	{
		Dust(Cupboard(i) + FVector(0.f, 0.f, CabinetHeight), FVector2D(CabinetWidth, CabinetDepth), 0.f, 0.5f);
	}
	Dust(Shelving() + FVector(0.f, 0.f, 183.f), FVector2D(100.f, ShelvesDepth), 0.f, 0.45f);
}

// ---------------------------------------------------------------------------------------------
// Cobwebs: the ceiling's corners, between the joists, the heater's pipes, under the table.
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildCobwebs(FRoomBuilder& Build)
{
	FRandomStream Random(5460);
	auto Upright = [](float Yaw) { return FRotator(0.f, Yaw, 90.f); };
	// The upper corners of the room.
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f) })
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const float Inset = 22.f + i * 20.f;
			Web(Build, FVector(Corner.X * (HalfX - Inset), Corner.Y * (HalfY - Inset), Height - 22.f - i * 6.f),
				FRotator(0.f, Corner.X * Corner.Y > 0.f ? 45.f : -45.f, 180.f), FVector2D(Inset * 1.6f, Inset * 1.6f));
		}
	}
	// Slung between joists, here and there.
	for (int32 i = 0; i < 7; ++i)
	{
		const float X = -HalfX + 52.f + 52.f * Random.RandRange(0, 10);
		const float Y = Random.FRandRange(-HalfY + 30.f, HalfY - 30.f);
		Web(Build, FVector(X, Y, Height - 20.f), Upright(0.f), FVector2D(42.f, Random.FRandRange(10.f, 20.f)));
	}
	// Between the heater's pipes and the wall; under the table's ends.
	Web(Build, Heater() + FVector(0.f, -18.f, HeaterTop + 40.f), Upright(0.f), FVector2D(36.f, 40.f));
	for (const float Side : { -1.f, 1.f })
	{
		Web(Build, Counter() + FVector(Side * (CounterLength * 0.5f - 8.f), 0.f, 40.f), Upright(90.f), FVector2D(50.f, 30.f));
	}
	Web(Build, Shelving() + FVector(10.f, 0.f, 186.f), Upright(0.f), FVector2D(30.f, 22.f));
	Web(Build, Airer() + FVector(0.f, -AirerLength * 0.5f + 4.f, 20.f), Upright(0.f), FVector2D(30.f, 26.f));
}

// ---------------------------------------------------------------------------------------------
// A thin mist on the floor: a local fog volume, a sphere inside the walls (10-08).
// ---------------------------------------------------------------------------------------------

void ALaundryActor::BuildMist()
{
	// A local fog volume is always a sphere of its largest axis (the wine cellar's 10-08 note): one
	// as big as the room's half-depth allows, its centre on the floor, so the walls contain it.
	const float Base = ULocalFogVolumeComponent::GetBaseVolumeSize();
	const float Radius = HalfY - 10.f;
	const float ScaleHeightCm = 60.f;
	Mist = NewObject<ULocalFogVolumeComponent>(this, TEXT("LaundryMist"));
	if (!Mist)
	{
		return;
	}
	Mist->SetMobility(EComponentMobility::Movable);
	Mist->AttachToComponent(LaundryRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Mist->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
	Mist->SetRelativeScale3D(FVector(Radius / Base));
	Mist->SetRadialFogExtinction(0.f);
	Mist->SetHeightFogExtinction(0.25f);
	Mist->SetHeightFogFalloff(100.f * Radius / ScaleHeightCm);
	Mist->SetHeightFogOffset(0.f);
	Mist->SetFogAlbedo(FLinearColor(0.72f, 0.72f, 0.70f));
	Mist->SetFogPhaseG(0.25f);
	Mist->RegisterComponent();
	AddInstanceComponent(Mist);
}
