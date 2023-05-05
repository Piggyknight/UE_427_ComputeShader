// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "comp_parallel_reduction.generated.h"

USTRUCT()
struct FReductionRes
{
	GENERATED_BODY()
	FIntPoint Size;
	FIntPoint DstSize;
	TRefCountPtr<IPooledRenderTarget> PooledMipRt;
	FRDGTextureRef RdgRt;
};

/**
 * 
 */
UCLASS()
class CUSTOMSHADERSDECLARATIONS_API UCompParallelReduction : public UObject
{
	GENERATED_BODY()
public:
	bool IsRenderTargetInited = false;
	bool IsRhiInited = false;
	TArray<FReductionRes> ResArray;

	UPROPERTY()
    UTextureRenderTarget2D* ReadbackRt;
	FRDGTextureRef RdgReadbackRt;
	TRefCountPtr<IPooledRenderTarget> PooledReadbackRt;

	TArray<FColor> ReadBackBuffer;
	
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;

	void InitRenderTarget(UTextureRenderTarget2D* RenderTarget);
	void InitDynamicRhi(FRHICommandListImmediate& RHICmdList);
	void ReleaseRhi();
	void ParallelReductionSum(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture);
	
};
