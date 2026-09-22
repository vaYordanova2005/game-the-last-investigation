#include "DoorActor.h"
#include "DetectiveCharacter.h"
#include "RoomBuildLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// HingeRoot sits at the hinge edge; the leaf is offset sideways from it, so rotating the hinge
	// swings the leaf like a real door instead of spinning it around its own centre.
	HingeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HingeRoot"));
	SetRootComponent(HingeRoot);

	// The part that turns, and the reason it is not HingeRoot itself.
	//
	// HingeRoot is the root component, so HingeRoot->GetRelativeRotation() is the actor's rotation
	// in the world, and the actor is spawned yawed ninety degrees to stand the leaf across the
	// doorway. Tick read that ninety, compared it against a closed target of zero, and dutifully
	// turned it to zero — so the door swung itself wide open on the first frame of every session
	// and stayed there. It has been standing open behind a prompt that says it is locked.
	//
	// Swing starts at zero and is the only thing the animation touches, so closed is zero and open
	// is the swing angle, whatever the actor happens to be yawed to in the world.
	Swing = CreateDefaultSubobject<USceneComponent>(TEXT("Swing"));
	Swing->SetupAttachment(HingeRoot);
	Swing->SetMobility(EComponentMobility::Movable);

	DoorLeaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLeaf"));
	DoorLeaf->SetupAttachment(Swing);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		DoorLeaf->SetStaticMesh(CubeMeshFinder.Object);
	}

	// Cube is 100uu per side; scale to a door-leaf slab and offset so the hinge edge lines up with
	// the root.
	//
	// Sized to the opening it is shut into, not six centimetres short of it. The opening is 106 by
	// 208 and the leaf was 100 by 204, which left a finger of doorway uncovered down the lock side
	// and another along the top — and what is behind that doorway is the unlit rest of the house,
	// so those fingers were solid black. A door with a black strip down its edge does not read as
	// a door that is shut; it reads as a door standing ajar.
	const FVector DoorSize(4.5f, 106.f, 210.f);
	DoorLeaf->SetRelativeScale3D(DoorSize / 100.f);
	DoorLeaf->SetRelativeLocation(FVector(0.f, DoorSize.Y * 0.5f, DoorSize.Z * 0.5f));
	DoorLeaf->SetMobility(EComponentMobility::Movable);
	DoorLeaf->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorLeaf->SetCollisionResponseToAllChannels(ECR_Block);
}

void ADoorActor::BeginPlay()
{
	Super::BeginPlay();

	// The door's own stream, for the same reason the clock has one. Every other piece of this
	// room is laid out from a fixed seed so that it is the same room every time the detective
	// wakes in it; the door was the one thing drawing from the global RNG, which meant the rot
	// blooms, the splits and the rust streaks moved every session — on the object the player
	// spends the longest looking at, and the one two -RoomShot frames most need to agree on.
	Random.Initialize(19551104 + 7);

	BuildDoorDetail();
}

void ADoorActor::BuildDoorDetail()
{
	// Detail is attached to the swinging pivot in the leaf's own space, so it swings with the door.
	// Built here rather than in the constructor because FRoomBuilder creates components at runtime.
	FRoomBuilder Build(this, Swing);

	// A panelled interior door that has been shut in a wet house for fifty years.
	//
	// It used to be built the other way round — a painted door with a few patches of bare wood
	// showing through — and painted is exactly what it looked like: a clean, pale, flat-tinted slab
	// with crisp joinery, the newest object in a room where everything else has come apart. The
	// flat tint was most of it. PaintedTrim is an untextured colour at better than twice the
	// reflectance of the plaster around it, so the door was both the brightest thing in that half
	// of the frame and the only thing in it with no surface at all.
	//
	// Now it is wood first: weathered boards carrying the whole leaf, with what is left of the
	// paint sitting on top in shrinking islands. Paint does not come off a door evenly — it holds
	// in the fields and goes first off everything that stands proud — so the islands go in the
	// panels and the joinery is left bare.
	// weathered_brown_planks is already a dark photograph — a fifth reflectance in red and a
	// twenty-fifth in blue — so these tints barely darken it at all. The first pass took it down
	// to charcoal and the door stopped being a door: it read as the unlit hole behind a doorway,
	// which is the exact thing the leaf was widened to stop it looking like. Rotten is not the
	// same as invisible. The leaf sits a little above the floorboards and well under the plaster,
	// which puts it second-darkest of the big surfaces and still readable by lantern.
	UMaterialInstanceDynamic* WoodMat = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.255f, 0.279f, 0.295f));
	// The stiles and rails: the same boards a shade lighter, so the joinery still catches an edge.
	UMaterialInstanceDynamic* FrameMat = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.330f, 0.362f, 0.388f));
	// Rot: the foot of a door standing in a wet room goes black and soft, and then goes.
	UMaterialInstanceDynamic* RotMat = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.197f, 0.205f, 0.220f));
	// What is left of the paint. Still the palest thing on the door, but now a fraction of it, and
	// warm rather than the faintly green PaintedTrim — nothing in this room is allowed to be cool.
	// The fields are boards like everything else. Handing them the paint material filled two
	// thirds of the leaf with the palest colour on the door and undid the whole point of it.
	UMaterialInstanceDynamic* PanelMat = Build.Surface(RoomSurfaces::RoughWood, FLinearColor(0.224f, 0.245f, 0.261f));
	// The lock and the hinges — the two things in this room the detective is going to stare at
	// hardest. green_metal_rust is green paint flat, so a neutral tint left them green; these are
	// the corrected values, see ARoomDressingActor's material block.
	UMaterialInstanceDynamic* IronMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.776f, 0.411f, 0.644f));
	UMaterialInstanceDynamic* RustMat = Build.Surface(RoomSurfaces::RustedIron, FLinearColor(0.988f, 0.407f, 0.499f));
	UMaterialInstanceDynamic* CrackMat = Build.Flat(FLinearColor(0.006f, 0.006f, 0.006f), 1.f);

	if (WoodMat)
	{
		DoorLeaf->SetMaterial(0, WoodMat);
	}

	const float LeafY = 53.f;   // the leaf's centre, offset from the hinge
	const float LeafZ = 103.5f;

	// Which side of the leaf the detective is standing on, and it was the wrong one.
	//
	// The leaf is 4.5 thick, so its faces are at local X = ±2.25. The actor is yawed ninety degrees
	// into the doorway, which maps local +X onto world +Y — and world +Y is *away* from the room,
	// into the unlit rest of the house. Every piece of detail here that was not explicitly mirrored
	// onto both faces was placed at +FaceX: the paint, the rot, the splits, both hinges, the hasp,
	// the padlock, the bolts. All of it has been hanging on the hall side of the door, where there
	// is nothing to light it and nobody to see it, and the face the detective actually looks at has
	// had nothing on it but the bare stiles and rails. That is most of why the door read as an
	// empty doorway: not enough of it was there.
	const float RoomSide = -1.f;
	const float LeafHalf = 2.25f;
	// Local X for something standing Proud centimetres off the leaf's room-side face.
	auto Proud = [&](float By) { return RoomSide * (LeafHalf + By); };
	const float FaceX = Proud(0.8f);

	// Stiles and rails, standing proud of the panel fields. Two boxes of frame per face is all it
	// takes: what the eye reads as a panelled door is the shadow line around each field.
	auto Frame = [&](float Y, float Z, float SizeY, float SizeZ)
	{
		for (int32 Face = 0; Face < 2; ++Face)
		{
			const float X = (Face == 0) ? FaceX : -FaceX;
			Build.Box(FVector(X, Y, Z), FRotator::ZeroRotator, FVector(1.6f, SizeY, SizeZ), FrameMat, /*bBlockingCollision*/ false);
		}
	};

	Frame(LeafY - 44.f, LeafZ, 12.f, 196.f);  // hanging stile
	Frame(LeafY + 44.f, LeafZ, 12.f, 196.f);  // lock stile
	Frame(LeafY, LeafZ + 92.f, 100.f, 12.f);  // top rail
	Frame(LeafY, LeafZ + 8.f, 100.f, 16.f);   // lock rail
	Frame(LeafY, LeafZ - 92.f, 100.f, 14.f);  // bottom rail

	// The panel fields themselves, set back a hair and a shade darker, which is what gives each
	// one its own edge in a room lit from one side.
	for (int32 Column = 0; Column < 2; ++Column)
	{
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const float PanelY = LeafY + (Column == 0 ? -22.f : 22.f);
			const float PanelZ = LeafZ + (Row == 0 ? -50.f : 50.f);
			for (int32 Face = 0; Face < 2; ++Face)
			{
				// Proud of the leaf, not buried in it. At the old offset these sat entirely
				// inside the 4.5-thick leaf and never drew a pixel, so the door has been
				// getting its panelled look from the shadow between the stiles alone — and
				// sitting shallower than the joinery is what makes them read as fields.
				const float X = (Face == 0) ? Proud(0.2f) : -Proud(0.2f);
				Build.Box(FVector(X, PanelY, PanelZ), FRotator::ZeroRotator, FVector(1.f, 32.f, 70.f), PanelMat, /*bBlockingCollision*/ false);
			}
		}
	}

	// There is no paint left on this door, and there is no geometry here pretending there is.
	//
	// The last version scattered nine pale slabs across the fields as surviving flakes. At a hand
	// across, laid flat on a face the player walks right up to, a slab is not a flake — it is a
	// rectangle of a different colour with four straight edges, and nine of them read as sheets of
	// paper stuck to the door. Flaking paint needs a torn alpha, and the honest answer for a door
	// shut in a wet house since the sixties is that the paint went long ago and the wood is the
	// whole surface.

	// Rot, from the floor up. Water gets into the end grain at the foot of a door and travels up
	// it, so the bottom rail goes first and the bottom of each stile goes with it.
	Build.Box(FVector(Proud(0.35f), LeafY, LeafZ - 94.f), FRotator::ZeroRotator, FVector(1.f, 104.f, 20.f), RotMat, /*bBlockingCollision*/ false);
	Build.Box(FVector(-Proud(0.35f), LeafY, LeafZ - 94.f), FRotator::ZeroRotator, FVector(1.f, 104.f, 20.f), RotMat, /*bBlockingCollision*/ false);
	for (int32 i = 0; i < 6; ++i)
	{
		const float BloomY = LeafY + Random.FRandRange(-48.f, 48.f);
		const float BloomZ = LeafZ - 86.f + Random.FRandRange(0.f, 42.f);
		Build.Box(FVector(Proud(0.35f), BloomY, BloomZ), FRotator(0.f, 0.f, Random.FRandRange(-10.f, 10.f)),
			FVector(1.f, Random.FRandRange(12.f, 28.f), Random.FRandRange(14.f, 34.f)), RotMat, /*bBlockingCollision*/ false);
	}

	// Where the rot has gone all the way through: a hole low in one field, with the dark of the
	// hall behind it. It is the only part of the rest of the house the detective can see from in
	// here, and it is the size of a fist.
	Build.Box(FVector(0.f, LeafY - 24.f, LeafZ - 62.f), FRotator(0.f, 0.f, 12.f), FVector(8.f, 13.f, 17.f), CrackMat, /*bBlockingCollision*/ false);

	// Deep splits running with the grain, and the kick marks at the bottom where a boot has been
	// put through the lower panel and the wood has never been replaced.
	for (int32 i = 0; i < 5; ++i)
	{
		const float CrackY = LeafY + Random.FRandRange(-42.f, 42.f);
		// Flat against the face and hair-thin. At better than a centimetre proud and nearly two
		// wide these stood off the door like twigs stuck to it.
		Build.Box(FVector(Proud(0.45f), CrackY, LeafZ + Random.FRandRange(-70.f, 60.f)),
			FRotator(0.f, 0.f, Random.FRandRange(-3.f, 3.f)),
			FVector(0.5f, 0.9f, Random.FRandRange(40.f, 110.f)), CrackMat, /*bBlockingCollision*/ false);
	}

	// Hinges, seized with rust, on the hinge edge.
	for (int32 i = 0; i < 2; ++i)
	{
		const float HingeZ = (i == 0) ? 30.f : 172.f;
		Build.Box(FVector(Proud(0.8f), 8.f, HingeZ), FRotator::ZeroRotator, FVector(1.5f, 22.f, 16.f), RustMat, /*bBlockingCollision*/ false);
		Build.Cyl(FVector(Proud(1.6f), 1.f, HingeZ), FRotator::ZeroRotator, FVector(6.f, 6.f, 20.f), RustMat, /*bBlockingCollision*/ false);
	}

	// The lock: a hasp screwed across the joint between leaf and frame, with a padlock through the
	// staple. It exists to be read, not solved — the way out of this room is not through the lock,
	// and a padlock says that in one glance where a keyhole would invite picking.
	const float LockZ = LeafZ + 8.f;
	// Kept on the leaf. The staple used to sit at LeafY + 62, which is nine centimetres past the
	// lock edge of a 106-wide door, so the padlock hung in mid-air beside it.
	Build.Box(FVector(Proud(1.f), LeafY + 30.f, LockZ), FRotator::ZeroRotator, FVector(2.f, 40.f, 12.f), IronMat, /*bBlockingCollision*/ false);
	Build.Box(FVector(Proud(2.f), LeafY + 46.f, LockZ), FRotator::ZeroRotator, FVector(2.4f, 10.f, 16.f), RustMat, /*bBlockingCollision*/ false);

	// The padlock body, hanging a little off vertical from the staple, and its shackle.
	const FVector LockCenter(Proud(4.f), LeafY + 46.f, LockZ - 16.f);
	Build.Box(LockCenter, FRotator(0.f, 0.f, 7.f), FVector(5.f, 15.f, 19.f), RustMat, /*bBlockingCollision*/ false);
	Build.Cyl(LockCenter + FVector(0.f, -5.f, 12.f), FRotator(0.f, 0.f, 4.f), FVector(2.6f, 2.6f, 16.f), IronMat, /*bBlockingCollision*/ false);
	Build.Cyl(LockCenter + FVector(0.f, 5.f, 12.f), FRotator(0.f, 0.f, 4.f), FVector(2.6f, 2.6f, 16.f), IronMat, /*bBlockingCollision*/ false);
	Build.Cyl(LockCenter + FVector(0.f, 0.f, 20.f), FRotator(0.f, 0.f, 90.f), FVector(2.6f, 2.6f, 10.f), IronMat, /*bBlockingCollision*/ false);

	for (int32 i = 0; i < 4; ++i)
	{
		const float BoltY = LeafY + 18.f + (i % 2) * 22.f;
		const float BoltZ = LockZ - 4.f + (i / 2) * 8.f;
		Build.Sph(FVector(Proud(1.6f), BoltY, BoltZ), 3.4f, IronMat);
	}

	// Rust bleeding down the wood from every piece of iron on the door. This is the part that
	// actually says rusted-shut: the hinges and the hasp can be as corroded as you like, and until
	// the water running off them has been staining the door under them for fifty years, what you
	// have is a sound door with old fittings on it.
	const float BleedFrom[3] = { 30.f, 172.f, LockZ };
	const float BleedY[3] = { 8.f, 8.f, LeafY + 46.f };
	for (int32 i = 0; i < 3; ++i)
	{
		for (int32 Streak = 0; Streak < 3; ++Streak)
		{
			const float Length = Random.FRandRange(24.f, 62.f);
			Build.Box(
				FVector(Proud(0.45f), BleedY[i] + Random.FRandRange(-9.f, 9.f), BleedFrom[i] - Length * 0.5f),
				FRotator(0.f, 0.f, Random.FRandRange(-2.f, 2.f)),
				FVector(1.2f, Random.FRandRange(2.5f, 6.f), Length),
				RustMat,
				/*bBlockingCollision*/ false);
		}
	}
}

void ADoorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float CurrentYaw = Swing->GetRelativeRotation().Yaw;
	if (!FMath::IsNearlyEqual(CurrentYaw, TargetYaw, 0.1f))
	{
		const float NewYaw = FMath::FInterpTo(CurrentYaw, TargetYaw, DeltaTime, 2.5f);
		Swing->SetRelativeRotation(FRotator(0.f, NewYaw, 0.f));
	}
}

void ADoorActor::Interact(AActor* Interactor)
{
	if (bIsLocked)
	{
		const ADetectiveCharacter* Detective = Cast<ADetectiveCharacter>(Interactor);
		if (Detective && Detective->bHasRoomKey)
		{
			bIsLocked = false;
		}
		else
		{
			return; // still locked, no key yet — prompt stays "It's locked."
		}
	}

	bIsOpen = !bIsOpen;
	TargetYaw = bIsOpen ? 100.f : 0.f;
}

FText ADoorActor::GetInteractPrompt(const AActor* Interactor) const
{
	if (bIsLocked)
	{
		// The same test Interact makes. Without it the player picks the key off the sill, walks
		// back to the door and is told it is locked — the one moment in this room where the way
		// out has been found, and nothing on screen admits it.
		const ADetectiveCharacter* Detective = Cast<const ADetectiveCharacter>(Interactor);
		if (!Detective || !Detective->bHasRoomKey)
		{
			return FText::FromString(TEXT("Locked. The iron has rusted into the frame."));
		}
		return FText::FromString(TEXT("[E] Unlock the door"));
	}
	return bIsOpen ? FText::FromString(TEXT("[E] Close door")) : FText::FromString(TEXT("[E] Open door"));
}
