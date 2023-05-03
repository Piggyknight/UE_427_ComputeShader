#include "ctrl_wash_effect.h"

#include "ComputeShaderDeclaration.h"

#include "GenerateMips.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "ShaderCompilerCore.h"
#include "Modules/ModuleManager.h"

#define NUM_THREADS_PER_GROUP_DIMENSION 8

class FParallelReductionMipsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FParallelReductionMipsCS);
	SHADER_USE_PARAMETER_STRUCT(FParallelReductionMipsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InTex)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutResult)
	END_SHADER_PARAMETER_STRUCT()
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		//OutEnvironment.SetDefine(TEXT("THREADS_X"), FComputeShaderUtils::kGolden2DGroupSize);
		//OutEnvironment.SetDefine(TEXT("THREADS_Y"), FComputeShaderUtils::kGolden2DGroupSize);
		//OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
	
};

class FParallelReductionShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FParallelReductionShader);
	SHADER_USE_PARAMETER_STRUCT(FParallelReductionShader, FGlobalShader);


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, SourceTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint32>, SumResult)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		const FPermutationDomain PermutationVector(Parameters.PermutationId);

		OutEnvironment.SetDefine(TEXT("THREADS_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};

class FPC_WashCS : public FGlobalShader
{
public:
	//DECLARE_EXPORTED_SHADER_TYPE(FWashEffectCS,Global,);
	DECLARE_GLOBAL_SHADER(FPC_WashCS);
	SHADER_USE_PARAMETER_STRUCT(FPC_WashCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutWashTex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float2>, InHitPoints)
		SHADER_PARAMETER(FVector4, TexelScaleOffset)
		SHADER_PARAMETER(FVector4, WashStrength)
		SHADER_PARAMETER(int, PointCount)
		SHADER_PARAMETER(int, KSize)
		END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

// This will tell the engine to create the shader and where the shader entry point is.
//                        ShaderType              ShaderPath             Shader function name    Type
IMPLEMENT_GLOBAL_SHADER(FPC_WashCS, "/CustomShaders/compute_wash.usf", "MainComputeShader", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FParallelReductionShader, "/CustomShaders/parallel_reduction_sum.usf", "main", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FParallelReductionMipsCS, "/CustomShaders/parallel_reduction.usf", "main", SF_Compute);

//Static members
FCtrl_WashEffect* FCtrl_WashEffect::instance = nullptr;


//Update the parameters by a providing an instance of the Parameters structure used by the shader manager
void FCtrl_WashEffect::UpdateParameters(FParam_WashCS& params)
{
	cachedParams = params;
	bCachedParamsAreValid = true;

	ENQUEUE_RENDER_COMMAND(UpdateParameters)(
		[this](FRHICommandListImmediate& RHICmdList)
		{
			//Update the parameters on the render thread
			this->Execute_RenderThread(RHICmdList);
		}
	);
}

TArray<FRHIGPUBufferReadback*> StreamingRequestReadbackBuffers;
uint32 MaxStreamingReadbackBuffers = 1;
uint32 ReadbackBuffersWriteIndex = 0;
uint32 ReadbackBuffersNumPending = 0;

void FCtrl_WashEffect::Execute_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	if (!(bCachedParamsAreValid && cachedParams.RenderTarget))
		return;

	bCachedParamsAreValid = false;

	FRHIGPUBufferReadback* LatestReadbackBuffer = nullptr;
	{
		while (ReadbackBuffersNumPending > 0)
		{
			auto Index = (ReadbackBuffersWriteIndex + MaxStreamingReadbackBuffers - ReadbackBuffersNumPending) % MaxStreamingReadbackBuffers;
			if (StreamingRequestReadbackBuffers[Index]->IsReady())
			{
				ReadbackBuffersNumPending--;
				LatestReadbackBuffer = StreamingRequestReadbackBuffers[Index];
			}
			else
			{
				break;
			}
		}
	}

	if (LatestReadbackBuffer)
	{
		uint32* Buffer = (uint32*)LatestReadbackBuffer->Lock(sizeof(uint32) * 1);

		UE_LOG(LogTemp, Log, TEXT("Buffer: %d"), Buffer[0]);

		LatestReadbackBuffer->Unlock();
	}
	
	
	check(IsInRenderingThread());
	FRDGBuilder GraphBuilder(RHICmdList);

	if (!StreamingRequestReadbackBuffers.IsValidIndex(0))
	{
		StreamingRequestReadbackBuffers.AddZeroed(4);
	}

	if (!StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex])
	{
		StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex] = new FRHIGPUBufferReadback(TEXT("ParallelReductionSumShader.BufferReadBack"));
	}

	
	UTextureRenderTarget2D* Rt = cachedParams.RenderTarget;
	UTextureRenderTarget2D* MipRt = cachedParams.RenderTargetMips;
	FIntPoint RtSize = FIntPoint(Rt->SizeX, Rt->SizeY);
	

	if (!PooledRt.IsValid())
	{
		FPooledRenderTargetDesc ComputeShaderOutputDesc(FPooledRenderTargetDesc::Create2DDesc(RtSize
																							, Rt->GetRenderTargetResource()->TextureRHI->GetFormat()
																							, FClearValueBinding::Black
																							, TexCreate_ShaderResource
																							, TexCreate_ShaderResource | TexCreate_UAV
																							, false));

		ComputeShaderOutputDesc.DebugName = TEXT("WashTempPoolRt");
		GRenderTargetPool.FindFreeElement(RHICmdList
										, ComputeShaderOutputDesc
										, PooledRt
										, TEXT("WashTempPoolRt"));
	}

	if (!PooledMipRt.IsValid())
	{
		FPooledRenderTargetDesc ComputeShaderOutputDesc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(2,2)
																							, MipRt->GetRenderTargetResource()->TextureRHI->GetFormat()
																							, FClearValueBinding::Black
																							, TexCreate_ShaderResource
																							, TexCreate_ShaderResource | TexCreate_UAV
																							, false));

		ComputeShaderOutputDesc.DebugName = TEXT("WashTempPoolRtMip");
		GRenderTargetPool.FindFreeElement(RHICmdList
										, ComputeShaderOutputDesc
										, PooledMipRt
										, TEXT("WashTempPoolRtMip"));
	}

	
	FRDGTextureRef RdgTempRt = GraphBuilder.RegisterExternalTexture(PooledRt
																		, TEXT("RDG_TEMP_POOL_RT")
																		, ERenderTargetTexture::Targetable
																		, ERDGTextureFlags::MultiFrame);

	TRefCountPtr<IPooledRenderTarget> OutWashPoolRt = CreateRenderTarget(Rt->GetRenderTargetResource()->TextureRHI, *Rt->GetName());
	RdgOutRt = GraphBuilder.RegisterExternalTexture(OutWashPoolRt
													, TEXT("RDG_OUT_RT")
													, ERenderTargetTexture::ShaderResource
													, ERDGTextureFlags::MultiFrame);

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(Rt->SizeX, Rt->SizeY, 1);
	AddCopyTexturePass(GraphBuilder, RdgOutRt, RdgTempRt, CopyInfo);

	FPC_WashCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPC_WashCS::FParameters>();
	PassParameters->OutWashTex = GraphBuilder.CreateUAV(RdgTempRt);
	PassParameters->TexelScaleOffset = cachedParams.TexelScaleOffset;
	PassParameters->WashStrength = cachedParams.WashStrength;
	PassParameters->PointCount = cachedParams.HitPointUvList.Num();
	PassParameters->KSize = cachedParams.KSize;

	HitPointResArray.Reset();
	int PointNum = cachedParams.HitPointUvList.Num();
	for (int i = 0; i < PointNum; ++i)
		HitPointResArray.Add(cachedParams.HitPointUvList[i]);

	FRDGBufferRef PointBuffer = CreateVertexBuffer(GraphBuilder
												, TEXT("WashCsPointBuffer")
												, FRDGBufferDesc::CreateBufferDesc(sizeof(FVector2D), PointNum)
												, HitPointResArray.GetResourceData()
												, PointNum * sizeof(FVector2D)
												, ERDGInitialDataFlags::NoCopy);
	PassParameters->InHitPoints = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PointBuffer, PF_G32R32F));

	TShaderMapRef<FPC_WashCS> WashEffectCs(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder
								, RDG_EVENT_NAME("WashEffectCS")
								, WashEffectCs
								, PassParameters
								, FIntVector(1, 1, 1));


	AddCopyTexturePass(GraphBuilder, RdgTempRt, RdgOutRt, CopyInfo);

	/*
	 FIntPoint Size = FIntPoint(Rt->SizeX,Rt->SizeY);
	auto GroupCount = FComputeShaderUtils::GetGroupCount(Size, FComputeShaderUtils::kGolden2DGroupSize);
	GroupCount.X += 1;
	GroupCount.Y += 1;
	auto BufferCount = GroupCount.X * GroupCount.Y;
	//UE_LOG(LogTemp, Log, TEXT("Group Count: X:%d, Y:%d, BufferCount: %d"), GroupCount.X, GroupCount.Y, BufferCount);
	
	TShaderMapRef<FParallelReductionShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	
	//auto SumResultBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), BufferCount);
	auto ParallelReductionParameters = GraphBuilder.AllocParameters<FParallelReductionShader::FParameters>();
	auto SumResultBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BufferCount);
	SumResultBufferDesc.Usage |= BUF_SourceCopy;
	SumResultBufferDesc.UnderlyingType = FRDGBufferDesc::EUnderlyingType::VertexBuffer;
	auto SumResultBuffer = GraphBuilder.CreateBuffer(SumResultBufferDesc, TEXT("SumResultBuffer"));
	ParallelReductionParameters->SumResult = GraphBuilder.CreateUAV(SumResultBuffer, PF_R32_UINT);

	FRDGTextureSRVDesc WashRtSrvDesc = FRDGTextureSRVDesc::Create(RdgTempRt);
	ParallelReductionParameters->SourceTexture = GraphBuilder.CreateSRV(WashRtSrvDesc);

	AddClearUAVPass(GraphBuilder, ParallelReductionParameters->SumResult, 0u);
	//ClearUnusedGraphResources(ComputeShader, ParallelReductionParameters);
	FComputeShaderUtils::AddPass(GraphBuilder
								, RDG_EVENT_NAME("ParallelReductionSumShader.ExecuteParallelReductionSumShader")
								, ComputeShader
								, ParallelReductionParameters
								, GroupCount);
	
	auto ReadbackBuffer = StreamingRequestReadbackBuffers[ReadbackBuffersWriteIndex];

	AddEnqueueCopyPass(GraphBuilder, ReadbackBuffer, SumResultBuffer, sizeof(uint32));

	ReadbackBuffersWriteIndex = (ReadbackBuffersWriteIndex + 1u) % MaxStreamingReadbackBuffers;
	ReadbackBuffersNumPending = FMath::Min(ReadbackBuffersNumPending + 1u, MaxStreamingReadbackBuffers);
	*/

	ParallelReductionSum(GraphBuilder, RdgTempRt);
	//UE_LOG(LogTemp, Log, TEXT("Finished Readback: WriteIdx:%d, Pending:%d "), ReadbackBuffersWriteIndex, ReadbackBuffersNumPending);
	
	
	GraphBuilder.Execute();
}

void FCtrl_WashEffect::ParallelReductionSum(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	check(Texture);
	RDG_EVENT_SCOPE(GraphBuilder, "ParallelReductionSum");
	
	const FRDGTextureDesc& TextureDesc = Texture->Desc;
	
	UTextureRenderTarget2D* Rt = cachedParams.RenderTargetMips;
	const FIntPoint DestTextureSize(4,4);
	TRefCountPtr<IPooledRenderTarget> OutWashPoolRt = CreateRenderTarget(Rt->GetRenderTargetResource()->TextureRHI, *Rt->GetName());
	RdgMipRt = GraphBuilder.RegisterExternalTexture(OutWashPoolRt
													, TEXT("RDG_Mip_OUT_RT")
													, ERenderTargetTexture::ShaderResource
													, ERDGTextureFlags::MultiFrame);

	
	FRDGTextureRef RdgTempRt = GraphBuilder.RegisterExternalTexture(PooledMipRt
																	, TEXT("RDG_TEMP_POOL_RT_MIP")
																	, ERenderTargetTexture::Targetable
																	, ERDGTextureFlags::MultiFrame);
	
	TShaderMapRef<FParallelReductionMipsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FParallelReductionMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FParallelReductionMipsCS::FParameters>();
	PassParameters->InTex   = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Texture));
	PassParameters->OutResult  = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RdgTempRt));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Parallel Reduction"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(DestTextureSize, 4));

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(Rt->SizeX, Rt->SizeY, 1);
	AddCopyTexturePass(GraphBuilder, RdgTempRt, RdgMipRt, CopyInfo);

	//ReadBackTexture(GraphBuilder);
}

void FCtrl_WashEffect::ReadBackTexture(FRDGBuilder& GraphBuilder)
{

	RDG_EVENT_SCOPE(GraphBuilder, "ReadbackTexture");
	FReadSurfaceDataFlags ReadDataFlags;
	ReadDataFlags.SetLinearToGamma(false);
	ReadDataFlags.SetOutputStencil(false);
	ReadDataFlags.SetMip(0);

	FIntPoint Extent = FIntPoint(1, 1);
	
	AddReadbackTexturePass(GraphBuilder
							, RDG_EVENT_NAME("DirtnessReadBack")
							, RdgOutRt
							,[this, Extent, ReadDataFlags] (FRHICommandListImmediate& RHICmdList)
							{
								RHICmdList.ReadSurfaceData(this->RdgMipRt->GetRHI()
															, FIntRect(0, 0, Extent.X, Extent.Y)
															, this->ReadBackBuffer
															, ReadDataFlags);
								
								for(auto& Color : this->ReadBackBuffer)
								{
									UE_LOG(LogTemp, Log, TEXT("Color: %d, %d, %d"), Color.R, Color.G, Color.B)
								}
							});
}