#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "TestActor.generated.h"

UCLASS()
class CUSTOMCOMPUTESHADER_API ATestActor : public APawn
{
	GENERATED_BODY()

//Properties
public:
	UPROPERTY()
		USceneComponent* Root;

	UPROPERTY(EditAnywhere)
	UStaticMeshComponent* mesh_8;

	UPROPERTY(EditAnywhere)
	UStaticMeshComponent* mesh_2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ShaderDemo)
	class UTextureRenderTarget2D* Rt_8 = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ShaderDemo)
	UTextureRenderTarget2D* Rt_2 = nullptr;

	// Sets default values for this pawn's properties
	ATestActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void BeginDestroy() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
