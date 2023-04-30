#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"

struct  FParam_WashCS
{
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	UTextureRenderTarget2D* RenderTargetMips;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	FVector4 WashStrength = FVector4(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	FVector4 TexelScaleOffset = FVector4(1.f, 1.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	int	KSize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	TArray<FVector2D> HitPointUvList;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WashEffectCsParameters")
	FName DbgName = TEXT("default");
};

