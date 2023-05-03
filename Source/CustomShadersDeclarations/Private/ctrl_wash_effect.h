#pragma once
#include "ComputeShaderDeclaration.h"


struct FRDGMipTex
{
	FIntPoint Size;
	FIntPoint DstSize;
	TRefCountPtr<IPooledRenderTarget> PooledMipRt;
	FRDGTextureRef RdgMipRt;
};

/// <summary>
/// A singleton Shader Manager for our Shader Type
/// </summary>
class CUSTOMSHADERSDECLARATIONS_API FCtrl_WashEffect
{
public:
	//The singleton instance
	static FCtrl_WashEffect* instance;

	//Get the instance
	static FCtrl_WashEffect* Get()
	{
		if (!instance)
			instance = new FCtrl_WashEffect();
		return instance;
	};

	volatile bool bCachedParamsAreValid;
	FParam_WashCS cachedParams;
	TResourceArray<FVector2D> HitPointResArray;


	TRefCountPtr<IPooledRenderTarget> PooledRt;
	TRefCountPtr<IPooledRenderTarget> PooledMipRt;
	FRDGTextureRef RdgOutRt;
	FRDGTextureRef RdgMipRt;

	TArray<FColor> ReadBackBuffer;

	bool IsInited = false;
	TArray<FRDGMipTex> MipTexArray;

	void InitMips(FRHICommandListImmediate& RHICmdList, FRDGTextureRef Texture);
	void UpdateParameters(FParam_WashCS& DrawParameters);
	void Execute_RenderThread(FRHICommandListImmediate& RHICmdList);
	void ParallelReductionSum(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture);
	void ReadBackTexture(FRDGBuilder& GraphBuilder);
};
