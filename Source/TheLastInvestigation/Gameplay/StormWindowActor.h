#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StormWindowActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class UPointLightComponent;
class URectLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class AStormForestActor;
struct FRoomSurface;

/** Geometry of the wall opening this storm is seen through, handed over by the room that spawns it. */
struct FStormWindowSetup
{
	float OpeningWidth = 260.f;
	float SillHeight = 85.f;
	float TopHeight = 250.f;
	float WallThickness = 20.f;

	/**
	 * Whether this actor glazes the opening itself: reveal, sill, sash and panes. False leaves the
	 * opening to whoever spawned it — the stair hall's window is leaded glass, which is its own
	 * construction — and builds only the curtains and everything outside.
	 */
	bool bGlazed = true;

	/**
	 * How far the inner sill stands out into the room past the wall face. A deep sill is a shelf,
	 * and over a worktop (the kitchen's, ten centimetres under its windows) it was a slab hanging
	 * over the sink with the taps through it. Zero lines the bottom of the reveal and no more,
	 * and leaves the room to put its own sill board in front. The apron under it is only built
	 * for a sill deep enough to need one.
	 */
	float InnerSillProud = 33.f;

	/**
	 * How many panes across the sash. Four keeps every window as it was; a wider opening needs more,
	 * or its panes come out as wide flat letterboxes (Room01's 280 opening has six, near square).
	 * The rows stay four: the meeting rail is the middle muntin.
	 */
	int32 PaneColumns = 4;

	/**
	 * For a follower (see SetLead) on another side of the house. The lead's directional light
	 * cannot come in through a window facing away from it, so an own-view follower carries the
	 * strike's shadows itself: its glow is moved far out, made to cast, and thrown somewhere new on
	 * every strike. It also has its own rain. The ground, the forest and the sky it looks out on
	 * are the one set round the whole house (AStormForestActor); it used to build its own.
	 */
	bool bOwnView = false;

	/**
	 * For a follower on the lead's side of the house but well along from it: its own rain. The
	 * lead's rain falls in a band in front of the lead's window, and eight metres along the same
	 * wall there is none. (The name is from when such a window also planted its own trees; the
	 * forest is all round the house now.)
	 */
	bool bOwnTrees = false;

	/** Multiplies the sky portal, for an opening whose glass lets through less than a clear pane. */
	float PortalScale = 1.f;

	/**
	 * Multiplies a follower's flash glow. The glow of a window without a view of its own is an
	 * unshadowed point light 24m across, so it lights every room near it through the walls (the 10-05
	 * cellar note). Zero leaves the strike to the sky portal in the opening, which floods tenfold with
	 * the flash and lights only through the glass: for windows built next to rooms that already have
	 * their own (the library's three, beside the girl's room and over the living room).
	 */
	float GlowScale = 1.f;

	// The drapes' cut. The defaults are every window's linen curtains as they have always been; the
	// library's are theatre velvet: wider, in deep folds, to the floor, tied back on gold cords, gold
	// fringe along what is left of the hem.

	/** How far each drape reaches from the side of the opening towards its middle. */
	float DrapeWidth = 64.f;
	/** Scales the folds' depth and spacing together, so the slope (and so the look of cloth) holds. */
	float DrapeFold = 1.f;
	/** How far down the drop each drape is tied back, as a fraction; zero hangs it straight. */
	float DrapeTie = 0.f;
	/** The side (0 or 1) whose tieback has given way: that drape hangs loose. */
	int32 DrapeTieGone = -1;
	/** Down to the floor, rather than thirty centimetres past the opening. */
	bool bDrapeToFloor = false;
	/** How far the pole stands out from the wall: further for heavy folds that must clear the sill. */
	float PoleProud = 16.f;
	/** Fringe, braid, cord and tassels; null for none. */
	UMaterialInterface* CurtainTrim = nullptr;

	/**
	 * Seed for this window's own layout: which panes are broken, where the curtains are torn, where
	 * the trees stand. Zero keeps the default for its role (one for the lead, one for a follower),
	 * so every follower past the first needs its own, or it is the first one's window again.
	 */
	int32 Seed = 0;

	/**
	 * What the curtains are made of. Null is the house's grey linen; the girl's room hangs a pink
	 * rose print. The surface must tile at Drapery's 34cm, since the drapes' UVs are laid out in it.
	 */
	const FRoomSurface* CurtainSurface = nullptr;
	/** The curtains' tint on that surface; the default is the house's grey worked out for linen. */
	FLinearColor CurtainTint = FLinearColor(0.268f, 0.184f, 0.118f);
};

/**
 * Everything on the far side of the glass, plus the storm lighting that reaches into the room.
 *
 * Owns three things that have to stay in sync: the window itself (frame, cracked glass, torn
 * curtains), the rain falling past it, and the lightning. The lightning is a *shadow-casting
 * directional light* rather than the point light the greybox used, because the brief's "long
 * moving shadows" only happen if the flash comes from far away and outside — a point light at the
 * window casts shadows that fan out from the opening instead. What the window looks out on (the
 * ground, the forest, the sky with its clouds, moon and lightning) is AStormForestActor, which the
 * lead window spawns and drives.
 *
 * Convention: the actor is spawned facing so that +X is outward, away from the room.
 */
UCLASS()
class AStormWindowActor : public AActor
{
	GENERATED_BODY()

public:
	AStormWindowActor();

	/** Must be called before BeginPlay (i.e. via a deferred spawn) so the frame matches the wall gap. */
	void Configure(const FStormWindowSetup& InSetup);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 0..1 gust strength, shared so wallpaper strips elsewhere in the room breathe with the same wind. */
	float GetWindGust() const;

	/** 0..1 how bright the current lightning flash is, for anything that needs to react to it. */
	float GetFlashAlpha() const { return FlashAlpha; }

	/** Lux of the strike currently in progress, before FlashAlpha scales it. */
	float GetStrikeIntensity() const { return StrikeIntensity; }

	/**
	 * The reveal lining's thickness: it lines the opening on both sides and over the head, so it
	 * stands this far past the opening into the wall face, and down to this far under the sill. A
	 * room that trims the window on its own side (AMasterBedroomActor's architrave) starts outside it.
	 */
	static constexpr float RevealLining = 6.f;

private:
	/** See SetViewUnderground and SetViewSealed. One player, one world: plain statics are enough. */
	static inline bool bViewUnderground = false;
	static inline bool bViewSealed = false;

public:

	/**
	 * Whether the player's eye is underground (set by ACellarActor every frame). While it is, every
	 * window's glow stays dark. The glows of windows without a view of their own are unshadowed and
	 * light everything within 24m whatever is in the way, so for them this is the only fix. The
	 * own-view glows cast shadows, and came through only because the ground outside did not: the
	 * cellar runs out under it, and its own thin ceiling did not stop them. The ground casts shadows
	 * now (checked with -StormFlash, the own-view glows left on underground: the cellar stayed dark),
	 * so for those this is a second guard rather than the fix.
	 */
	static void SetViewUnderground(bool bUnderground) { bViewUnderground = bUnderground; }

	/**
	 * Whether the player's eye is in a room with no window (set by ABathroomActor when it changes).
	 * Nothing of the storm should light such a room, and the unshadowed glows of the windows near it
	 * would, through its walls: while this is set every window's glow stays dark, as underground,
	 * and the rest of the storm's light stays at its floor (LitFlash).
	 */
	static void SetViewSealed(bool bSealed) { bViewSealed = bSealed; }

private:
	/**
	 * The flash as the storm's lights carry it: FlashAlpha, or nothing while the eye is in a sealed
	 * room. Turning the glows off was not enough there: the volumetric fog is one volume through
	 * every wall, and the sky portals' and the directional light's strike lit the bathroom's air
	 * through its walls, a faint cold haze over the dark tiles on every flash. Checked with
	 * -StormFlash and the detective started in the room (-StartX/Y/Z): a -RoomShot camera moved in
	 * half a second before the capture still shows the fog as it was seen from outside. The sky
	 * still flashes and GetFlashAlpha still reports it; only the light stays at its floor.
	 */
	float LitFlash() const { return bViewSealed ? 0.f : FlashAlpha; }

public:

	/** Counts strikes since BeginPlay, so a listener can tell one flash from the next. */
	int32 GetStrikeCount() const { return StrikeCount; }

	/**
	 * Where each tieback cord is made fast on the wall beside the window, in world space: one per
	 * drape that is tied back with a trim (FStormWindowSetup::DrapeTie, CurtainTrim), filled when the
	 * window is built. The hook stands a centimetre off the plaster, inside the reach of any decal
	 * laid on that wall, so the room that owns the wall keeps its damp off it.
	 */
	const TArray<FVector>& GetTieHooks() const { return TieHooks; }

	/**
	 * The middle of each curtain pole bracket, in world space, filled when the window is built. A
	 * bracket is PoleBracketSection square and runs back from the pole to within a few centimetres of
	 * the plaster, inside the reach of any decal laid on that wall.
	 */
	const TArray<FVector>& GetPoleBrackets() const { return PoleBrackets; }
	static constexpr float PoleBracketSection = 4.f;

	/**
	 * Makes this a second window onto another storm rather than a storm of its own. Must be called
	 * before BeginPlay.
	 *
	 * There is one sky over the house. A second window has to flash when the first one does, and it
	 * must not bring a second directional light with it — two of them make the renderer pick one
	 * arbitrarily for the fog and the translucency. So a follower builds only its window, its
	 * curtains and its own sky portal, looks out at the lead's forest and sky, and takes its flash
	 * from the lead every frame.
	 */
	void SetLead(AStormWindowActor* InLead)
	{
		Lead = InLead;
		if (InLead)
		{
			// The lead's flash has to be this frame's before the follower reads it.
			AddTickPrerequisiteActor(InLead);
		}
	}

private:
	void BuildWindow();
	void BuildRain();
	void TickCurtains(float DeltaTime);
	void TickRain(float DeltaTime);
	void TickLightning(float DeltaTime);
	void BeginStrike();

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<USceneComponent> StormRoot;

	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UDirectionalLightComponent> LightningLight;

	/** Sits just outside the opening; gives the flash a bright near-field core the directional light can't. */
	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<UPointLightComponent> LightningGlow;

	/**
	 * The overcast sky, as a light the size of the window and standing just outside it.
	 *
	 * This is what actually lights the room between strikes, and it replaces trying to do that job
	 * with the distant directional light — which could not do it: at the shallow angle the long
	 * shadows need, its beam spends forty metres inside the treeline before it ever reaches the
	 * glass, so the room stayed black no matter how many lux it was given. A rect light in the
	 * opening is also simply what is physically there — a window is an area source of sky — and
	 * because it sits *outside* the glass, the muntin bars throw their grid across the far wall.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Storm")
	TObjectPtr<URectLightComponent> SkyPortal;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Curtains;

	/**
	 * How each curtain moves with the gust, parallel to Curtains: X scales the sideways swing (roll
	 * about the pole), Y the billow into the room (pitch). (1, 0) for every curtain but the long ones.
	 */
	TArray<FVector2D> CurtainSwing;

	/** See GetTieHooks. */
	TArray<FVector> TieHooks;
	/** See GetPoleBrackets. */
	TArray<FVector> PoleBrackets;

	/** The world outside, on the lead window only: ground, forest, sky. */
	UPROPERTY(Transient)
	TObjectPtr<AStormForestActor> Forest;

	int32 StrikeCount = 0;

	/** An own-view follower's copy of the lead's strike count, so each strike is placed once. */
	int32 FollowedStrike = 0;

	/** Follower side of TickLightning: the far flash for an own-view window. */
	void FollowOwnView();

	/** How many times over the sky portal brightens at the top of the current flash. */
	float PortalFlood() const;

	/** Set on a follower window; see SetLead. */
	UPROPERTY(Transient)
	TObjectPtr<AStormWindowActor> Lead;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> RainInstances;

	FStormWindowSetup Setup;

	TArray<FVector> RainPositions;
	TArray<float> RainSpeeds;

	/** Scratch buffer for the per-frame instance update, kept alive so the tick allocates nothing. */
	TArray<FTransform> RainTransforms;

	FRandomStream Random;
	float ElapsedTime = 0.f;

	// Lightning state. A strike is a burst of sub-flashes rather than a single blink, which is what
	// makes real lightning read as unpredictable instead of as a metronome: two to five return
	// strokes for a strike to ground, three to eight flickers for a discharge inside the cloud.
	float TimeUntilNextStrike = 4.f;
	int32 SubFlashesRemaining = 0;
	float SubFlashTimer = 0.f;
	bool bSubFlashOn = false;
	float StrikeIntensity = 0.f;
	float FlashAlpha = 0.f;
	/** Whether the discharge in progress is a strike to ground, and whether its channel shows. */
	bool bGroundStrike = false;
	bool bChannel = false;
	/** -StormFlash: the held strike has been given its place in the sky. */
	bool bForcedStrikePlaced = false;

	/**
	 * Seconds between discharges. It was 3.5 to 9, every one a full strike: a strobe. A storm
	 * overhead gives something every ten to twenty-five seconds, and most of that is in the cloud.
	 */
	UPROPERTY(EditAnywhere, Category = "Storm")
	FVector2f StrikeInterval = FVector2f(9.f, 26.f);

	/** How many of the discharges are strikes to ground: the ones that light the room. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float GroundStrikeShare = 0.38f;

	/** Lux at the peak of a strike to ground. Cold blue-white, and bright enough to momentarily flatten the lantern. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	FVector2f StrikeLux = FVector2f(60.f, 150.f);

	/** Lux at the peak of a flicker inside the cloud: a pulse in the window's light, not a flash. */
	UPROPERTY(EditAnywhere, Category = "Storm")
	FVector2f CloudFlashLux = FVector2f(6.f, 22.f);

	UPROPERTY(EditAnywhere, Category = "Storm")
	int32 RainDropCount = 420;

	/**
	 * Lux the storm sky gives off between strikes — the light's floor, never switched off.
	 *
	 * This is the dial for how dark the room is. It has to be high enough to print a pale,
	 * muntin-barred rectangle of window light across the door wall, because that patch is the
	 * room's second subject after the lantern; low enough that the corners the lantern has not
	 * reached are still genuinely dark.
	 */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float StormAmbientLux = 1.4f;

	/**
	 * Candelas of overcast sky coming through the opening. This is the dial for how dark the room
	 * is: it sets the pale rectangle on the door wall, the glow on the sill, and how much of the
	 * far corners the player can make out before the lantern reaches them.
	 */
	UPROPERTY(EditAnywhere, Category = "Storm")
	float SkyPortalCandelas = 310.f;
};
