#pragma once
#include "ComputeShaderDeclaration.h"

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

	class UCompParallelReduction* ReductionComp;

	void Init(UCompParallelReduction* comp);
	void UpdateParameters(FParam_WashCS& DrawParameters);
	void Execute_RenderThread(FRHICommandListImmediate& RHICmdList);
};
