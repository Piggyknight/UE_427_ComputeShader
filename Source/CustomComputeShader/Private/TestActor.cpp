#include "TestActor.h"
#include "Kismet/GameplayStatics.h"
#include "CustomShadersDeclarations/Private/ctrl_wash_effect.h"
#include "CustomShadersDeclarations/Private/ComputeShaderDeclaration.h"
#include "CustomShadersDeclarations/Private/comp_parallel_reduction.h"

// Sets default values
ATestActor::ATestActor()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	mesh_8 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("mesh_1"));
	mesh_2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Static Mesh"));
}

// Called when the game starts or when spawned
void ATestActor::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInstanceDynamic* MID = mesh_8->CreateAndSetMaterialInstanceDynamic(0);
	MID->SetTextureParameterValue(TEXT("InputTexture"), (UTexture*)Rt_8);

	MID = mesh_2->CreateAndSetMaterialInstanceDynamic(0);
	

	Reduction = NewObject<UCompParallelReduction>(this);
	Reduction->InitRenderTarget(Rt_8);
	MID->SetTextureParameterValue(TEXT("InputTexture"), (UTexture*)Reduction->ReadbackRt);
	FCtrl_WashEffect::Get()->Init(Reduction);
}

void ATestActor::BeginDestroy()
{
	Super::BeginDestroy();
}

void ATestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//Update parameters
	FParam_WashCS parameters;
	parameters.RenderTarget = Rt_8;
	parameters.RenderTargetMips = Reduction->ReadbackRt;
	parameters.WashStrength = FVector4(0.01f, 0.005f, 0.01f, 1.f);
	parameters.TexelScaleOffset = FVector4(1.0f, 1.0f, 0.f, 0.f);
	parameters.DbgName = TEXT("ATestActor");
	parameters.KSize = 1;
	parameters.HitPointUvList.Add(FVector2D(0.5f, 0.5f));

	FCtrl_WashEffect::Get()->UpdateParameters(parameters);
}